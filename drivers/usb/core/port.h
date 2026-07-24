/* SPDX-License-Identifier: GPL-2.0 */
/*
 * usb hub driver head file
 *
 * Copyright (C) 1999 Linus Torvalds
 * Copyright (C) 1999 Johannes Erdfelt
 * Copyright (C) 1999 Gregory P. Smith
 * Copyright (C) 2001 Brad Hards (bhards@bigpond.net.au)
 * Copyright (C) 2012 Intel Corp (tianyu.lan@intel.com)
 *
 *  move struct usb_port to this file.
 */

#include <linux/container_of.h>
#include <linux/device.h>
#include <linux/mutex_types.h>
#include <linux/usb.h>

#include <uapi/linux/usb/ch9.h>

/**
 * struct usb port - kernel's representation of a usb port
 * @child: usb device attached to the port
 * @dev: generic device interface
 * @port_owner: port's owner
 * @peer: related usb2 and usb3 ports (share the same connector)
 * @connector: USB Type-C connector
 * @pwrseq: power sequencing descriptor for the port
 * @req: default pm qos request for hubs without port power control
 * @connect_type: port's connect type
 * @state: device state of the usb device attached to the port
 * @state_kn: kernfs_node of the sysfs attribute that accesses @state
 * @location: opaque representation of platform connector location
 * @status_lock: synchronize port_event() vs usb_port_{suspend|resume}
 * @portnum: port index num based one
 * @is_superspeed cache super-speed status
 * @usb3_lpm_u1_permit: whether USB3 U1 LPM is permitted.
 * @usb3_lpm_u2_permit: whether USB3 U2 LPM is permitted.
 * @early_stop: whether port initialization will be stopped earlier.
 * @ignore_event: whether events of the port are ignored.
 */
struct usb_port {
	struct usb_device *child;
	struct device dev;
	struct usb_dev_state *port_owner;
	struct usb_port *peer;
	struct typec_connector *connector;
	struct pwrseq_desc *pwrseq;
	struct dev_pm_qos_request *req;
	enum usb_port_connect_type connect_type;
	enum usb_device_state state;
	struct kernfs_node *state_kn;
	usb_port_location_t location;
	struct mutex status_lock;
	u32 over_current_count;
	u8 portnum;
	u32 quirks;
	unsigned int early_stop:1;
	unsigned int ignore_event:1;
	unsigned int is_superspeed:1;
	unsigned int usb3_lpm_u1_permit:1;
	unsigned int usb3_lpm_u2_permit:1;
};

#define to_usb_port(_dev) \
	container_of(_dev, struct usb_port, dev)

int usb_port_is_power_on(struct usb_port *port, unsigned int portstatus);
