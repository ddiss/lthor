/*
 * libthor - Tizen Thor communication protocol
 *
 * Licensed under the Apache License, Version 2.0 (the License);
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "thor.h"
#include "thor_internal.h"

int thor_init()
{
	return libusb_init(NULL);
}

void thor_cleanup()
{
	libusb_exit(NULL);
}

int thor_check_proto(struct thor_device_id *dev_id)
{
	struct thor_device_handle *th;
	int ret;

	ret = thor_open(dev_id, 0, &th);
	if (!ret)
		thor_close(th);

	return ret;
}

static struct thor_device_id * thor_choose_id(
	struct thor_device_id *user_dev_id)
{
	static struct thor_device_id default_id = {
		.busid = NULL,
		.vid = 0x04e8,
		.pid = 0x685d,
		.serial = NULL,
	};

	if (user_dev_id->busid == NULL
	    && user_dev_id->vid < 0
	    && user_dev_id->pid < 0
	    && user_dev_id->serial == NULL)
		user_dev_id = &default_id;

	return user_dev_id;

}

static int t_thor_do_handshake(struct thor_device_handle *th)
{
	char challenge[] = "THOR";
	char response[] = "ROHT";
	char buffer[sizeof(response)];
	int ret;

	ret = t_usb_send(th, (unsigned char *)challenge, sizeof(challenge) - 1,
			 DEFAULT_TIMEOUT);
	if (ret < 0)
		return ret;

	ret = t_usb_recv(th, (unsigned char *)buffer, sizeof(buffer) - 1,
			 DEFAULT_TIMEOUT);
	if (ret < 0)
		return ret;

	buffer[sizeof(buffer) - 1] = '\0';

	if (strcmp(buffer, response))
		return -EINVAL;

	return 0;
}

int thor_open(struct thor_device_id *user_dev_id, int wait,
	      thor_device_handle **handle)
{
	struct thor_device_id *dev_id = thor_choose_id(user_dev_id);
	struct thor_device_handle *th;
	int found, ret;

	th = calloc(sizeof(*th), 1);
	if (!th)
		return -ENOMEM;

	found = t_usb_find_device(dev_id, wait, th);
	if (found <= 0) {
		ret = -ENODEV;
		goto close_dev;
	}

	ret = t_acm_prepare_device(th);
	if (ret)
		goto close_dev;

	ret = t_thor_do_handshake(th);
	if (ret) {
		ret = -EINVAL;
		goto close_dev;
	}

	*handle = th;
	return 0;
close_dev:
	thor_close(th);
	return ret;
}

void thor_close(thor_device_handle *th)
{
	t_usb_close_device(th);
	free(th);
}

static int t_thor_exec_cmd_full(thor_device_handle *th,  request_type req_id,
				int req_sub_id, int *idata, int icnt,
				char **sdata, int scnt, struct res_pkt *res)
{
	int ret;
	struct res_pkt resp;

	if (!res)
		res = &resp;

	ret = t_usb_send_req(th, req_id, req_sub_id, idata, icnt,
			     sdata, scnt);
	if (ret < 0)
		return ret;

	ret = t_usb_recv_req(th, res);
	if (ret < 0)
		return ret;

	return res->ack;
}

static int t_thor_exec_cmd(thor_device_handle *th,  request_type req_id,
			   int req_sub_id, int *idata, int icnt)
{
	return t_thor_exec_cmd_full(th, req_id, req_sub_id, idata, icnt,
				    NULL, 0, NULL);
}

int thor_start_session(thor_device_handle *th, size_t total)
{
	int ret;

	ret = t_thor_exec_cmd(th, RQT_DL, RQT_DL_INIT, (int *)&total, 1);

	return ret;
}

int thor_end_session(thor_device_handle *th)
{
	int ret;

	ret = t_thor_exec_cmd(th, RQT_DL, RQT_DL_EXIT, NULL, 0);

	return ret;
}

static int t_thor_send_chunk(thor_device_handle *th,
			     unsigned char *chunk, size_t size,
			     int chunk_number)
{
	struct data_res_pkt resp;
	int ret;

	ret = t_usb_send(th, chunk, size, DEFAULT_TIMEOUT);
	if (ret < 0)
		return ret;

	memset(&resp, 0, DATA_RES_PKT_SIZE);

	ret = t_usb_recv(th, &resp, DATA_RES_PKT_SIZE, DEFAULT_TIMEOUT);
	if (ret < 0)
		return ret;

	if (resp.cnt != chunk_number)
		return ret;

	return resp.ack;
}

static int t_thor_send_raw_data(thor_device_handle *th,
				struct thor_data_src *data,
				size_t trans_unit_size,
				thor_progress_cb report_progress,
				void *user_data)
{
	unsigned char *chunk;
	size_t data_left;
	size_t size;
	size_t data_sent = 0;
	int chunk_number = 1;
	int ret;

	chunk = malloc(trans_unit_size);
	if (!chunk)
		return -ENOMEM;

	data_left = data->get_file_length(data);

	while (data_left) {
		size = data_left > trans_unit_size ?
			trans_unit_size : data_left;

		ret = data->get_block(data, chunk, size);
		if (ret < 0 || ret != size)
			goto cleanup;

		memset(chunk + size, 0, trans_unit_size - size);
		if (th) {
			ret = t_thor_send_chunk(th, chunk, trans_unit_size,
						chunk_number);
			if (ret)
				goto cleanup;
		}

		data_sent += size;
		data_left -= size;
		++chunk_number;
		if (report_progress)
			report_progress(th, data, data_sent, data_left,
					chunk_number, user_data);
	}

	ret = 0;
cleanup:
	free(chunk);
	return ret;;

}

int thor_send_data(thor_device_handle *th, struct thor_data_src *data,
		   enum thor_data_type type, thor_progress_cb report_progress,
		   void *user_data, thor_next_entry_cb report_next_entry,
		   void *ne_cb_data)
{
	size_t filesize;
	const char *filename;
	struct res_pkt resp;
	int32_t int_data[2];
	size_t trans_unit_size;
	int ret;

	while (1) {
		ret = data->next_file(data);
		if (ret <= 0)
			break;
		if (report_next_entry)
			report_next_entry(th, data, ne_cb_data);

		filesize = data->get_file_length(data);
		filename = data->get_name(data);

		int_data[0] = type;
		int_data[1] = filesize;

		ret = t_thor_exec_cmd_full(th, RQT_DL, RQT_DL_FILE_INFO,
					   int_data, ARRAY_SIZE(int_data),
					   (char **)&filename, 1, &resp);
		if (ret < 0)
			return ret;

		trans_unit_size = resp.int_data[0];

		if (th) {
			ret = t_thor_exec_cmd(th, RQT_DL, RQT_DL_FILE_START,
					      NULL, 0);
			if (ret < 0)
				return ret;
		}

		ret = t_thor_send_raw_data(th, data, trans_unit_size,
					   report_progress, user_data);
		if (ret < 0)
			return ret;

		if (th) {
			ret = t_thor_exec_cmd(th, RQT_DL, RQT_DL_FILE_END,
					      NULL, 0);
			if (ret < 0)
				return ret;
		}
	}

	return 0;
}

int thor_reboot(thor_device_handle *th)
{
	int ret;

	ret = t_thor_exec_cmd(th, RQT_CMD, RQT_CMD_REBOOT, NULL, 0);

	return ret;
}

int thor_get_data_src(const char *path, enum thor_data_src_format format,
		      struct thor_data_src **data)
{
	int ret;

	switch (format) {
	case THOR_FORMAT_RAW:
		ret = t_file_get_data_src(path, data);
		break;
	case THOR_FORMAT_TAR:
		ret = t_tar_get_data_src(path, data);
		break;
	default:
		ret = -ENOTSUP;
	}

	return ret;
}

void thor_release_data_src(struct thor_data_src *data)
{
	if (data->release)
		data->release(data);
}

