/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Driver for HTR32xx family of I2C LED controllers
 *
 * Copyright (c) 2025 Radxa Computer (Shenzhen) Co., Ltd.
 *
 * Based on drivers/leds/leds-is31fl32xx.c:
 * Copyright (c) 2015 Allworx Corp.
 * David Rivshin <drivshin@allworx.com>
 *
 * Steps:
 *  1. Software enable: write 0x00 = 0x01
 *  2. Select 22kHz PWM (opt): write 0x4B = 0x01
 *  3. Enable channels: set each enable register = 0x01
 *  4. Set PWM values: write each PWM reg
 *  5. Update: write 0x25 = 0x00
 *  6. Shutdown: write 0x00 = 0x00 (and 0x4F=0x00 to reset)
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/leds.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/delay.h>

#define HTR32XX_REG_NONE 0xFF

struct htr32xx_priv;
struct htr32xx_led {
	u8 channel;      /* 1-based channel number */
	struct led_classdev cdev;
	struct htr32xx_priv *priv;
	struct work_struct work; /* For asynchronous execution of I²C writes */
};

struct htr32xx_chipdef {
	u8 channels;
	u8 shutdown_reg;
	u8 pwm_update_reg;
	u8 global_ctrl_reg;
	u8 reset_reg;
	u8 pwm_base;
	bool pwm_reversed;
	u8 en_base;
	u8 en_regs;
	u8 en_per_reg;
};

struct htr32xx_priv {
	struct i2c_client *client;
	const struct htr32xx_chipdef *cdef;
	u8 num_leds;
	struct htr32xx_led leds[];
};

static int htr32xx_write(struct htr32xx_priv *priv, u8 reg, u8 val)
{
	int ret;
	ret = i2c_smbus_write_byte_data(priv->client, reg, val);
	if (ret < 0)
		dev_err(&priv->client->dev,
			"write reg 0x%02x failed: %d\n", reg, ret);
	return ret;
}

static int htr32xx_reset_regs(struct htr32xx_priv *priv)
{
	const struct htr32xx_chipdef *c = priv->cdef;
	int ret, i;

	/* reset-specific reg */
	if (c->reset_reg != HTR32XX_REG_NONE) {
		ret = htr32xx_write(priv, c->reset_reg, 0x00);
		if (ret)
			return ret;
		msleep(10);
	}

	/* software enable */
	ret = htr32xx_write(priv, c->shutdown_reg, 0x01);
	if (ret)
		return ret;
	msleep(10);

	/* optional PWM freq sel */
	if (c->global_ctrl_reg != HTR32XX_REG_NONE) {
		ret = htr32xx_write(priv, c->global_ctrl_reg, 0x01);
		if (ret)
			return ret;
		msleep(10);
	}

	/* enable all channels */
	for (i = 0; i < c->en_regs; i++) {
		ret = htr32xx_write(priv, c->en_base + i, GENMASK(c->en_per_reg - 1, 0));
		if (ret)
			return ret;
	}

	return htr32xx_write(priv, c->pwm_update_reg, 0x00);
}

static int htr32xx_set_brightness(struct led_classdev *cdev,
		   enum led_brightness brightness)
{
	struct htr32xx_led *led = container_of(cdev, struct htr32xx_led, cdev);
	cdev->brightness = brightness;
	schedule_work(&led->work);
	return 0;
}

/**
 * work handler: Execute the actual I2C write in a non-atomic context
 */
static void htr32xx_brightness_work(struct work_struct *w)
{
	struct htr32xx_led *led = container_of(w, struct htr32xx_led, work);
	struct htr32xx_priv *priv = led->priv;
	const struct htr32xx_chipdef *c = priv->cdef;
	u8 idx = led->channel - 1;
	htr32xx_write(priv, c->pwm_base + idx, led->cdev.brightness);
	htr32xx_write(priv, c->pwm_update_reg, 0x00);
}

static int htr32xx_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device_node *child;
	struct device_node *np = client->dev.of_node;
	struct htr32xx_priv *priv;
	const struct htr32xx_chipdef *cdef;
	int count, i, ret;

	cdef = of_device_get_match_data(&client->dev);
	if (!cdef)
		return -EINVAL;

	count = of_get_available_child_count(np);
	if (!count)
		return -EINVAL;

	priv = devm_kzalloc(&client->dev,
			sizeof(*priv) + count * sizeof(struct htr32xx_led),
			GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->client = client;
	priv->cdef = cdef;

	/* init regs */
	ret = htr32xx_reset_regs(priv);
	if (ret)
		return ret;

	/* register leds */
	i = 0;
	for_each_available_child_of_node(np, child) {
		struct led_init_data init_data = {};
		struct htr32xx_led *led = &priv->leds[i];
		struct led_classdev *cdev = &led->cdev;
		u32 channel;
		const char *label;

		of_property_read_u32(child, "reg", &channel);
		led->channel = channel;
		led->priv = priv;

		of_property_read_string(child, "label", &label);
		cdev->name = devm_kasprintf(&client->dev, GFP_KERNEL,
			"%s:%s", dev_name(&client->dev), label ?: "led");
		cdev->brightness_set = htr32xx_set_brightness;
		INIT_WORK(&led->work, htr32xx_brightness_work);
		cdev->max_brightness = 0xFF;

		init_data.fwnode = of_fwnode_handle(child);
		ret = devm_led_classdev_register_ext(&client->dev,
			cdev, &init_data);
		if (ret)
			return ret;
		i++;
	}

	priv->num_leds = i;
	i2c_set_clientdata(client, priv);

	return 0;
}

static int htr32xx_remove(struct i2c_client *client)
{
	struct htr32xx_priv *priv = i2c_get_clientdata(client);
	struct htr32xx_chipdef const *c = priv->cdef;

	/* shutdown */
	htr32xx_write(priv, c->shutdown_reg, 0x00);
	msleep(10);
	/* reset regs if needed */
	if (c->reset_reg != HTR32XX_REG_NONE)
		htr32xx_write(priv, c->reset_reg, 0x00);

	return 0;
}

static const struct htr32xx_chipdef htr3236_def = {
	.channels = 36,
	.shutdown_reg = 0x00,
	.pwm_update_reg = 0x25,
	.global_ctrl_reg = 0x4B,
	.reset_reg = 0x4F,
	.pwm_base = 0x01,
	.pwm_reversed = false,
	.en_base = 0x26,
	.en_regs = 36,
	.en_per_reg = 1,
};

static const struct htr32xx_chipdef htr3218_def = {
	.channels = 18,
	.shutdown_reg = 0x00,
	.pwm_update_reg = 0x25,
	.global_ctrl_reg = 0x4B,
	.reset_reg = 0x4F,
	.pwm_base = 0x0A,
	.pwm_reversed = false,
	.en_base = 0x2F,
	.en_regs = 18,
	.en_per_reg = 1,
};

static const struct htr32xx_chipdef htr3212_def = {
	.channels = 12,
	.shutdown_reg = 0x00,
	.pwm_update_reg = 0x25,
	.global_ctrl_reg = 0x4B,
	.reset_reg = 0x4F,
	.pwm_base = 0x0D,
	.pwm_reversed = false,
	.en_base = 0x32,
	.en_regs = 12,
	.en_per_reg = 1,
};

static const struct of_device_id htr32xx_of_match[] = {
	{ .compatible = "htr,htr3236", .data = &htr3236_def },
	{ .compatible = "htr,htr3218", .data = &htr3218_def },
	{ .compatible = "htr,htr3212", .data = &htr3212_def },
	{ }
};
MODULE_DEVICE_TABLE(of, htr32xx_of_match);

static const struct i2c_device_id htr32xx_id[] = {
	{ "htr3236", 0 },
	{ "htr3218", 0 },
	{ "htr3212", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, htr32xx_id);

static struct i2c_driver htr32xx_driver = {
	.driver = {
		.name = "htr32xx",
		.of_match_table = htr32xx_of_match,
	},
	.probe = htr32xx_probe,
	.remove = htr32xx_remove,
	.id_table = htr32xx_id,
};
module_i2c_driver(htr32xx_driver);

MODULE_AUTHOR("Jiali Chen <chenjiali@radxa.com>");
MODULE_DESCRIPTION("HTR32xx LED driver");
MODULE_LICENSE("GPL v2");
