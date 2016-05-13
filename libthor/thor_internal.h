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

#ifndef THOR_INTERNAL_H__
#define THOR_INTERNAL_H__

#include <libusb-1.0/libusb.h>

#include "thor.h"
#include "thor-proto.h"

#define DEFAULT_TIMEOUT 4000 /* 4000 ms */

#ifndef offsetof
#define offsetof(type, member) ((size_t) &((type *)0)->member)
#endif /* offsetof */

#define container_of(ptr, type, member) ({                      \
        const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
        (type *)( (char *)__mptr - offsetof(type,member) );})

#define ARRAY_SIZE(_a) (sizeof(_a)/sizeof(_a[0]))

struct thor_device_handle {
	libusb_device_handle *devh;
	int control_interface;
	int control_interface_id;
	int data_interface;
	int data_interface_id;
	int data_ep_in;
	int data_ep_out;
};

struct t_usb_transfer;

typedef void (*t_usb_transfer_cb)(struct t_usb_transfer *);

struct t_usb_transfer {
	struct libusb_transfer *ltransfer;
	t_usb_transfer_cb transfer_finished;
	size_t size;
	int ret;
	int cancelled;
};

struct t_thor_data_chunk {
	struct t_usb_transfer data_transfer;
	struct t_usb_transfer resp_transfer;
	void *user_data;
	size_t useful_size;
	struct data_res_pkt resp;
	unsigned char *buf;
	size_t trans_unit_size;
	int chunk_number;
	int data_finished;
	int resp_finished;
};

struct t_thor_data_transfer {
	struct thor_device_handle *th;
	struct thor_data_src *data;
	thor_progress_cb report_progress;
	void *user_data;
	size_t data_left;
	size_t data_sent;
	size_t data_in_progress;
	int chunk_number;
	int completed;
	int ret;
};


int t_usb_handle_events_completed(int *completed);

int t_usb_init_transfer(struct t_usb_transfer *t,
			libusb_device_handle *devh,
			unsigned char ep,
			unsigned char *buf, size_t size,
			t_usb_transfer_cb transfer_finished,
			unsigned int timeout);

static inline void t_usb_cleanup_transfer(struct t_usb_transfer *t)
{
	libusb_free_transfer(t->ltransfer);
}

static inline int t_usb_init_in_transfer(struct t_usb_transfer *t,
			   struct thor_device_handle *th,
			   unsigned char *buf, size_t size,
			   t_usb_transfer_cb transfer_finished,
			   unsigned int timeout)
{
	return t_usb_init_transfer(t, th->devh, th->data_ep_in, buf, size,
				   transfer_finished, timeout);
}

static inline int t_usb_init_out_transfer(struct t_usb_transfer *t,
			   struct thor_device_handle *th,
			   unsigned char *buf, size_t size,
			   t_usb_transfer_cb transfer_finished,
			   unsigned int timeout)
{
	return t_usb_init_transfer(t, th->devh, th->data_ep_out, buf, size,
				   transfer_finished, timeout);
}

static inline int t_usb_submit_transfer(struct t_usb_transfer *t)
{
	return libusb_submit_transfer(t->ltransfer);
}

static inline int t_usb_cancel_transfer(struct t_usb_transfer *t)
{
	return libusb_cancel_transfer(t->ltransfer);
}

int t_file_get_data_src(const char *path, struct thor_data_src **data);

int t_tar_get_data_src(const char *path, struct thor_data_src **data);

int t_usb_send(struct thor_device_handle *th, unsigned char *buf,
	       size_t count, int timeout);

int t_usb_recv(struct thor_device_handle *th, unsigned char *buf,
	       size_t count, int timeout);

int t_usb_send_req(struct thor_device_handle *th, request_type req_id,
		   int req_sub_id, int *idata, int icnt, char **sdata,
		   int scnt);

int t_usb_recv_req(struct thor_device_handle *th, struct res_pkt *resp);

int t_usb_find_device(struct thor_device_id *dev_id, int wait,
		      struct thor_device_handle *th);

void t_usb_close_device(struct thor_device_handle *th);

int t_acm_prepare_device(struct thor_device_handle *th);

#endif /* THOR_INTERNAL_H__ */

