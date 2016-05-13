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

#include <sys/types.h>
#include <stdio.h>
#include <endian.h>
#ifdef __linux__
#include <linux/usb/cdc.h>
#else
#include <stdint.h>

#ifdef __APPLE__
#include <libkern/OSByteOrder.h>

#define htole32(x) OSSwapHostToLittleInt32(x)
#endif

struct usb_cdc_line_coding {
	uint32_t	dwDTERate; /* little endian */
	uint8_t	bCharFormat;
#define USB_CDC_1_STOP_BITS			0

	uint8_t	bParityType;
#define USB_CDC_NO_PARITY			0

	uint8_t	bDataBits;
} __attribute__ ((packed));

#define USB_CDC_REQ_SET_LINE_CODING		0x20
#define USB_CDC_REQ_SET_CONTROL_LINE_STATE	0x22
#endif

#include <libusb-1.0/libusb.h>

#include "thor_internal.h"

static int acm_set_control_line_state(struct thor_device_handle *th, int state)
{
	int ret;

	ret = libusb_control_transfer(th->devh,
				      LIBUSB_REQUEST_TYPE_CLASS |
				      LIBUSB_RECIPIENT_INTERFACE,
				      USB_CDC_REQ_SET_CONTROL_LINE_STATE,
				      state ? 0x3 : 0,
				      (uint16_t)th->control_interface_id,
				      NULL,
				      0,
				      DEFAULT_TIMEOUT);

	if (ret < 0)
		return ret;

	return 0;
}

static int acm_set_line_coding(struct thor_device_handle *th)
{
	struct usb_cdc_line_coding default_thor_line_coding = {
		.dwDTERate = htole32(9600),
		.bCharFormat = USB_CDC_1_STOP_BITS,
		.bParityType = USB_CDC_NO_PARITY,
		.bDataBits = 8,
	};
	int ret;

	ret = libusb_control_transfer(th->devh,
				      LIBUSB_REQUEST_TYPE_CLASS |
				      LIBUSB_RECIPIENT_INTERFACE,
				      USB_CDC_REQ_SET_LINE_CODING,
				      0,
				      (uint16_t)th->control_interface_id,
				      (unsigned char *)&default_thor_line_coding,
				      sizeof(default_thor_line_coding),
				      DEFAULT_TIMEOUT);

	if (ret < 0)
		return ret;

	return 0;
}


int t_acm_prepare_device(struct thor_device_handle *th)
{
	int ret;

	ret = acm_set_control_line_state(th, 0);
	if (ret < 0)
		return ret;

	ret = acm_set_line_coding(th);
	if (ret < 0)
		return ret;

	ret = acm_set_control_line_state(th, 1);

	return ret;
}

