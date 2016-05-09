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

#define DEFAULT_TIMEOUT 1000 /* 1000 ms */

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

#endif /* THOR_INTERNAL_H__ */

