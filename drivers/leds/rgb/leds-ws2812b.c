// SPDX-License-Identifier: GPL-2.0-only
/*
 * WorldSemi WS2812B individually-addressable LED driver using SPI
 *
 * Copyright 2022 Chuanhong Guo <gch981213@gmail.com>
 *
 * This driver simulates WS2812B protocol using SPI MOSI pin. A one pulse
 * is transferred as 3'b110 and a zero pulse is 3'b100. For this driver to
 * work properly, the SPI frequency should be 2.105MHz~2.85MHz and it needs
 * to transfer all the bytes continuously.
 */

#include <linux/led-class-multicolor.h>
#include <linux/leds.h>
#include <linux/completion.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/property.h>
#include <linux/spi/spi.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>

#define WS2812B_BYTES_PER_COLOR 3
#define WS2812B_NUM_COLORS 3
/* A continuous 0 for 50us+ as the 'reset' signal */
#define WS2812B_RESET_LEN 18

struct ws2812b_led {
	struct led_classdev_mc mc_cdev;
	struct mc_subled subled[WS2812B_NUM_COLORS];
	int cascade;
};

struct ws2812b_sync_wait {
	struct list_head list;
	struct completion done;
	unsigned long seq;
	int ret;
};

struct ws2812b_priv {
	struct led_classdev ldev;
	struct spi_device *spi;
	spinlock_t state_lock; /* protects shadow_buf and flush state */
	struct mutex io_lock; /* serializes SPI transfers */
	struct workqueue_struct *flush_wq;
	struct work_struct flush_work;
	int num_leds;
	size_t data_len;
	u8 *shadow_buf;
	u8 *tx_buf;
	bool dirty;
	bool flush_pending;
	bool removing;
	unsigned long update_seq;
	unsigned long flushed_seq;
	struct list_head sync_waiters;
	int last_flush_ret;
	struct ws2812b_led leds[];
};

/**
 * ws2812b_set_byte - convert a byte of data to 3-byte SPI data for pulses
 * @buf: target data buffer
 * @offset: offset of the target byte in the data stream
 * @val: 1-byte data to be set
 *
 * WS2812B receives a stream of bytes from DI, takes the first 3 byte as LED
 * brightness and pases the rest to the next LED through the DO pin.
 * This function assembles a single byte of data to the LED:
 * A bit is represented with a pulse of specific length. A long pulse is a 1
 * and a short pulse is a 0.
 * SPI transfers data continuously, MSB first. We can send 3'b100 to create a
 * 0 pulse and 3'b110 for a 1 pulse. In this way, a byte of data takes up 3
 * bytes in a SPI transfer:
 *  1x0 1x0 1x0 1x0 1x0 1x0 1x0 1x0
 * Let's rearrange it in 8 bits:
 *  1x01x01x 01x01x01 x01x01x0
 * The higher 3 bits, middle 2 bits and lower 3 bits are represented with the
 * 1st, 2nd and 3rd byte in the SPI transfer respectively.
 * There are only 8 combinations for 3 bits and 4 for 2 bits, so we can create
 * a lookup table for the 3 bytes.
 * e.g. For 0x6b -> 2'b01101011:
 *  Bit 7-5: 3'b011 -> 10011011 -> 0x9b
 *  Bit 4-3: 2'b01  -> 01001101 -> 0x4d
 *  Bit 2-0: 3'b011 -> 00110110 -> 0x36
 */
static void ws2812b_set_byte(u8 *buf, size_t offset, u8 val)
{
	/* The lookup table for Bit 7-5 4-3 2-0 */
	const u8 h3b[] = { 0x92, 0x93, 0x9a, 0x9b, 0xd2, 0xd3, 0xda, 0xdb };
	const u8 m2b[] = { 0x49, 0x4d, 0x69, 0x6d };
	const u8 l3b[] = { 0x24, 0x26, 0x34, 0x36, 0xa4, 0xa6, 0xb4, 0xb6 };
	u8 *p = buf + WS2812B_RESET_LEN +
		(offset * WS2812B_BYTES_PER_COLOR);

	p[0] = h3b[val >> 5]; /* Bit 7-5 */
	p[1] = m2b[(val >> 3) & 0x3]; /* Bit 4-3 */
	p[2] = l3b[val & 0x7]; /* Bit 2-0 */
}

static unsigned long ws2812b_stage_locked(struct ws2812b_priv *priv,
					  struct ws2812b_led *led,
					  struct led_classdev_mc *mc_cdev,
					  enum led_brightness brightness)
{
	unsigned long seq;
	int i;

	led_mc_calc_color_components(mc_cdev, brightness);

	for (i = 0; i < WS2812B_NUM_COLORS; i++)
		ws2812b_set_byte(priv->shadow_buf,
				 led->cascade * WS2812B_NUM_COLORS + i,
				 led->subled[i].brightness);
	priv->dirty = true;
	seq = ++priv->update_seq;

	return seq;
}

static int ws2812b_stage_and_queue(struct ws2812b_priv *priv,
				   struct ws2812b_led *led,
				   struct led_classdev_mc *mc_cdev,
				   enum led_brightness brightness,
				   struct ws2812b_sync_wait *waiter)
{
	unsigned long flags;
	bool queue;

	spin_lock_irqsave(&priv->state_lock, flags);
	if (priv->removing) {
		spin_unlock_irqrestore(&priv->state_lock, flags);
		return -ENODEV;
	}

	if (waiter) {
		waiter->seq = ws2812b_stage_locked(priv, led, mc_cdev, brightness);
		list_add_tail(&waiter->list, &priv->sync_waiters);
	} else {
		ws2812b_stage_locked(priv, led, mc_cdev, brightness);
	}

	queue = !priv->flush_pending;
	priv->flush_pending = true;
	spin_unlock_irqrestore(&priv->state_lock, flags);

	if (queue)
		queue_work(priv->flush_wq, &priv->flush_work);

	return 0;
}

static void ws2812b_mark_removing(struct ws2812b_priv *priv)
{
	LIST_HEAD(waiters);
	struct ws2812b_sync_wait *waiter;
	struct ws2812b_sync_wait *tmp;
	unsigned long flags;

	spin_lock_irqsave(&priv->state_lock, flags);
	priv->removing = true;
	list_splice_init(&priv->sync_waiters, &waiters);
	spin_unlock_irqrestore(&priv->state_lock, flags);

	list_for_each_entry_safe(waiter, tmp, &waiters, list) {
		waiter->ret = -ENODEV;
		list_del(&waiter->list);
		complete(&waiter->done);
	}
}

static void ws2812b_flush_work(struct work_struct *work)
{
	struct ws2812b_priv *priv = container_of(work, struct ws2812b_priv,
						  flush_work);
	struct ws2812b_sync_wait *waiter;
	struct ws2812b_sync_wait *tmp;
	unsigned long flags;
	unsigned long seq;
	bool dirty;
	int ret;

	spin_lock_irqsave(&priv->state_lock, flags);
	if (!priv->dirty) {
		priv->flush_pending = false;
		spin_unlock_irqrestore(&priv->state_lock, flags);
		return;
	}
	memcpy(priv->tx_buf, priv->shadow_buf, priv->data_len);
	seq = priv->update_seq;
	priv->dirty = false;
	spin_unlock_irqrestore(&priv->state_lock, flags);

	mutex_lock(&priv->io_lock);
	ret = spi_write(priv->spi, priv->tx_buf, priv->data_len);
	mutex_unlock(&priv->io_lock);
	if (ret)
		dev_err_ratelimited(&priv->spi->dev,
				    "SPI transfer failed: %d\n", ret);

	spin_lock_irqsave(&priv->state_lock, flags);
	priv->last_flush_ret = ret;
	if (priv->flushed_seq < seq)
		priv->flushed_seq = seq;
	list_for_each_entry_safe(waiter, tmp, &priv->sync_waiters, list) {
		if (waiter->seq > priv->flushed_seq)
			continue;
		waiter->ret = ret;
		list_del(&waiter->list);
		complete(&waiter->done);
	}
	dirty = priv->dirty;
	priv->flush_pending = dirty;
	spin_unlock_irqrestore(&priv->state_lock, flags);

	if (dirty)
		queue_work(priv->flush_wq, &priv->flush_work);
}

static void
ws2812b_set(struct led_classdev *cdev, enum led_brightness brightness)
{
	struct led_classdev_mc *mc_cdev = lcdev_to_mccdev(cdev);
	struct ws2812b_led *led =
		container_of(mc_cdev, struct ws2812b_led, mc_cdev);
	struct ws2812b_priv *priv = dev_get_drvdata(cdev->dev->parent);

	ws2812b_stage_and_queue(priv, led, mc_cdev, brightness, NULL);
}

static int
ws2812b_set_blocking(struct led_classdev *cdev, enum led_brightness brightness)
{
	struct led_classdev_mc *mc_cdev = lcdev_to_mccdev(cdev);
	struct ws2812b_led *led =
		container_of(mc_cdev, struct ws2812b_led, mc_cdev);
	struct ws2812b_priv *priv = dev_get_drvdata(cdev->dev->parent);
	struct ws2812b_sync_wait waiter;
	int ret;

	INIT_LIST_HEAD(&waiter.list);
	init_completion(&waiter.done);
	waiter.ret = 0;

	ret = ws2812b_stage_and_queue(priv, led, mc_cdev, brightness, &waiter);
	if (ret)
		return ret;

	wait_for_completion(&waiter.done);

	return waiter.ret;
}

static int ws2812b_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	int cur_led = 0;
	struct ws2812b_priv *priv;
	int num_leds, i, cnt, ret;

	num_leds = device_get_child_node_count(dev);

	priv = devm_kzalloc(dev, struct_size(priv, leds, num_leds), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	priv->data_len =
		num_leds * WS2812B_BYTES_PER_COLOR * WS2812B_NUM_COLORS +
		WS2812B_RESET_LEN;
	priv->shadow_buf = kzalloc(priv->data_len, GFP_KERNEL);
	if (!priv->shadow_buf)
		return -ENOMEM;
	priv->tx_buf = kzalloc(priv->data_len, GFP_KERNEL);
	if (!priv->tx_buf) {
		ret = -ENOMEM;
		goto ERR_FREE_SHADOW;
	}

	for (i = 0; i < num_leds * WS2812B_NUM_COLORS; i++)
		ws2812b_set_byte(priv->shadow_buf, i, 0);
	memcpy(priv->tx_buf, priv->shadow_buf, priv->data_len);

	spin_lock_init(&priv->state_lock);
	mutex_init(&priv->io_lock);
	INIT_LIST_HEAD(&priv->sync_waiters);
	INIT_WORK(&priv->flush_work, ws2812b_flush_work);
	priv->num_leds = num_leds;
	priv->spi = spi;
	priv->last_flush_ret = 0;
	priv->flush_wq = alloc_ordered_workqueue("%s", 0, dev_name(dev));
	if (!priv->flush_wq) {
		ret = -ENOMEM;
		goto ERR_DESTROY_MUTEX;
	}

	spi_set_drvdata(spi, priv);

	device_for_each_child_node_scoped(dev, led_node) {
		struct led_classdev_mc *mc_cdev = &priv->leds[cur_led].mc_cdev;
		struct led_init_data init_data = {
			.fwnode = led_node,
		};
		/* WS2812B LEDs usually come with GRB color */
		u32 color_idx[WS2812B_NUM_COLORS] = {
			LED_COLOR_ID_GREEN,
			LED_COLOR_ID_RED,
			LED_COLOR_ID_BLUE,
		};
		u32 cascade;

		ret = fwnode_property_read_u32(led_node, "reg", &cascade);
		if (ret) {
			dev_err(dev, "failed to obtain numerical LED index for %s",
				fwnode_get_name(led_node));
			goto ERR_UNREG_LEDS;
		}
		if (cascade >= num_leds) {
			dev_err(dev, "LED index of %s is larger than the number of LEDs.",
				fwnode_get_name(led_node));
			ret = -EINVAL;
			goto ERR_UNREG_LEDS;
		}

		cnt = fwnode_property_count_u32(led_node, "color-index");
		if (cnt > 0 && cnt <= WS2812B_NUM_COLORS)
			fwnode_property_read_u32_array(led_node, "color-index",
						       color_idx, (size_t)cnt);

		mc_cdev->subled_info = priv->leds[cur_led].subled;
		mc_cdev->num_colors = WS2812B_NUM_COLORS;
		mc_cdev->led_cdev.max_brightness = 255;
		mc_cdev->led_cdev.brightness_set = ws2812b_set;
		mc_cdev->led_cdev.brightness_set_blocking =
			ws2812b_set_blocking;

		for (i = 0; i < WS2812B_NUM_COLORS; i++) {
			priv->leds[cur_led].subled[i].color_index = color_idx[i];
			priv->leds[cur_led].subled[i].intensity = 255;
		}

		priv->leds[cur_led].cascade = cascade;

		ret = led_classdev_multicolor_register_ext(dev, mc_cdev, &init_data);
		if (ret) {
			dev_err(dev, "registration of %s failed.",
				fwnode_get_name(led_node));
			goto ERR_UNREG_LEDS;
		}
		cur_led++;
	}

	return 0;
ERR_UNREG_LEDS:
	ws2812b_mark_removing(priv);
	while (cur_led--)
		led_classdev_multicolor_unregister(&priv->leds[cur_led].mc_cdev);
	flush_workqueue(priv->flush_wq);
	destroy_workqueue(priv->flush_wq);
ERR_DESTROY_MUTEX:
	mutex_destroy(&priv->io_lock);
	kfree(priv->tx_buf);
ERR_FREE_SHADOW:
	kfree(priv->shadow_buf);
	return ret;
}

static void ws2812b_remove(struct spi_device *spi)
{
	struct ws2812b_priv *priv = spi_get_drvdata(spi);
	int cur_led;

	ws2812b_mark_removing(priv);
	for (cur_led = priv->num_leds - 1; cur_led >= 0; cur_led--)
		led_classdev_multicolor_unregister(&priv->leds[cur_led].mc_cdev);
	flush_workqueue(priv->flush_wq);
	destroy_workqueue(priv->flush_wq);
	kfree(priv->tx_buf);
	kfree(priv->shadow_buf);
	mutex_destroy(&priv->io_lock);
}

static const struct spi_device_id ws2812b_spi_ids[] = {
	{ "ws2812b" },
	{},
};
MODULE_DEVICE_TABLE(spi, ws2812b_spi_ids);

static const struct of_device_id ws2812b_dt_ids[] = {
	{ .compatible = "worldsemi,ws2812b" },
	{},
};
MODULE_DEVICE_TABLE(of, ws2812b_dt_ids);

static struct spi_driver ws2812b_driver = {
	.probe		= ws2812b_probe,
	.remove		= ws2812b_remove,
	.id_table	= ws2812b_spi_ids,
	.driver = {
		.name		= KBUILD_MODNAME,
		.of_match_table	= ws2812b_dt_ids,
	},
};

module_spi_driver(ws2812b_driver);

MODULE_AUTHOR("Chuanhong Guo <gch981213@gmail.com>");
MODULE_DESCRIPTION("WS2812B LED driver using SPI");
MODULE_LICENSE("GPL");
