// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * Author: Manivannan Sadhasivam <manivannan.sadhasivam@oss.qualcomm.com>
 */

#include <linux/cleanup.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/of_platform.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/pwrseq/provider.h>
#include <linux/regulator/consumer.h>
#include <linux/serdev.h>
#include <linux/slab.h>
#include <linux/usb.h>

struct pwrseq_pcie_m2_pdata {
	const struct pwrseq_target_data **targets;
};

struct pwrseq_pcie_m2_ctx {
	struct pwrseq_device *pwrseq;
	struct device_node *of_node;
	const struct pwrseq_pcie_m2_pdata *pdata;
	struct regulator_bulk_data *regs;
	size_t num_vregs;
	struct notifier_block nb;
	struct notifier_block usb_nb;
	struct gpio_desc *w_disable1_gpio;
	struct gpio_desc *w_disable2_gpio;
	unsigned int w_disable2_refcnt;
	struct mutex w_disable2_lock;
	struct mutex serdev_lock;
	bool usb_bt_seen;
	struct serdev_device *serdev;
	struct of_changeset *ocs;
	struct device *dev;
};

static int pwrseq_pcie_m2_vregs_enable(struct pwrseq_device *pwrseq)
{
	struct pwrseq_pcie_m2_ctx *ctx = pwrseq_device_get_drvdata(pwrseq);

	return regulator_bulk_enable(ctx->num_vregs, ctx->regs);
}

static int pwrseq_pcie_m2_vregs_disable(struct pwrseq_device *pwrseq)
{
	struct pwrseq_pcie_m2_ctx *ctx = pwrseq_device_get_drvdata(pwrseq);

	return regulator_bulk_disable(ctx->num_vregs, ctx->regs);
}

static const struct pwrseq_unit_data pwrseq_pcie_m2_vregs_unit_data = {
	.name = "regulators-enable",
	.enable = pwrseq_pcie_m2_vregs_enable,
	.disable = pwrseq_pcie_m2_vregs_disable,
};

static const struct pwrseq_unit_data *pwrseq_pcie_m2_unit_deps[] = {
	&pwrseq_pcie_m2_vregs_unit_data,
	NULL
};

static int pwrseq_pci_m2_e_bt_enable(struct pwrseq_device *pwrseq)
{
	struct pwrseq_pcie_m2_ctx *ctx = pwrseq_device_get_drvdata(pwrseq);

	guard(mutex)(&ctx->w_disable2_lock);

	if (ctx->w_disable2_refcnt++)
		return 0;

	return gpiod_set_value_cansleep(ctx->w_disable2_gpio, 0);
}

static int pwrseq_pci_m2_e_bt_disable(struct pwrseq_device *pwrseq)
{
	struct pwrseq_pcie_m2_ctx *ctx = pwrseq_device_get_drvdata(pwrseq);
	int ret;

	guard(mutex)(&ctx->w_disable2_lock);

	if (WARN_ON(!ctx->w_disable2_refcnt))
		return 0;

	if (--ctx->w_disable2_refcnt)
		return 0;

	ret = gpiod_set_value_cansleep(ctx->w_disable2_gpio, 1);
	if (!ret)
		msleep(100);

	return ret;
}

/*
 * XXX There are two Bluetooth units to allow either one to be able to power
 * off and thus reset the controller. In practice only one of the interfaces
 * is used, so there is no conflict. However userspace could power off the
 * USB unit by disabling the associated USB port, without the UART unit or
 * its consumer ever knowing.
 */
static const struct pwrseq_unit_data pwrseq_pcie_m2_e_bt_uart_unit_data = {
	.name = "bt-uart-enable",
	.deps = pwrseq_pcie_m2_unit_deps,
	.enable = pwrseq_pci_m2_e_bt_enable,
	.disable = pwrseq_pci_m2_e_bt_disable,
};

static const struct pwrseq_unit_data pwrseq_pcie_m2_e_bt_usb_unit_data = {
	.name = "bt-usb-enable",
	.deps = pwrseq_pcie_m2_unit_deps,
	.enable = pwrseq_pci_m2_e_bt_enable,
	.disable = pwrseq_pci_m2_e_bt_disable,
};

static int pwrseq_pci_m2_e_wifi_enable(struct pwrseq_device *pwrseq)
{
	struct pwrseq_pcie_m2_ctx *ctx = pwrseq_device_get_drvdata(pwrseq);

	return gpiod_set_value_cansleep(ctx->w_disable1_gpio, 0);
}

static int pwrseq_pci_m2_e_wifi_disable(struct pwrseq_device *pwrseq)
{
	struct pwrseq_pcie_m2_ctx *ctx = pwrseq_device_get_drvdata(pwrseq);

	return gpiod_set_value_cansleep(ctx->w_disable1_gpio, 1);
}

static const struct pwrseq_unit_data pwrseq_pcie_m2_e_wifi_unit_data = {
	.name = "wifi-enable",
	.deps = pwrseq_pcie_m2_unit_deps,
	.enable = pwrseq_pci_m2_e_wifi_enable,
	.disable = pwrseq_pci_m2_e_wifi_disable,
};

static const struct pwrseq_unit_data pwrseq_pcie_m2_m_pcie_unit_data = {
	.name = "pcie-enable",
	.deps = pwrseq_pcie_m2_unit_deps,
};

static int pwrseq_pcie_m2_e_pwup_delay(struct pwrseq_device *pwrseq)
{
	/*
	 * FIXME: This delay is only required for some Qcom WLAN/BT cards like
	 * WCN7850 and not for all devices. But currently, there is no way to
	 * identify the device model before enumeration.
	 */
	msleep(50);

	return 0;
}

static const struct pwrseq_target_data pwrseq_pcie_m2_e_uart_target_data = {
	.name = "uart",
	.unit = &pwrseq_pcie_m2_e_bt_uart_unit_data,
	.post_enable = pwrseq_pcie_m2_e_pwup_delay,
};

static const struct pwrseq_target_data pwrseq_pcie_m2_e_usb_target_data = {
	.name = "usb",
	.unit = &pwrseq_pcie_m2_e_bt_usb_unit_data,
};

static const struct pwrseq_target_data pwrseq_pcie_m2_e_pcie_target_data = {
	.name = "pcie",
	.unit = &pwrseq_pcie_m2_e_wifi_unit_data,
	.post_enable = pwrseq_pcie_m2_e_pwup_delay,
};

static const struct pwrseq_target_data pwrseq_pcie_m2_e_sdio_target_data = {
	.name = "sdio",
	.unit = &pwrseq_pcie_m2_e_wifi_unit_data,
	.post_enable = pwrseq_pcie_m2_e_pwup_delay,
};

static const struct pwrseq_target_data pwrseq_pcie_m2_m_pcie_target_data = {
	.name = "pcie",
	.unit = &pwrseq_pcie_m2_m_pcie_unit_data,
};

static const struct pwrseq_target_data *pwrseq_pcie_m2_e_targets[] = {
	&pwrseq_pcie_m2_e_pcie_target_data,
	&pwrseq_pcie_m2_e_sdio_target_data,
	&pwrseq_pcie_m2_e_uart_target_data,
	&pwrseq_pcie_m2_e_usb_target_data,
	NULL
};

static const struct pwrseq_target_data *pwrseq_pcie_m2_m_targets[] = {
	&pwrseq_pcie_m2_m_pcie_target_data,
	NULL
};

static const struct pwrseq_pcie_m2_pdata pwrseq_pcie_m2_e_of_data = {
	.targets = pwrseq_pcie_m2_e_targets,
};

static const struct pwrseq_pcie_m2_pdata pwrseq_pcie_m2_m_of_data = {
	.targets = pwrseq_pcie_m2_m_targets,
};

static int pwrseq_pcie_m2_match(struct pwrseq_device *pwrseq,
				 struct device *dev)
{
	struct pwrseq_pcie_m2_ctx *ctx = pwrseq_device_get_drvdata(pwrseq);
	struct device_node *endpoint __free(device_node) = NULL;

	/*
	 * Traverse the 'remote-endpoint' nodes and check if the remote node's
	 * parent matches the OF node of 'dev'.
	 */
	for_each_endpoint_of_node(ctx->of_node, endpoint) {
		/* USB port devices are tied to the port nodes. */
		struct device_node *remote_port __free(device_node) =
				of_graph_get_remote_port(endpoint);
		if (device_match_of_node(dev, remote_port))
			return PWRSEQ_MATCH_OK;

		/* Try the remote port parent for other types. */
		struct device_node *remote __free(device_node) =
				of_graph_get_remote_port_parent(endpoint);
		if (device_match_of_node(dev, remote))
			return PWRSEQ_MATCH_OK;
	}

	return PWRSEQ_NO_MATCH;
}

static int pwrseq_m2_pcie_create_bt_node(struct pwrseq_pcie_m2_ctx *ctx,
					struct device_node *parent,
					const char *compatible)
{
	struct device *dev = ctx->dev;
	struct device_node *np;
	int ret;

	ctx->ocs = kzalloc_obj(*ctx->ocs);
	if (!ctx->ocs)
		return -ENOMEM;

	of_changeset_init(ctx->ocs);

	np = of_changeset_create_node(ctx->ocs, parent, "bluetooth");
	if (!np) {
		dev_err(dev, "Failed to create bluetooth node\n");
		ret = -ENODEV;
		goto err_destroy_changeset;
	}

	ret = of_changeset_add_prop_string(ctx->ocs, np, "compatible", compatible);
	if (ret) {
		dev_err(dev, "Failed to add bluetooth compatible: %d\n", ret);
		goto err_destroy_changeset;
	}

	ret = of_changeset_apply(ctx->ocs);
	if (ret) {
		dev_err(dev, "Failed to apply changeset: %d\n", ret);
		goto err_destroy_changeset;
	}

	ret = device_add_of_node(&ctx->serdev->dev, np);
	if (ret) {
		dev_err(dev, "Failed to add OF node: %d\n", ret);
		goto err_revert_changeset;
	}

	return 0;

err_revert_changeset:
	of_changeset_revert(ctx->ocs);
err_destroy_changeset:
	of_changeset_destroy(ctx->ocs);
	kfree(ctx->ocs);
	ctx->ocs = NULL;

	return ret;
}

#if IS_ENABLED(CONFIG_USB)
static bool pwrseq_pcie_m2_is_bt_iface(struct usb_device *udev)
{
	struct usb_host_config *conf = udev->actconfig;
	unsigned int i;

	if (udev->descriptor.bDeviceClass == USB_CLASS_WIRELESS_CONTROLLER &&
	    udev->descriptor.bDeviceSubClass == 0x01 &&
	    udev->descriptor.bDeviceProtocol == 0x01)
		return true;

	if (!conf)
		return false;

	for (i = 0; i < conf->desc.bNumInterfaces; i++) {
		struct usb_interface *intf = conf->interface[i];
		struct usb_host_interface *alt;

		if (!intf)
			continue;

		alt = intf->cur_altsetting;
		if (alt &&
		    alt->desc.bInterfaceClass == USB_CLASS_WIRELESS_CONTROLLER &&
		    alt->desc.bInterfaceSubClass == 0x01 &&
		    alt->desc.bInterfaceProtocol == 0x01)
			return true;
	}

	return false;
}

struct pwrseq_pcie_m2_usb_match {
	struct device_node *hub;
	u32 port;
};

static int pwrseq_pcie_m2_match_usb_bt(struct usb_device *udev, void *data)
{
	struct pwrseq_pcie_m2_usb_match *match = data;

	if (!udev->parent || dev_of_node(&udev->parent->dev) != match->hub)
		return 0;

	if (udev->portnum != match->port)
		return 0;

	return pwrseq_pcie_m2_is_bt_iface(udev);
}

static bool pwrseq_pcie_m2_has_usb_bt(struct pwrseq_pcie_m2_ctx *ctx)
{
	struct pwrseq_pcie_m2_usb_match match;

	if (ctx->usb_bt_seen)
		return true;

	struct device_node *ep __free(device_node) =
			of_graph_get_endpoint_by_regs(ctx->of_node, 2, 0);
	if (!ep)
		return false;

	struct device_node *usb_port __free(device_node) =
			of_graph_get_remote_port(ep);
	if (!usb_port)
		return false;

	if (of_property_read_u32(usb_port, "reg", &match.port))
		return false;

	struct device_node *ports __free(device_node) =
			of_get_parent(usb_port);
	if (!ports)
		return false;

	struct device_node *hub __free(device_node) = of_get_parent(ports);
	if (!hub)
		return false;

	match.hub = hub;

	return usb_for_each_dev(&match, pwrseq_pcie_m2_match_usb_bt) != 0;
}
#else
static bool pwrseq_pcie_m2_has_usb_bt(struct pwrseq_pcie_m2_ctx *ctx)
{
	return ctx->usb_bt_seen;
}
#endif

static int pwrseq_pcie_m2_create_serdev(struct pwrseq_pcie_m2_ctx *ctx,
					const char *compatible)
{
	struct serdev_controller *serdev_ctrl;
	struct device *dev = ctx->dev;
	int ret;
	guard(mutex)(&ctx->serdev_lock);

	struct device_node *serdev_parent __free(device_node) =
		of_graph_get_remote_node(dev_of_node(ctx->dev), 3, 0);
	if (!serdev_parent)
		return 0;

	if (pwrseq_pcie_m2_has_usb_bt(ctx)) {
		ctx->usb_bt_seen = true;
		dev_dbg(dev,
			"Bluetooth is attached over USB, skipping UART serdev\n");
		return 0;
	}

	serdev_ctrl = of_find_serdev_controller_by_node(serdev_parent);
	if (!serdev_ctrl)
		return 0;

	/* Bail out if the device was already attached to this controller */
	if (serdev_ctrl->serdev) {
		serdev_controller_put(serdev_ctrl);
		return 0;
	}

	ctx->serdev = serdev_device_alloc(serdev_ctrl);
	if (!ctx->serdev) {
		ret = -ENOMEM;
		goto err_put_ctrl;
	}

	ret = pwrseq_m2_pcie_create_bt_node(ctx, serdev_parent, compatible);
	if (ret)
		goto err_free_serdev;

	ret = serdev_device_add(ctx->serdev);
	if (ret) {
		dev_err(dev, "Failed to add serdev for bluetooth: %d\n", ret);
		goto err_free_dt_node;
	}

	serdev_controller_put(serdev_ctrl);

	return 0;

err_free_dt_node:
	device_remove_of_node(&ctx->serdev->dev);
	of_changeset_revert(ctx->ocs);
	of_changeset_destroy(ctx->ocs);
	kfree(ctx->ocs);
	ctx->ocs = NULL;
err_free_serdev:
	serdev_device_put(ctx->serdev);
	ctx->serdev = NULL;
err_put_ctrl:
	serdev_controller_put(serdev_ctrl);

	return ret;
}

static void pwrseq_pcie_m2_remove_serdev_locked(struct pwrseq_pcie_m2_ctx *ctx)
{
	if (ctx->serdev) {
		device_remove_of_node(&ctx->serdev->dev);
		serdev_device_remove(ctx->serdev);
		ctx->serdev = NULL;
	}

	if (ctx->ocs) {
		of_changeset_revert(ctx->ocs);
		of_changeset_destroy(ctx->ocs);
		kfree(ctx->ocs);
		ctx->ocs = NULL;
	}
}

static void pwrseq_pcie_m2_remove_serdev(struct pwrseq_pcie_m2_ctx *ctx)
{
	guard(mutex)(&ctx->serdev_lock);

	pwrseq_pcie_m2_remove_serdev_locked(ctx);
}

#if IS_ENABLED(CONFIG_USB)
static int pwrseq_pcie_m2_usb_notify(struct notifier_block *nb,
				     unsigned long action, void *data)
{
	struct pwrseq_pcie_m2_ctx *ctx =
		container_of(nb, struct pwrseq_pcie_m2_ctx, usb_nb);
	struct usb_device *udev = data;
	struct pwrseq_pcie_m2_usb_match match;

	if (action != USB_DEVICE_ADD)
		return NOTIFY_DONE;

	struct device_node *ep __free(device_node) =
			of_graph_get_endpoint_by_regs(ctx->of_node, 2, 0);
	if (!ep)
		return NOTIFY_DONE;

	struct device_node *usb_port __free(device_node) =
			of_graph_get_remote_port(ep);
	if (!usb_port || of_property_read_u32(usb_port, "reg", &match.port))
		return NOTIFY_DONE;

	struct device_node *ports __free(device_node) = of_get_parent(usb_port);
	if (!ports)
		return NOTIFY_DONE;

	struct device_node *hub __free(device_node) = of_get_parent(ports);
	if (!hub)
		return NOTIFY_DONE;

	match.hub = hub;
	if (!pwrseq_pcie_m2_match_usb_bt(udev, &match))
		return NOTIFY_DONE;

	guard(mutex)(&ctx->serdev_lock);

	ctx->usb_bt_seen = true;
	if (ctx->serdev) {
		dev_info(ctx->dev,
			 "USB Bluetooth detected, removing UART Bluetooth\n");
		pwrseq_pcie_m2_remove_serdev_locked(ctx);
	}

	return NOTIFY_OK;
}
#endif

static const struct pci_device_id pcie_m2_serdev_ids[] = {
	{ /* QCNFA765A with QCA2066 */
		PCI_VDEVICE_SUB(QCOM, 0x1103, PCI_VENDOR_ID_QCOM, 0x3374),
		.driver_data = (kernel_ulong_t)"qcom,qca2066-bt",
	},
	{ /* QCNFA765A */
		PCI_VDEVICE(QCOM, 0x1103),
		.driver_data = (kernel_ulong_t)"qcom,wcn6855-bt",
	},
	{ /* QCNCM865A */
		PCI_VDEVICE(QCOM, 0x1107),
		.driver_data = (kernel_ulong_t)"qcom,wcn8750-bt",
	},
	{}
};

static int pwrseq_m2_pcie_notify(struct notifier_block *nb, unsigned long action,
			      void *data)
{
	struct pwrseq_pcie_m2_ctx *ctx = container_of(nb, struct pwrseq_pcie_m2_ctx, nb);
	struct pci_dev *pdev = to_pci_dev(data);
	const struct pci_device_id * id;
	const char *compatible;
	int ret;

	/*
	 * Check whether the PCI device is associated with this M.2 connector or
	 * not, by comparing the OF node of the PCI device parent and the Port 0
	 * (PCIe) remote node parent OF node.
	 */
	struct device_node *pci_parent __free(device_node) =
			of_graph_get_remote_node(dev_of_node(ctx->dev), 0, 0);
	if (!pci_parent || (pci_parent != pdev->dev.parent->of_node))
		return NOTIFY_DONE;

	switch (action) {
	case BUS_NOTIFY_BOUND_DRIVER:
		/* Create serdev device for matched devices */
		if ((id = pci_match_id(pcie_m2_serdev_ids, pdev))) {
			compatible = (const char*)id->driver_data;
			ret = pwrseq_pcie_m2_create_serdev(ctx, compatible);
			if (ret)
				return notifier_from_errno(ret);
		}
		break;
	case BUS_NOTIFY_UNBIND_DRIVER:
		/* Destroy serdev device for matched devices */
		if (pci_match_id(pcie_m2_serdev_ids, pdev))
			pwrseq_pcie_m2_remove_serdev(ctx);

		break;
	case BUS_NOTIFY_REMOVED_DEVICE:
		if (pci_match_id(pcie_m2_serdev_ids, pdev))
			ctx->usb_bt_seen = false;

		break;
	}

	return NOTIFY_OK;
}

static bool pwrseq_pcie_m2_check_remote_node(struct device *dev, u8 port, u8 endpoint,
					     const char *node)
{
	struct device_node *remote __free(device_node) =
			of_graph_get_remote_node(dev_of_node(dev), port, endpoint);

	if (remote && of_node_name_eq(remote, node))
		return true;

	return false;
}

/*
 * If the connector exposes a non-discoverable bus like UART, the respective
 * protocol device needs to be created manually with the help of the notifier
 * of the discoverable bus like PCIe.
 */
static int pwrseq_pcie_m2_register_notifier(struct pwrseq_pcie_m2_ctx *ctx, struct device *dev)
{
	int ret;

	/*
	 * Register a PCI notifier for Key E connector that has PCIe as Port
	 * 0/Endpoint 0 interface and Serial as Port 3/Endpoint 0 interface.
	 */
	if (pwrseq_pcie_m2_check_remote_node(dev, 3, 0, "serial")) {
		if (pwrseq_pcie_m2_check_remote_node(dev, 0, 0, "pcie")) {
			ctx->dev = dev;
			ctx->nb.notifier_call = pwrseq_m2_pcie_notify;
			ret = bus_register_notifier(&pci_bus_type, &ctx->nb);
			if (ret)
				return dev_err_probe(dev, ret,
						     "Failed to register notifier for serdev\n");
		}
	}

	return 0;
}

static int pwrseq_pcie_m2_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct pwrseq_pcie_m2_ctx *ctx;
	struct pwrseq_config config = {};
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	platform_set_drvdata(pdev, ctx);
	ctx->of_node = dev_of_node(dev);
	ctx->pdata = device_get_match_data(dev);
	if (!ctx->pdata)
		return dev_err_probe(dev, -ENODEV,
				     "Failed to obtain platform data\n");

	ret = devm_mutex_init(dev, &ctx->w_disable2_lock);
	if (ret)
		return ret;

	ret = devm_mutex_init(dev, &ctx->serdev_lock);
	if (ret)
		return ret;

	/*
	 * Currently, of_regulator_bulk_get_all() is the only regulator API that
	 * allows to get all supplies in the devicetree node without manually
	 * specifying them.
	 */
	ret = of_regulator_bulk_get_all(dev, dev_of_node(dev), &ctx->regs);
	if (ret < 0)
		return dev_err_probe(dev, ret,
				     "Failed to get all regulators\n");

	ctx->num_vregs = ret;

	ctx->w_disable1_gpio = devm_gpiod_get_optional(dev, "w-disable1", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->w_disable1_gpio)) {
		ret = dev_err_probe(dev, PTR_ERR(ctx->w_disable1_gpio),
				     "Failed to get the W_DISABLE_1# GPIO\n");
		goto err_free_regulators;
	}

	ctx->w_disable2_gpio = devm_gpiod_get_optional(dev, "w-disable2", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->w_disable2_gpio)) {
		ret = dev_err_probe(dev, PTR_ERR(ctx->w_disable2_gpio),
				     "Failed to get the W_DISABLE_2# GPIO\n");
		goto err_free_regulators;
	}

	config.parent = dev;
	config.owner = THIS_MODULE;
	config.drvdata = ctx;
	config.match = pwrseq_pcie_m2_match;
	config.targets = ctx->pdata->targets;

	ctx->pwrseq = devm_pwrseq_device_register(dev, &config);
	if (IS_ERR(ctx->pwrseq)) {
		ret = dev_err_probe(dev, PTR_ERR(ctx->pwrseq),
				     "Failed to register the power sequencer\n");
		goto err_free_regulators;
	}

	/*
	 * Register a notifier for creating protocol devices for
	 * non-discoverable busses like UART.
	 */
	ret = pwrseq_pcie_m2_register_notifier(ctx, dev);
	if (ret)
		goto err_free_regulators;

#if IS_ENABLED(CONFIG_USB)
	ctx->usb_nb.notifier_call = pwrseq_pcie_m2_usb_notify;
	usb_register_notify(&ctx->usb_nb);
#endif

	return 0;

err_free_regulators:
	regulator_bulk_free(ctx->num_vregs, ctx->regs);

	return ret;
}

static void pwrseq_pcie_m2_remove(struct platform_device *pdev)
{
	struct pwrseq_pcie_m2_ctx *ctx = platform_get_drvdata(pdev);

#if IS_ENABLED(CONFIG_USB)
	usb_unregister_notify(&ctx->usb_nb);
#endif
	bus_unregister_notifier(&pci_bus_type, &ctx->nb);
	pwrseq_pcie_m2_remove_serdev(ctx);

	regulator_bulk_free(ctx->num_vregs, ctx->regs);
}

static const struct of_device_id pwrseq_pcie_m2_of_match[] = {
	{
		.compatible = "pcie-m2-m-connector",
		.data = &pwrseq_pcie_m2_m_of_data,
	},
	{
		.compatible = "pcie-m2-e-connector",
		.data = &pwrseq_pcie_m2_e_of_data,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, pwrseq_pcie_m2_of_match);

static struct platform_driver pwrseq_pcie_m2_driver = {
	.driver = {
		.name = "pwrseq-pcie-m2",
		.of_match_table = pwrseq_pcie_m2_of_match,
	},
	.probe = pwrseq_pcie_m2_probe,
	.remove = pwrseq_pcie_m2_remove,
};
module_platform_driver(pwrseq_pcie_m2_driver);

MODULE_AUTHOR("Manivannan Sadhasivam <manivannan.sadhasivam@oss.qualcomm.com>");
MODULE_DESCRIPTION("Power Sequencing driver for PCIe M.2 connector");
MODULE_LICENSE("GPL");
