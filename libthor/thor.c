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
#include <assert.h>

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

static int odin_do_handshake(struct thor_device_handle *th)
{
	char challenge[] = "ODIN";
	char response[] = "LOKE";
	char buffer[sizeof(response)];
	int ret;

	fprintf(stderr, "sending challenge...\n");
	ret = t_usb_send(th, (unsigned char *)challenge, sizeof(challenge) - 1,
			 DEFAULT_TIMEOUT);
	if (ret < 0)
		return ret;

	fprintf(stderr, "receiving challenge response...\n");
	ret = t_usb_recv(th, (unsigned char *)buffer, sizeof(buffer) - 1,
			 DEFAULT_TIMEOUT);
	if (ret < 0)
		return ret;

	buffer[sizeof(buffer) - 1] = '\0';

	if (strcmp(buffer, response))
		return -EINVAL;
	fprintf(stderr, "handshake successful\n");

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


	th->odin_mode = user_dev_id->odin_mode;
	if (th->odin_mode)
		ret = odin_do_handshake(th);
	else
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

int thor_start_session(thor_device_handle *th, off_t total)
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

int thor_odin_start_session(thor_device_handle *th, uint32_t *_xfer_size)
{
	int ret;
	struct rqt_odin_dl_init rqt = {0};
	struct rsp_odin_dl_init rsp = {0};
	uint8_t buf[RQT_ODIN_PACKED_DL_INIT_LEN];

	assert(th->odin_mode);
	rqt.id = RQT_ODIN_DL_INIT;
	rqt.subid = RQT_ODIN_DL_INIT_BEGIN;
	/* xfer_size zero */

	ret = rqt_odin_pack_dl_init(&rqt, buf, RQT_ODIN_PACKED_DL_INIT_LEN);
	if (ret < 0)
		return ret;

	ret = t_usb_send(th, buf, RQT_ODIN_PACKED_DL_INIT_LEN, DEFAULT_TIMEOUT);
	if (ret < 0)
		return ret;

	/* TODO: use build-time assert */
	assert(RSP_ODIN_PACKED_DL_INIT_LEN <= sizeof(buf));
	ret = t_usb_recv(th, buf, RSP_ODIN_PACKED_DL_INIT_LEN, DEFAULT_TIMEOUT);
	if (ret < 0)
		return ret;

	ret = rsp_odin_unpack_dl_init(buf, RSP_ODIN_PACKED_DL_INIT_LEN, &rsp);
	if (ret < 0)
		return ret;

	if (rsp.xfer_size == 0) {
		fprintf(stderr, "error: Loke xfer_size zero!\n");
		return -EIO;
	}

	fprintf(stderr, "Odin session initialised with Loke xfer_size: %u\n",
		rsp.xfer_size);
	*_xfer_size = rsp.xfer_size;

	return 0;
}

int thor_odin_end_session(thor_device_handle *th)
{
	int ret;
	struct rqt_odin_dl_end rqt = {0};
	struct rsp_odin_dl_end rsp = {0};
	uint8_t buf[RQT_ODIN_PACKED_DL_END_LEN];

	assert(th->odin_mode);
	rqt.id = RQT_ODIN_DL_END;
	rqt.subid = RQT_ODIN_DL_END_REG;

	ret = rqt_odin_pack_dl_end(&rqt, buf, RQT_ODIN_PACKED_DL_END_LEN);
	if (ret < 0)
		return ret;

	ret = t_usb_send(th, buf, RQT_ODIN_PACKED_DL_END_LEN, DEFAULT_TIMEOUT);
	if (ret < 0)
		return ret;

	/* TODO: use build-time assert */
	assert(RSP_ODIN_PACKED_DL_END_LEN <= sizeof(buf));
	ret = t_usb_recv(th, buf, RSP_ODIN_PACKED_DL_END_LEN, DEFAULT_TIMEOUT);
	if (ret < 0)
		return ret;

	ret = rsp_odin_unpack_dl_end(buf, RSP_ODIN_PACKED_DL_END_LEN, &rsp);
	if (ret < 0)
		return ret;

	fprintf(stderr, "Odin session ended with unknown: %u\n", rsp.unknown);

	return 0;
}

int thor_odin_session_set_xfer_size(thor_device_handle *th,
				    uint32_t xfer_size)
{
	int ret;
	struct rqt_odin_dl_init rqt = {0};
	struct rsp_odin_dl_init rsp = {0};
	uint8_t buf[RQT_ODIN_PACKED_DL_INIT_LEN];

	assert(th->odin_mode);
	rqt.id = RQT_ODIN_DL_INIT;
	rqt.subid = RQT_ODIN_DL_INIT_XFER_SIZE;
	rqt.xfer_size = xfer_size;

	ret = rqt_odin_pack_dl_init(&rqt, buf, RQT_ODIN_PACKED_DL_INIT_LEN);
	if (ret < 0)
		return ret;

	ret = t_usb_send(th, buf, RQT_ODIN_PACKED_DL_INIT_LEN, DEFAULT_TIMEOUT);
	if (ret < 0)
		return ret;

	/* TODO: use build-time assert */
	assert(RSP_ODIN_PACKED_DL_INIT_LEN <= sizeof(buf));
	ret = t_usb_recv(th, buf, RSP_ODIN_PACKED_DL_INIT_LEN, DEFAULT_TIMEOUT);
	if (ret < 0)
		return ret;

	ret = rsp_odin_unpack_dl_init(buf, RSP_ODIN_PACKED_DL_INIT_LEN, &rsp);
	if (ret < 0)
		return ret;

	fprintf(stderr, "Odin session initialised xfer size to %u (rsp %u)\n",
		xfer_size, rsp.xfer_size);

	return 0;
}

int thor_odin_session_use_sd(thor_device_handle *th)
{
	int ret;
	struct rqt_odin_dl_init rqt = {0};
	struct rsp_odin_dl_init rsp = {0};
	uint8_t buf[RQT_ODIN_PACKED_DL_INIT_LEN];

	assert(th->odin_mode);
	rqt.id = RQT_ODIN_DL_INIT;
	rqt.subid = RQT_ODIN_DL_INIT_TF;
	/* xfer_size zero */

	ret = rqt_odin_pack_dl_init(&rqt, buf, RQT_ODIN_PACKED_DL_INIT_LEN);
	if (ret < 0)
		return ret;

	ret = t_usb_send(th, buf, RQT_ODIN_PACKED_DL_INIT_LEN, DEFAULT_TIMEOUT);
	if (ret < 0)
		return ret;

	/* TODO: use build-time assert */
	assert(RSP_ODIN_PACKED_DL_INIT_LEN <= sizeof(buf));
	ret = t_usb_recv(th, buf, RSP_ODIN_PACKED_DL_INIT_LEN, DEFAULT_TIMEOUT);
	if (ret < 0)
		return ret;

	ret = rsp_odin_unpack_dl_init(buf, RSP_ODIN_PACKED_DL_INIT_LEN, &rsp);
	if (ret < 0)
		return ret;

	fprintf(stderr, "Odin session initialised to use SD card (sz %u)\n",
		rsp.xfer_size);

	return 0;
}

int thor_odin_start_pit_dump(thor_device_handle *th, uint32_t *_dump_total)
{
	int ret;
	struct rqt_odin_pit rqt = {0};
	struct rsp_odin_pit rsp = {0};
	uint8_t buf[RQT_ODIN_PACKED_PIT_LEN];

	assert(th->odin_mode);
	rqt.id = RQT_ODIN_PIT;
	rqt.subid = RQT_ODIN_PIT_DUMP;

	ret = rqt_odin_pack_pit(&rqt, buf, RQT_ODIN_PACKED_PIT_LEN);
	if (ret < 0)
		return ret;

	ret = t_usb_send(th, buf, RQT_ODIN_PACKED_PIT_LEN, DEFAULT_TIMEOUT);
	if (ret < 0)
		return ret;

	/* TODO: use build-time assert */
	assert(RSP_ODIN_PACKED_PIT_LEN <= sizeof(buf));
	ret = t_usb_recv(th, buf, RSP_ODIN_PACKED_PIT_LEN, DEFAULT_TIMEOUT);
	if (ret < 0)
		return ret;

	ret = rsp_odin_unpack_pit(buf, RSP_ODIN_PACKED_PIT_LEN, &rsp);
	if (ret < 0)
		return ret;

	fprintf(stderr, "Odin pit dump initiated. total: %u\n", rsp.total_len);
	*_dump_total = rsp.total_len;

	return 0;
}

int thor_odin_end_pit_dump(thor_device_handle *th)
{
	int ret;
	struct rqt_odin_pit rqt = {0};
	struct rsp_odin_pit rsp = {0};
	uint8_t buf[RQT_ODIN_PACKED_PIT_LEN];

	assert(th->odin_mode);
	rqt.id = RQT_ODIN_PIT;
	rqt.subid = RQT_ODIN_PIT_XFER_END;

	ret = rqt_odin_pack_pit(&rqt, buf, RQT_ODIN_PACKED_PIT_LEN);
	if (ret < 0)
		return ret;

	ret = t_usb_send(th, buf, RQT_ODIN_PACKED_PIT_LEN, DEFAULT_TIMEOUT);
	if (ret < 0)
		return ret;

	/* TODO: use build-time assert */
	assert(RSP_ODIN_PACKED_PIT_LEN <= sizeof(buf));
	ret = t_usb_recv(th, buf, RSP_ODIN_PACKED_PIT_LEN, DEFAULT_TIMEOUT);
	if (ret < 0)
		return ret;

	ret = rsp_odin_unpack_pit(buf, RSP_ODIN_PACKED_PIT_LEN, &rsp);
	if (ret < 0)
		return ret;

	fprintf(stderr, "Odin pit dump ended. total: %u\n", rsp.total_len);

	return 0;
}

static int t_thor_submit_chunk(struct t_thor_data_chunk *chunk)
{
	int ret;

	chunk->data_finished = chunk->resp_finished = 0;

	ret = t_usb_submit_transfer(&chunk->data_transfer);
	if (ret)
		goto out;

	memset(&chunk->resp, 0, DATA_RES_PKT_SIZE);
	ret = t_usb_submit_transfer(&chunk->resp_transfer);
	if (ret)
		goto cancel_data_transfer;

	return 0;
cancel_data_transfer:
	t_usb_cancel_transfer(&chunk->data_transfer);
out:
	return ret;
}

static int t_thor_prep_next_chunk(struct t_thor_data_chunk *chunk,
				  struct t_thor_data_transfer *transfer_data)
{
	off_t to_read;
	int ret;

	to_read = transfer_data->data_left - transfer_data->data_in_progress;
	if (to_read <= 0) {
		printf("to big data in progress\n");
		fflush(stdout);
		return -EINVAL;
	}

	chunk->useful_size = to_read > chunk->trans_unit_size ?
		chunk->trans_unit_size : to_read;

	ret = transfer_data->data->get_block(transfer_data->data,
					  chunk->buf, chunk->useful_size);
	if (ret < 0 || ret != chunk->useful_size)
		return ret;

	memset(chunk->buf + chunk->useful_size, 0,
	       chunk->trans_unit_size - chunk->useful_size);
	chunk->chunk_number = transfer_data->chunk_number++;

	ret = t_thor_submit_chunk(chunk);
	if (!ret)
		transfer_data->data_in_progress += chunk->useful_size;

	return ret;
}

static void check_next_chunk(struct t_thor_data_chunk *chunk,
			     struct t_thor_data_transfer *transfer_data)
{
	/* If there is some more data to be queued */
	if (transfer_data->data_left - transfer_data->data_in_progress) {
		int ret;

		ret = t_thor_prep_next_chunk(chunk, transfer_data);
		if (ret) {
			transfer_data->ret = ret;
			transfer_data->completed = 1;
		}
	} else {
		/* Last one turns the light off */
		if (transfer_data->data_in_progress == 0)
			transfer_data->completed = 1;
	}
}

static void data_transfer_finished(struct t_usb_transfer *_data_transfer)
{
	struct t_thor_data_chunk *chunk = container_of(_data_transfer,
						       struct t_thor_data_chunk,
						       data_transfer);
	struct t_thor_data_transfer *transfer_data = chunk->user_data;

	chunk->data_finished = 1;

	if (_data_transfer->cancelled || transfer_data->ret)
		return;

	if (_data_transfer->ret) {
		transfer_data->ret = _data_transfer->ret;
		transfer_data->completed = 1;
	}

	if (chunk->resp_finished)
		check_next_chunk(chunk, transfer_data);
}

static void resp_transfer_finished(struct t_usb_transfer *_resp_transfer)
{
	struct t_thor_data_chunk *chunk = container_of(_resp_transfer,
						       struct t_thor_data_chunk,
						       resp_transfer);
	struct t_thor_data_transfer *transfer_data = chunk->user_data;

	chunk->resp_finished = 1;
	transfer_data->data_in_progress -= chunk->useful_size;

	if (_resp_transfer->cancelled || transfer_data->ret) {
		if (transfer_data->data_in_progress == 0)
			transfer_data->completed = 1;
		return;
	}

	if (_resp_transfer->ret) {
		transfer_data->ret = _resp_transfer->ret;
		goto complete_all;
	}

	if (chunk->resp.cnt != chunk->chunk_number) {
		printf ("chunk number mismatch: %d != %d\n",
			chunk->resp.cnt, chunk->chunk_number);
		fflush(stdout);
		transfer_data->ret = -EINVAL;
		goto complete_all;
	}

	transfer_data->data_sent += chunk->useful_size;
	transfer_data->data_left -= chunk->useful_size;
	if (transfer_data->report_progress)
		transfer_data->report_progress(transfer_data->th,
					       transfer_data->data,
					       transfer_data->data_sent,
					       transfer_data->data_left,
					       chunk->chunk_number,
					       transfer_data->user_data);

	if (chunk->data_finished)
		check_next_chunk(chunk, transfer_data);

	return;
complete_all:
	transfer_data->completed = 1;
	return;
}

static int t_thor_init_chunk(struct t_thor_data_chunk *chunk,
			     thor_device_handle *th,
			     off_t trans_unit_size,
			     void *user_data)
{
	int ret;

	chunk->user_data = user_data;
	chunk->useful_size = 0;
	chunk->trans_unit_size = trans_unit_size;

	chunk->buf = malloc(trans_unit_size);
	if (!chunk->buf)
		return -ENOMEM;

	ret = t_usb_init_out_transfer(&chunk->data_transfer, th, chunk->buf,
				     trans_unit_size, data_transfer_finished,
				     DEFAULT_TIMEOUT);
	if (ret)
		goto free_buf;

	ret = t_usb_init_in_transfer(&chunk->resp_transfer, th,
				     (unsigned char *)&chunk->resp,
				      DATA_RES_PKT_SIZE,
				      resp_transfer_finished,
				      2*DEFAULT_TIMEOUT);
	if (ret)
		goto cleanup_data_transfer;

	return 0;
cleanup_data_transfer:
	t_usb_cleanup_transfer(&chunk->data_transfer);
free_buf:
	free(chunk->buf);

	return ret;
}

static void t_thor_cleanup_chunk(struct t_thor_data_chunk *chunk)
{
	t_usb_cleanup_transfer(&chunk->data_transfer);
	t_usb_cleanup_transfer(&chunk->resp_transfer);
	free(chunk->buf);
}

static inline int
t_thor_handle_events(struct t_thor_data_transfer *transfer_data)
{
	return t_usb_handle_events_completed(&transfer_data->completed);
}

static inline void t_thor_cancel_chunk(struct t_thor_data_chunk *chunk)
{
	t_usb_cancel_transfer(&chunk->data_transfer);
	t_usb_cancel_transfer(&chunk->resp_transfer);
}

static int t_thor_send_raw_data(thor_device_handle *th,
				struct thor_data_src *data,
				off_t trans_unit_size,
				thor_progress_cb report_progress,
				void *user_data)
{
	struct t_thor_data_chunk chunk[3];
	struct t_thor_data_transfer transfer_data;
	int i, j;
	int ret;

	for (i = 0; i < ARRAY_SIZE(chunk); ++i) {
		ret = t_thor_init_chunk(chunk + i, th, trans_unit_size,
					&transfer_data);
		if (ret)
			goto cleanup_chunks;
	}

	transfer_data.data = data;
	transfer_data.report_progress = report_progress;
	transfer_data.user_data = user_data;
	transfer_data.data_left = data->get_file_length(data);
	transfer_data.data_sent = 0;
	transfer_data.chunk_number = 1;
	transfer_data.completed = 0;
	transfer_data.data_in_progress = 0;
	transfer_data.ret = 0;

	for (i = 0;
	     i < ARRAY_SIZE(chunk)
	      && (transfer_data.data_left - transfer_data.data_in_progress > 0);
	     ++i) {
		ret = t_thor_prep_next_chunk(chunk + i, &transfer_data);
		if (ret)
			goto cancel_chunks;
	}

	t_thor_handle_events(&transfer_data);

	if (transfer_data.data_in_progress) {
		ret = transfer_data.ret;
		goto cancel_chunks;
	}

	for (i = 0; i < ARRAY_SIZE(chunk); ++i)
		t_thor_cleanup_chunk(chunk + i);

	return transfer_data.ret;

cancel_chunks:
	for (j = 0; j < i; ++j)
		t_thor_cancel_chunk(chunk + j);
	if (i) {
		transfer_data.completed = 0;
		t_thor_handle_events(&transfer_data);
	}

	i = ARRAY_SIZE(chunk);
cleanup_chunks:
	for (j = 0; j < i; ++j)
		t_thor_cleanup_chunk(chunk + j);

	return ret;
}

int thor_send_data(thor_device_handle *th, struct thor_data_src *data,
		   enum thor_data_type type, thor_progress_cb report_progress,
		   void *user_data, thor_next_entry_cb report_next_entry,
		   void *ne_cb_data)
{
	off_t filesize;
	const char *filename;
	struct res_pkt resp;
	int32_t int_data[2];
	off_t trans_unit_size;
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

		if (!th)
			continue;

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

#define MIN(a, b) ((a) < (b) ? (a) : (b))
static int thor_odin_recv_raw_data(thor_device_handle *th,
				struct thor_data_src *data,
				off_t trans_unit_size,
				thor_progress_cb report_progress,
				void *user_data)
{
	struct t_thor_data_transfer transfer_data;
	uint8_t *buf;
	int i, j;
	int ret;

	assert(th->odin_mode);

	/* XXX FIXME transfer unit is always 500?! */
	trans_unit_size = 500;

	buf = malloc(trans_unit_size);
	if (buf == NULL)
		return -ENOMEM;

	transfer_data.data = data;
	transfer_data.report_progress = report_progress;
	transfer_data.user_data = user_data;
	transfer_data.data_left = data->get_file_length(data);
	transfer_data.data_sent = 0;
	transfer_data.chunk_number = 0;
	transfer_data.completed = 0;		/* XXX unused */
	transfer_data.data_in_progress = 0;	/* XXX unused */
	transfer_data.ret = 0;

	while (transfer_data.data_left > 0) {
		struct rqt_odin_pit rqt = {0};
		uint8_t rqt_buf[RQT_ODIN_PACKED_PIT_LEN];
		off_t this_xfer = MIN(transfer_data.data_left, trans_unit_size);

		rqt.id = RQT_ODIN_PIT;
		rqt.subid = RQT_ODIN_PIT_PART;
		rqt.part_off = transfer_data.chunk_number;

		ret = rqt_odin_pack_pit(&rqt, rqt_buf, RQT_ODIN_PACKED_PIT_LEN);
		if (ret < 0) {
			transfer_data.ret = ret;
			break;
		}

		ret = t_usb_send(th, rqt_buf, RQT_ODIN_PACKED_PIT_LEN,
				DEFAULT_TIMEOUT);
		if (ret < 0) {
			transfer_data.ret = ret;
			break;
		}

		transfer_data.chunk_number++;

		ret = t_usb_recv(th, buf, this_xfer, DEFAULT_TIMEOUT);
		if (ret < 0) {
			transfer_data.ret = ret;
			break;
		}

		transfer_data.data_left -= this_xfer;
		transfer_data.data_sent += this_xfer;
		ret = data->put_block(data, buf, this_xfer);
		if (ret < 0) {
			transfer_data.ret = ret;
			break;
		}

		transfer_data.report_progress(transfer_data.th,
					      transfer_data.data,
					      transfer_data.data_sent,
					      transfer_data.data_left,
					      transfer_data.chunk_number,
					      transfer_data.user_data);
	}

	/*
	 * All done receiving PIT data.
	 * XXX strangely, sometimes an empty bulk transfer is needed after
	 * receiving, otherwise the PIT_END won't be processed.
	 * - Galaxy Tab S2: required
	 * - Galaxy S8: not needed
	 */
	t_usb_recv(th, buf, 0, 1);

	free(buf);
	return transfer_data.ret;
}

int thor_odin_recv_pit_data(thor_device_handle *th, uint32_t chunk_size,
			    uint32_t dump_total, struct thor_data_src *data,
			    enum thor_data_type type,
			    thor_progress_cb report_progress,
			    void *user_data,
			    thor_next_entry_cb report_next_entry,
			    void *ne_cb_data)
{
	off_t filesize;
	const char *filename;
	struct res_pkt resp;
	off_t trans_unit_size;
	int ret;

	assert(type == THOR_PIT_DATA);
	ret = data->next_file(data);
	if (ret <= 0) {
		fprintf(stderr, "invalid data dest\n");
		return -EBADF;
	}
	if (report_next_entry)
		report_next_entry(th, data, ne_cb_data);

	if (!th) {
		fprintf(stderr, "skipping chunk recv\n");
		return 0;
	}

	filename = data->get_name(data);
	ret = data->set_file_length(data, dump_total);
	if (ret < 0) {
		fprintf(stderr, "failed to truncate %s\n", filename);
		return ret;
	}

	ret = thor_odin_recv_raw_data(th, data, chunk_size,
				   report_progress, user_data);
	if (ret < 0) {
		fprintf(stderr, "failed to recv data for %s\n", filename);
		return ret;
	}

	return 0;
}

int thor_reboot(thor_device_handle *th)
{
	int ret;

	ret = t_thor_exec_cmd(th, RQT_CMD, RQT_CMD_REBOOT, NULL, 0);

	return ret;
}

int thor_odin_reboot(thor_device_handle *th)
{
	int ret;
	struct rqt_odin_dl_end rqt = {0};
	struct rsp_odin_dl_end rsp = {0};
	uint8_t buf[RQT_ODIN_PACKED_DL_END_LEN];

	assert(th->odin_mode);
	rqt.id = RQT_ODIN_DL_END;
	rqt.subid = RQT_ODIN_DL_END_REBOOT;

	ret = rqt_odin_pack_dl_end(&rqt, buf, RQT_ODIN_PACKED_DL_END_LEN);
	if (ret < 0)
		return ret;

	ret = t_usb_send(th, buf, RQT_ODIN_PACKED_DL_END_LEN, DEFAULT_TIMEOUT);
	if (ret < 0)
		return ret;

	/* TODO: use build-time assert */
	assert(RSP_ODIN_PACKED_DL_END_LEN <= sizeof(buf));
	ret = t_usb_recv(th, buf, RSP_ODIN_PACKED_DL_END_LEN, DEFAULT_TIMEOUT);
	if (ret < 0)
		return ret;

	ret = rsp_odin_unpack_dl_end(buf, RSP_ODIN_PACKED_DL_END_LEN, &rsp);
	if (ret < 0)
		return ret;

	fprintf(stderr, "Odin reboot succeeded with unknown: %u\n", rsp.unknown);

	return 0;
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

int thor_get_data_dest(const char *path, enum thor_data_src_format format,
		      struct thor_data_src **data)
{
	int ret;

	switch (format) {
	case THOR_FORMAT_RAW:
		ret = t_file_get_data_dest(path, data);
		break;
	default:
		/* THOR_FORMAT_TAR not yet supported as a dest */
		ret = -ENOTSUP;
	}

	return ret;
}

void thor_release_data_src(struct thor_data_src *data)
{
	if (data->release)
		data->release(data);
}

