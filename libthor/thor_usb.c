#include <sys/types.h>
#include <stdio.h>
#include <endian.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#ifdef __linux__
#include <linux/usb/cdc.h>
#else
#define USB_CDC_SUBCLASS_ACM			0x02

#define USB_CDC_PROTO_NONE			0
#define USB_CDC_ACM_PROTO_AT_V25TER		1
#endif
#ifdef __linux__
#include <linux/usb/ch9.h>
#else
#include <stdint.h>

#define USB_DT_INTERFACE_ASSOCIATION	0x0b

#define USB_CLASS_COMM			2
#define USB_CLASS_CDC_DATA		0x0a

struct usb_descriptor_header {
	uint8_t  bLength;
	uint8_t  bDescriptorType;
} __attribute__ ((packed));

struct usb_interface_assoc_descriptor {
	uint8_t  bLength;
	uint8_t  bDescriptorType;

	uint8_t  bFirstInterface;
	uint8_t  bInterfaceCount;
	uint8_t  bFunctionClass;
	uint8_t  bFunctionSubClass;
	uint8_t  bFunctionProtocol;
	uint8_t  iFunction;
} __attribute__ ((packed));
#endif
#include <libusb-1.0/libusb.h>

#include "thor.h"
#include "thor_internal.h"

#define MAX_SERIAL_LEN 256

struct hotplug_helper {
	struct thor_device_handle *th;
	struct thor_device_id *dev_id;
	int completed;
};

static int check_busid_match(const char *expected, libusb_device *dev)
{
	/* Max USB depth is 7 */
	uint8_t dev_port[8];
	int nports;
	uint8_t bus_number;
	int val;
	int i;
	int ret;

	bus_number = libusb_get_bus_number(dev);
	ret = sscanf(expected, "%d", &val);
	if (ret < 1)
		return -EINVAL;

	if (val != bus_number)
		return 0;

	expected = strchr(expected, '-');
	if (!expected)
		return -EINVAL;

	nports = libusb_get_port_numbers(dev, (uint8_t *)dev_port, sizeof(dev_port));
	if (nports < 0)
		return nports;


	for (i = 0; i < nports; ++i) {
		ret = sscanf(expected, "%d", &val);
		if (ret < 1)
			return -EINVAL;

		if (val != dev_port[i])
			return 0;

		expected = strchr(expected, '.');
		if (!expected) {
			if (i + 1 == nports)
				return 1;
			else
				break;
		}
	}

	return 0;
}

static int check_vid_pid_match(int vid, int pid, libusb_device *dev)
{
	struct libusb_device_descriptor desc;
	int ret;

	ret = libusb_get_device_descriptor(dev, &desc);
	if (ret < 0)
		return ret;

	if (vid >= 0 && vid != desc.idVendor)
		return 0;

	if (pid >= 0 && pid != desc.idProduct)
		return 0;

	return 1;
}

static int check_serial_match(const char *serial, libusb_device *dev,
		       libusb_device_handle **devh)
{
	char buf[MAX_SERIAL_LEN];
	struct libusb_device_descriptor desc;
	libusb_device_handle *handle;
	int ret;

	ret = libusb_get_device_descriptor(dev, &desc);
	if (ret < 0)
		return ret;

	ret = libusb_open(dev, &handle);
	if (ret < 0)
		return ret;

	ret = libusb_get_string_descriptor_ascii(handle, desc.iSerialNumber,
						 (unsigned char*)buf,
						 sizeof(buf));
	if (ret < 0)
		return ret;

	if (strcmp(serial, buf))
		return 0;

	*devh = handle;
	return 1;
}

static inline int
is_data_interface(const struct libusb_interface_descriptor *idesc)
{
	return idesc->bInterfaceClass == USB_CLASS_CDC_DATA;
}

static inline int
is_control_interface(const struct libusb_interface_descriptor *idesc)
{
	return idesc->bInterfaceClass == USB_CLASS_COMM
		&& idesc->bInterfaceSubClass == USB_CDC_SUBCLASS_ACM
		&& idesc->bInterfaceProtocol == USB_CDC_ACM_PROTO_AT_V25TER;
}

static int find_idesc_by_id(struct libusb_config_descriptor *cdesc, int id)
{
	int i;

	for (i = 0; i < cdesc->bNumInterfaces; ++i)
		if (cdesc->interface[i].altsetting[0].bInterfaceNumber == id)
			return i;

	return -ENODEV;
}

static int check_assoc(struct libusb_config_descriptor *cdesc,
		       struct usb_interface_assoc_descriptor *assoc_desc,
		       struct thor_device_handle *th)
{
	int intf_a, intf_b;

	if (assoc_desc->bInterfaceCount != 2
	    || assoc_desc->bFunctionClass != USB_CLASS_COMM
	    || assoc_desc->bFunctionSubClass != USB_CDC_SUBCLASS_ACM
	    || assoc_desc->bFunctionProtocol != USB_CDC_PROTO_NONE)
		return -EINVAL;

	intf_a = find_idesc_by_id(cdesc, assoc_desc->bFirstInterface);
	intf_b = find_idesc_by_id(cdesc, assoc_desc->bFirstInterface + 1);

	if (is_data_interface(cdesc->interface[intf_a].altsetting + 0)
	    && is_control_interface(cdesc->interface[intf_b].altsetting + 0)) {
		th->data_interface = intf_a;
		th->data_interface_id = assoc_desc->bFirstInterface;
		th->control_interface = intf_b;
		th->control_interface_id = assoc_desc->bFirstInterface + 1;
	} else if (is_control_interface(cdesc->interface[intf_a].altsetting + 0)
		   && is_data_interface(cdesc->interface[intf_b].altsetting + 0)) {
		th->data_interface = intf_b;
		th->data_interface_id = assoc_desc->bFirstInterface + 1;
		th->control_interface = intf_a;
		th->control_interface_id = assoc_desc->bFirstInterface;
	} else {
		return -ENODEV;
	}

	return 0;
}

static int find_interfaces(struct libusb_config_descriptor *cdesc,
			   struct thor_device_handle *th)
{
	struct usb_descriptor_header *header;
	struct usb_interface_assoc_descriptor *assoc_desc = NULL;
	int assoc_valid = 0;
	int pos;
	int ret;

	/* Try to find IAD and use it */
	pos = 0;
	for (; pos < cdesc->extra_length; pos += header->bLength) {
		header = (struct usb_descriptor_header *)(cdesc->extra + pos);
		if (header->bDescriptorType != USB_DT_INTERFACE_ASSOCIATION)
			continue;

		if (pos + sizeof(assoc_desc) > cdesc->extra_length)
			break;

		assoc_desc = (struct usb_interface_assoc_descriptor *)header;
		ret = check_assoc(cdesc, assoc_desc, th);
		if (!ret) {
			assoc_valid = 1;
			break;
		}
	}

	/*
	 * If we were unable to find IAD let's
	 * just try to manually find interfaces
	 */
	if (!assoc_valid) {
		int i;
#define get_intf_desc(_intf) (&(cdesc->interface[_intf].altsetting[0]))
		th->data_interface = -1;
		th->control_interface = -1;

		for (i = 0; i < cdesc->bNumInterfaces; ++i) {
			if (!is_data_interface(get_intf_desc(i)))
				continue;

			th->data_interface = i;
			th->data_interface_id =
				get_intf_desc(i)->bInterfaceNumber;
			break;
		}

		if (th->data_interface < 0)
			return -ENODEV;

		for (i = 0; i < cdesc->bNumInterfaces; ++i) {
			if (!is_control_interface(get_intf_desc(i)))
				continue;
			th->control_interface = i;
			th->control_interface_id =
				get_intf_desc(i)->bInterfaceNumber;
		}

		if (th->control_interface < 0)
			return -ENODEV;
#undef get_intf_desc
	}

	return 0;
}

static int find_data_eps(struct libusb_config_descriptor *cdesc,
			 struct thor_device_handle *th)
{
	const struct libusb_interface_descriptor *idesc;
	int i;

	idesc = cdesc->interface[th->data_interface_id].altsetting + 0;

	if (idesc->bNumEndpoints != 2)
		return -EINVAL;

	th->data_ep_in = -1;
	th->data_ep_out = -1;

	for (i = 0; i < idesc->bNumEndpoints; ++i) {
		if ((idesc->endpoint[i].bmAttributes & 0x03) !=
		    LIBUSB_TRANSFER_TYPE_BULK)
			return -1;
		if ((idesc->endpoint[i].bEndpointAddress & (1 << 7))
		    == LIBUSB_ENDPOINT_IN)
			th->data_ep_in = idesc->endpoint[i].bEndpointAddress;
		else
			th->data_ep_out = idesc->endpoint[i].bEndpointAddress;
	}

	if (th->data_ep_in < 0 || th->data_ep_out < 0)
		return -EINVAL;

	return 0;
}

static int find_intf_and_eps(libusb_device *dev,
			     struct thor_device_handle *th)
{
	struct libusb_config_descriptor *cdesc;
	int ret;

	ret = libusb_get_active_config_descriptor(dev, &cdesc);
	if (ret < 0)
		return ret;

	ret = find_interfaces(cdesc, th);
	if (ret) {
		ret = -ENODEV;
		goto cleanup_desc;
	}

	ret = find_data_eps(cdesc, th);
	if (ret) {
		ret = -ENODEV;
		goto cleanup_desc;
	}

	ret = 0;
cleanup_desc:
	libusb_free_config_descriptor(cdesc);
	return ret;
}

static int claim_intf(struct thor_device_handle *th)
{
	int ret;

	/*
	 * Check if our OS allows us to detach kernel driver.
	 * If yes then we mark this device as auto-detach and try to claim
	 * our interfaces. libusb will detach kernel driver, if any when we
	 * will try to claim interface.
	 * If our os doesn't support detaching kernel driver we simply try
	 * to claim our interfaces. If we fail it means that probably there
	 * is some kernel driver bound to this device but we cannot do anything
	 * with this.
	 */
	ret = libusb_has_capability(LIBUSB_CAP_SUPPORTS_DETACH_KERNEL_DRIVER);
	if (ret) {
		ret = libusb_set_auto_detach_kernel_driver(th->devh, 1);
		if (ret < 0)
			goto out;
	}

	ret = libusb_claim_interface(th->devh, th->data_interface_id);
	if (ret < 0)
		goto out;

	ret = libusb_claim_interface(th->devh, th->control_interface_id);
	if (ret < 0)
		goto release_data;

	return 0;

release_data:
	libusb_release_interface(th->devh, th->data_interface);
out:
	return ret;
}

static int check_device_match(struct thor_device_id *dev_id,
		       libusb_device *dev, struct thor_device_handle *th)
{
	int ret;

	if (dev_id->busid) {
		ret = check_busid_match(dev_id->busid, dev);
		if (ret <= 0)
			goto no_match;
	}

	if (dev_id->vid >= 0 || dev_id->pid >= 0) {
		ret = check_vid_pid_match(dev_id->vid, dev_id->pid, dev);
		if (ret <= 0)
			goto no_match;
	}

	if (dev_id->serial) {
		ret = check_serial_match(dev_id->serial, dev, &th->devh);
		if (ret <= 0)
			goto no_match;
	} else {
		ret = libusb_open(dev, &th->devh);
		if (ret < 0)
			goto no_match;
	}

	ret = find_intf_and_eps(dev, th);
	if (ret < 0)
		goto err;

	ret = claim_intf(th);
	if (ret < 0)
		goto err;

	return 1;
err:
	libusb_close(th->devh);
no_match:
	return 0;
}

static int find_existing_device(struct thor_device_id *dev_id,
			    struct thor_device_handle *th)
{
	libusb_device **dev_list;
	int i, ndevices;
	int ret = 0;

	ndevices = libusb_get_device_list(NULL, &dev_list);
	if (ndevices < 0)
		return ndevices;

	for (i = 0; i < ndevices; ++i) {
		ret = check_device_match(dev_id, dev_list[i], th);
		if (ret > 0)
			/* device match and opened */
			break;
	}

	libusb_free_device_list(dev_list, 1);

	return ret > 0 ? 1 : 0;

}

static int hotplug_device_arrived(libusb_context *ctx, libusb_device *device,
		   libusb_hotplug_event event, void *user_data)
{
	struct hotplug_helper *helper = user_data;

	if (check_device_match(helper->dev_id, device, helper->th) > 0) {
		helper->completed = 1;
		return 1;
	}

	return 0;
}


int t_usb_find_device(struct thor_device_id *dev_id, int wait,
		      struct thor_device_handle *th)
{
	struct hotplug_helper helper = {
		.th = th,
		.dev_id = dev_id,
		.completed = 0,
	};
	int found;

	found = find_existing_device(dev_id, th);
	if (found <= 0) {
		if (!wait)
			return found;

		libusb_hotplug_register_callback(NULL,
						 LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED,
						 0,
						 dev_id->vid >= 0 ? dev_id->vid
						 : LIBUSB_HOTPLUG_MATCH_ANY,
						 dev_id->pid >= 0 ? dev_id->pid
						 : LIBUSB_HOTPLUG_MATCH_ANY,
						 LIBUSB_HOTPLUG_MATCH_ANY,
						 hotplug_device_arrived,
						 &helper,
						 NULL);

		while (!helper.completed)
			libusb_handle_events_completed(NULL, &helper.completed);
	}

	return 1;
}

void t_usb_close_device(struct thor_device_handle *th)
{
	if (th->devh)
		libusb_close(th->devh);
}

int t_usb_send(struct thor_device_handle *th, unsigned char *buf,
	       size_t count, int timeout)
{
	int ret;
	int transferred = 0;

	ret = libusb_bulk_transfer(th->devh,
				   th->data_ep_out,
				   (unsigned char *)buf,
				   count,
				   &transferred,
				   timeout);

	if (ret < 0)
		return ret;
	if (transferred < count)
		return -EIO;

	return 0;
}

int t_usb_recv(struct thor_device_handle *th, unsigned char *buf,
	       size_t count, int timeout)
{
	int ret;
	int transferred = 0;

	ret = libusb_bulk_transfer(th->devh,
				   th->data_ep_in,
				   (unsigned char *)buf,
				   count,
				   &transferred,
				   timeout);

	if (ret < 0)
		return ret;
	if (transferred < count)
		return -EIO;

	return 0;
}

int t_usb_send_req(struct thor_device_handle *th, request_type req_id,
		   int req_sub_id, int *idata, int icnt, char **sdata, int scnt)
{
	struct rqt_pkt req;
	int i;
	int ret;

	assert(icnt <= sizeof(req.int_data)/sizeof(req.int_data[0]));
	assert(icnt >= 0);
	assert(scnt <= sizeof(req.str_data)/sizeof(req.str_data[0]));
	assert(scnt >= 0);

	memset(&req, 0, sizeof(req));

	req.id = req_id;
	req.sub_id = req_sub_id;

	if (idata) {
		for (i = 0; i < icnt; i++)
			req.int_data[i] = idata[i];
	}

	if (sdata) {
		for (i = 0; i < scnt; i++)
			strcpy(req.str_data[i],sdata[i]);
	}

	ret = t_usb_send(th, (unsigned char *)&req, RQT_PKT_SIZE, DEFAULT_TIMEOUT);

	return ret;
}

int t_usb_recv_req(struct thor_device_handle *th, struct res_pkt *resp)
{
	int ret;

	ret = t_usb_recv(th, (unsigned char *)resp, sizeof(*resp),
			 DEFAULT_TIMEOUT);

	return ret;
}

