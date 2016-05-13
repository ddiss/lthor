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

#ifndef THOR_H__
#define THOR_H__

#include <sys/types.h>
#include <stdio.h>
#include <stddef.h>

struct thor_device_id {
	const char *busid;
	int vid;
	int pid;
	const char *serial;
};

struct thor_device_handle;
typedef struct thor_device_handle thor_device_handle;

enum thor_data_type {
	THOR_NORMAL_DATA = 0,
	THOR_PIT_DATA,
};

struct thor_data_src {
	off_t (*get_file_length)(struct thor_data_src *src);
	off_t (*get_size)(struct thor_data_src *src);
	off_t (*get_block)(struct thor_data_src *src, void *data, off_t len);
	const char* (*get_name)(struct thor_data_src *src);
	int (*next_file)(struct thor_data_src *src);
	void (*release)(struct thor_data_src *src);
};

enum thor_data_src_format {
	THOR_FORMAT_RAW = 0,
	THOR_FORMAT_TAR,
};

typedef void (*thor_progress_cb)(thor_device_handle *th,
				 struct thor_data_src *data,
				 int sent, int left, int chunk_nmb,
				 void *user_data);

typedef void (*thor_next_entry_cb)(thor_device_handle *th,
				 struct thor_data_src *data,
				 void *user_data);

/* Init the Thor library */
int thor_init();

/* Cleanup the thor library */
void thor_cleanup();

/* Check if device is thor compatible */
int thor_check_proto(struct thor_device_id *dev_id);

/* Open the device and prepare it for thor communication */
int thor_open(struct thor_device_id *dev_id, int wait,
	      thor_device_handle **handle);

/* Close the device */
void thor_close(thor_device_handle *th);

/* Start thor "session" */
int thor_start_session(thor_device_handle *th, off_t total);

/* Send a butch of data to the target */
int thor_send_data(thor_device_handle *th, struct thor_data_src *data,
		   enum thor_data_type type, thor_progress_cb report_progress,
		   void *user_data, thor_next_entry_cb report_next_entry,
		   void *ne_cb_data);

/* End the session */
int thor_end_session(thor_device_handle *th);

/* Open a standard file or archive as data source for thor */
int thor_get_data_src(const char *path, enum thor_data_src_format format,
		      struct thor_data_src **data);

/* Release data source */
void thor_release_data_src(struct thor_data_src *data);

/* Request target reboot */
int thor_reboot(thor_device_handle *th);

#endif /* THOR_H__ */

