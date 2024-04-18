/*
 * ILITEK Touch IC driver
 *
 * Copyright (C) 2011 ILI Technology Corporation.
 *
 * Author: Luca Hsu <luca_hsu@ilitek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301 USA.
 *
 */

#include "ilitek_ts.h"
#include "ilitek_common.h"

struct ilitek_ts_data *ts;

char driver_ver[] = {
	DRIVER_VERSION_0, DRIVER_VERSION_1, DRIVER_VERSION_2,
	DRIVER_VERSION_3, CUSTOMER_H_ID, CUSTOMER_L_ID, TEST_VERSION,
};

#if ILITEK_PLAT == ILITEK_PLAT_MTK
extern struct tpd_device *tpd;
#ifdef ILITEK_ENABLE_DMA
static uint8_t *I2CDMABuf_va;
static dma_addr_t I2CDMABuf_pa;
#endif
#endif

#if defined(ILITEK_WAKELOCK_SUPPORT)
struct wake_lock ilitek_wake_lock;
#endif

#ifdef ILITEK_TUNING_MESSAGE
static struct sock *ilitek_netlink_sock;
bool ilitek_debug_flag;
static u_int ilitek_pid = 100, ilitek_seq = 23;
#endif

static void __maybe_unused ilitek_udp_reply(void *payload, int size)
{
#ifdef ILITEK_TUNING_MESSAGE
	struct sk_buff *skb;
	struct nlmsghdr *nlh;
	int len = NLMSG_SPACE(size);
	int ret;
	int pid = ilitek_pid, seq = ilitek_seq;

	TP_DBG(NULL, "[%s] ilitek_debug_flag: %d\n", __func__, ilitek_debug_flag);
	if (!ilitek_debug_flag)
		return;

	skb = alloc_skb(len, GFP_ATOMIC);
	if (!skb) {
		TP_ERR(NULL, "alloc skb error\n");
		return;
	}

	nlh = nlmsg_put(skb, pid, seq, 0, size, 0);
	if (!nlh)
		goto nlmsg_failure;

	nlh->nlmsg_flags = 0;
	memcpy(NLMSG_DATA(nlh), payload, size);

	NETLINK_CB(skb).portid = 0;	/* from kernel */
	NETLINK_CB(skb).dst_group = 0;	/* unicast */

	ret = netlink_unicast(ilitek_netlink_sock, skb, pid, MSG_DONTWAIT);
	if (ret < 0)
		TP_ERR(NULL, "ilitek send failed, ret: %d\n", ret);
	return;

nlmsg_failure:
	kfree_skb(skb);

#endif /* ILITEK_TUNING_MESSAGE */
}

static void __maybe_unused udp_receive(struct sk_buff *skb)
{
#ifdef ILITEK_TUNING_MESSAGE
	int count = 0, ret = 0, i = 0;
	uint8_t *data;
	struct nlmsghdr *nlh;

	nlh = (struct nlmsghdr *)skb->data;
	ilitek_pid = NETLINK_CREDS(skb)->pid;
	ilitek_seq = nlh->nlmsg_seq;

	TP_DBG(NULL, "netlink received, pid: %d, seq: %d\n",
		     ilitek_pid, ilitek_seq);

	data = (uint8_t *) NLMSG_DATA(nlh);
	count = nlmsg_len(nlh);
	if (!strcmp(data, "Open!")) {
		TP_MSG(NULL, "data is :%s\n", (char *)data);
		ts->operation_protection = true;
		ilitek_udp_reply(data, sizeof("Open!"));
	} else if (!strcmp(data, "Close!")) {
		TP_MSG(NULL, "data is :%s\n", (char *)data);
		ts->operation_protection = false;
	} else if (!strcmp(data, "Wifi_Paint_Start") ||
		   !strcmp(data, "Daemon_Debug_Start")) {
		ilitek_debug_flag = true;
	} else if (!strcmp(data, "Wifi_Paint_End") ||
		   !strcmp(data, "Daemon_Debug_End")) {
		ilitek_debug_flag = false;
	}


	TP_DBG(NULL, "count = %d  data[count -3] = %d data[count -2] = %c\n", count, data[count - 3], data[count - 2]);
	for (i = 0; i < count; i++) {
		//TP_MSG(NULL, "data[%d] = 0x%x\n", i, data[i]);
	}
	if (data[count - 2] == 'I' && (count == 20 || count == 52) &&
	    data[0] == 0x77 && data[1] == 0x77) {
		TP_DBG(NULL, "IOCTL_WRITE CMD = %d\n", data[2]);
		switch (data[2]) {
		case 13:
			//ilitek_irq_enable();
			TP_MSG(NULL, "ilitek_irq_enable. do nothing\n");
			break;
		case 12:
			//ilitek_irq_disable();
			TP_MSG(NULL, "ilitek_irq_disable. do nothing\n");
			break;
		case 19:
			ilitek_reset(ts->dev->reset_time);
			break;
		case 21:
			TP_MSG(NULL, "ilitek The ilitek_debug_flag = %d.\n", data[3]);
			if (data[3] == 0)
				ilitek_debug_flag = false;
			else if (data[3] == 1)
				ilitek_debug_flag = true;
			break;
		case 15:
			if (data[3] == 0) {
				ilitek_irq_disable();
				TP_DBG(NULL, "ilitek_irq_disable.\n");
			} else {
				ilitek_irq_enable();
				TP_DBG(NULL, "ilitek_irq_enable.\n");
			}
			break;
		case 16:
			ts->operation_protection = data[3];
			TP_MSG(NULL, "ts->operation_protection = %d\n", ts->operation_protection);
			break;
		case 8:
			TP_MSG(NULL, "get driver version\n");
			ilitek_udp_reply(driver_ver, 7);
			break;
		case 18:
			TP_DBG(NULL, "firmware update write 33 bytes data\n");
			ret = ilitek_write(&data[3], 33);
			if (ret < 0)
				TP_ERR(NULL, "i2c write error, ret %d\n", ret);
			if (ret < 0) {
				data[0] = 1;
			} else {
				data[0] = 0;
			}
			ilitek_udp_reply(data, 1);
			return;
			break;
		default:
			return;
		}
	} else if (data[count - 2] == 'W') {
		ret = ilitek_write(data, count - 2);
		if (ret < 0)
			TP_ERR(NULL, "i2c write error, ret %d\n", ret);
		if (ret < 0) {
			data[0] = 1;
		} else {
			data[0] = 0;
		}
		ilitek_udp_reply(data, 1);
	} else if (data[count - 2] == 'R') {
		ret = ilitek_read(data, count - 2);
		if (ret < 0)
			TP_ERR(NULL, "i2c read error, ret %d\n", ret);
		if (ret < 0) {
			data[count - 2] = 1;
		} else {
			data[count - 2] = 0;
		}
		ilitek_udp_reply(data, count - 1);
	}
#endif /* ILITEK_TUNING_MESSAGE */
}

static void ilitek_esd_check(struct work_struct *work)
{
	int retry = 3;
	static bool is_first_run = true;
	static uint32_t protocol_ver = 0;

	/*
	 * update protocol version at the first run
	 */
	if (is_first_run) {
		is_first_run = false;
		protocol_ver = ts->dev->protocol.ver;
		TP_MSG(NULL, "[ESD] firstly loading protocol ver: %x as ref.\n",
			protocol_ver);
	}

	if (ts->operation_protection || ts->esd_skip) {
		TP_MSG(NULL, "[ESD] operation_protection: %hhu, esd_skip: %hhu\n",
			ts->operation_protection, ts->esd_skip);
		goto skip_return;
	}

	mutex_lock(&ts->ilitek_mutex);

	for (; retry-- > 0;) {
		if (api_protocol_set_cmd(ts->dev, GET_PTL_VER, NULL) < 0) {
			TP_ERR(NULL, "[ESD] i2c comm. failed\n");
			continue;
		}

		if (protocol_ver != ts->dev->protocol.ver) {
			TP_ERR(NULL, "unexpected ptl ver (referance)%x vs. %x\n",
				protocol_ver, ts->dev->protocol.ver);
			continue;
		}

		goto pass_return;
	}

	ilitek_reset(ts->dev->reset_time);

pass_return:
	mutex_unlock(&ts->ilitek_mutex);

skip_return:
	queue_delayed_work(ts->esd_workq, &ts->esd_work, ts->esd_delay);
}

void ilitek_irq_enable(void)
{
	if (!ts->irq_registerred)
		return;

	if (atomic_read(&ts->irq_enabled))
		return;

#ifdef MTK_UNDTS
	mt_eint_unmask(CUST_EINT_TOUCH_PANEL_NUM);
#else
	enable_irq(ts->irq);
#endif

	atomic_set(&ts->irq_enabled, 1);
	TP_DBG(NULL, "%s\n", __func__);
}

void ilitek_irq_disable(void)
{
	if (!ts->irq_registerred)
		return;

	if (!atomic_read(&ts->irq_enabled))
		return;

#ifdef MTK_UNDTS
	mt_eint_mask(CUST_EINT_TOUCH_PANEL_NUM);
#else
	disable_irq_nosync(ts->irq);
#endif

	atomic_set(&ts->irq_enabled, 0);
	TP_DBG(NULL, "%s\n", __func__);
}

#ifdef ILITEK_ENABLE_DMA
static int ilitek_dma_i2c_read(uint8_t *buf, int len)
{
	struct i2c_client *client = (struct i2c_client *)ts->client;
	int err;


	if (len < 8) {
		client->ext_flag = client->ext_flag & (~I2C_DMA_FLAG);
		return i2c_master_recv(client, buf, len);
	}

	client->ext_flag = client->ext_flag | I2C_DMA_FLAG;
	if ((err = i2c_master_recv(client, (uint8_t *)I2CDMABuf_pa, len)) < 0)
		return err;

	memcpy(buf, I2CDMABuf_va, len);

	return 0;
}

static int ilitek_dma_i2c_write(uint8_t *cmd, int len)
{
	struct i2c_client *client = (struct i2c_client *)ts->client;

	if (len <= 8) {
		client->ext_flag = client->ext_flag & (~I2C_DMA_FLAG);
		return i2c_master_send(client, cmd, len);
	}

	memcpy(I2CDMABuf_va, cmd, len);

	client->ext_flag = client->ext_flag | I2C_DMA_FLAG;

	return i2c_master_send(client, (uint8_t *)I2CDMABuf_pa, len);
}
#endif

static int ilitek_i2c_transfer(struct i2c_msg *msgs, int cnt)
{
	int err = 0;
	struct i2c_client *client = (struct i2c_client *)ts->client;
	int count = ILITEK_I2C_RETRY_COUNT;

#ifdef ILITEK_ENABLE_DMA
	int i;

	for (i = 0; i < cnt; i++) {
		count = ILITEK_I2C_RETRY_COUNT;
		while (count-- >= 0) {
			msgs[i].ext_flag = 0;
			if (msgs[i].flags == I2C_M_RD)
				err = ilitek_dma_i2c_read(msgs[i].buf, msgs[i].len);
			else if (!msgs[i].flags)
				err = ilitek_dma_i2c_write(msgs[i].buf, msgs[i].len);

			if (err < 0) {
				TP_ERR(NULL, "i2c[0x%hx] dma tx/rx failed, err: %d\n",
					msgs[i].addr, err);
				mdelay(20);
				continue;
			}

			break;
		}
	}
#else
	while (count-- >= 0) {
		if ((err = i2c_transfer(client->adapter, msgs, cnt)) < 0) {
			TP_ERR(NULL, "i2c[0x%hx] tx/rx failed, err: %d\n",
				msgs[0].addr, err);
			mdelay(20);
			continue;
		}
		break;
	}
#endif

	return err;
}

static int __maybe_unused ilitek_i2c_write_and_read(uint8_t *cmd, int w_len,
						    int delay_ms, uint8_t *buf,
						    int r_len)
{
	int error;

	/*
	 * Default ILITEK_BL_ADDR. is firstly used.
	 * if communication failed, change between BL addr. and
	 * other addr. defined by DTS, then retry.
	 */
	static unsigned short addr = ILITEK_BL_ADDR;
	struct i2c_client *client = (struct i2c_client *)ts->client;
	struct i2c_msg msgs[2] = {
		{.addr = addr, .flags = 0, .len = w_len,
		 .buf = cmd, SCL_RATE(400000)},
		{.addr = addr, .flags = I2C_M_RD, .len = r_len,
		 .buf = buf, SCL_RATE(400000)}
	};

	/*
	 * IMPORTANT: If I2C repeat start is required, please check with ILITEK.
	 */
	if (w_len > 0 && r_len > 0 && !delay_ms) {
		if (ilitek_i2c_transfer(msgs, 2) < 0) {
			/* try another i2c addr. (default: 0x41) */
			addr = (addr == ILITEK_BL_ADDR) ?
				client->addr : ILITEK_BL_ADDR;
			msgs[0].addr = msgs[1].addr = addr;

			return ilitek_i2c_transfer(msgs, 2);
		}

		return 0;
	}

	if (w_len > 0 && ilitek_i2c_transfer(msgs, 1) < 0) {
		/* try another i2c addr. (default: 0x41) */
		addr = (addr == ILITEK_BL_ADDR) ? client->addr : ILITEK_BL_ADDR;
		msgs[0].addr = msgs[1].addr = addr;

		if ((error = ilitek_i2c_transfer(msgs, 1)) < 0)
			return error;
	}

	if (delay_ms > 0)
		mdelay(delay_ms);

	if (r_len > 0 && ilitek_i2c_transfer(msgs + 1, 1) < 0) {
		/* try another i2c addr. (default: 0x41) */
		addr = (addr == ILITEK_BL_ADDR) ? client->addr : ILITEK_BL_ADDR;
		msgs[0].addr = msgs[1].addr = addr;

		return ilitek_i2c_transfer(msgs + 1, 1);
	}

	return 0;
}

static int __maybe_unused ilitek_i2c_write(uint8_t *cmd, int len)
{
	return ilitek_i2c_write_and_read(cmd, len, 0, NULL, 0);
}

static int __maybe_unused ilitek_i2c_read(uint8_t *buf, int len)
{
	return ilitek_i2c_write_and_read(NULL, 0, 0, buf, len);
}

static int __maybe_unused ilitek_spi_write_and_read(uint8_t *cmd, int w_len,
						    int delay_ms, uint8_t *buf,
						    int r_len)
{
	int error = 0;
	uint8_t *wbuf, *rbuf;
	struct spi_device *spi = (struct spi_device *)ts->client;
	struct spi_transfer xfer = {
		.len = r_len + 4,
		.speed_hz = ((struct spi_device *)ts->client)->max_speed_hz,
	};
	struct spi_message msg;

	if (w_len > 0 && r_len > 0) {
		if ((error = ilitek_spi_write_and_read(cmd, w_len, delay_ms,
						       NULL, 0)) < 0)
			return error;

		return ilitek_spi_write_and_read(NULL, 0, 0, buf, r_len);
	}

	wbuf = CALLOC(4096, sizeof(uint8_t));
	rbuf = CALLOC(4096, sizeof(uint8_t));

	if (!wbuf || !rbuf) {
		error = -ENOMEM;
		goto exit;
	}

	xfer.tx_buf = wbuf;
	xfer.rx_buf = rbuf;

	wbuf[1] = 0xAA;

	/* wbuf[0] set as 0x83 for spi data read */
	if (r_len > 0) {
		wbuf[0] = 0x83;
		memset(wbuf + 2, 0, xfer.len - 2);

		spi_message_init(&msg);
		spi_message_add_tail(&xfer, &msg);
		if ((error = spi_sync(spi, &msg)) < 0)
			goto exit;

		TP_DBG(NULL, "[rbuf]: %*phD, len: %d\n", xfer.len, rbuf, xfer.len);

		memcpy(buf, rbuf + 4, r_len);
	} else if (w_len > 0) {
		wbuf[0] = 0x82;
		wbuf[2] = cmd[0];
		wbuf[3] = 0;
		memcpy(wbuf + 4, cmd + 1, w_len - 1);

		TP_DBG(NULL, "[wbuf]: %*phD, len: %d\n", 3 + w_len, wbuf, 3 + w_len);

		if ((error = spi_write(spi, wbuf, 3 + w_len)) < 0)
			goto exit;

		if (delay_ms > 0)
			mdelay(delay_ms);
	}

exit:
	CFREE(wbuf);
	CFREE(rbuf);

	return (error < 0) ? error : 0;
}

static int __maybe_unused ilitek_spi_write(uint8_t *cmd, int len)
{
	return ilitek_spi_write_and_read(cmd, len, 0, NULL, 0);
}

static int __maybe_unused ilitek_spi_read(uint8_t *buf, int len)
{
	return ilitek_spi_write_and_read(NULL, 0, 0, buf, len);
}

int ilitek_write(uint8_t *cmd, int len)
{
	int error;

#ifdef ILITEK_SPI_INTERFACE
	error = ilitek_spi_write(cmd, len);
#else
	error = ilitek_i2c_write(cmd, len);
#endif

	return (error < 0) ? error : 0;
}

int ilitek_read(uint8_t *buf, int len)
{
	int error;

#ifdef ILITEK_SPI_INTERFACE
	error = ilitek_spi_read(buf, len);
#else
	error = ilitek_i2c_read(buf, len);
#endif

	return (error < 0) ? error : 0;
}

int ilitek_write_and_read(uint8_t *cmd, int w_len, int delay_ms,
			  uint8_t *buf, int r_len)
{
	int error;

#ifdef ILITEK_SPI_INTERFACE
	error = ilitek_spi_write_and_read(cmd, w_len, delay_ms, buf, r_len);
#else
	error = ilitek_i2c_write_and_read(cmd, w_len, delay_ms, buf, r_len);
#endif

	return (error < 0) ? error : 0;
}

void __maybe_unused ilitek_gpio_dbg(void)
{
#if defined(ILITEK_GPIO_DEBUG)
	gpio_direction_output(ts->test_gpio, 0);
	mdelay(1);
	gpio_direction_output(ts->test_gpio, 1);
#endif
}

void ilitek_reset(int delay)
{
	TP_MSG(NULL, "reset_gpio: %d, delay: %d\n", ts->reset_gpio, delay);

	ilitek_irq_disable();

#if ILITEK_PLAT == ILITEK_PLAT_MTK && defined(MTK_UNDTS)
	mt_set_gpio_mode(ts->reset_gpio, GPIO_CTP_RST_PIN_M_GPIO);
	mt_set_gpio_dir(ts->reset_gpio, GPIO_DIR_OUT);
	mt_set_gpio_out(ts->reset_gpio, GPIO_OUT_ONE);
	mdelay(10);

	mt_set_gpio_mode(ts->reset_gpio, GPIO_CTP_RST_PIN_M_GPIO);
	mt_set_gpio_dir(ts->reset_gpio, GPIO_DIR_OUT);
	mt_set_gpio_out(ts->reset_gpio, GPIO_OUT_ZERO);
	mdelay(10);

	mt_set_gpio_mode(ts->reset_gpio, GPIO_CTP_RST_PIN_M_GPIO);
	mt_set_gpio_dir(ts->reset_gpio, GPIO_DIR_OUT);
	mt_set_gpio_out(ts->reset_gpio, GPIO_OUT_ONE);
	mdelay(delay);
#elif ILITEK_PLAT == ILITEK_PLAT_MTK
	tpd_gpio_output(ts->reset_gpio, 1);
	mdelay(10);
	tpd_gpio_output(ts->reset_gpio, 0);
	mdelay(10);
	tpd_gpio_output(ts->reset_gpio, 1);
	mdelay(delay);
#else
	gpio_direction_output(ts->reset_gpio, 1);
	mdelay(10);
	gpio_direction_output(ts->reset_gpio, 0);
	mdelay(10);
	gpio_direction_output(ts->reset_gpio, 1);
	mdelay(delay);
#endif

	ilitek_irq_enable();
}

static int ilitek_free_gpio(void)
{

#ifndef MTK_UNDTS
	if (gpio_is_valid(ts->reset_gpio)) {
		TP_MSG(NULL, "reset_gpio is valid so free\n");
		gpio_free(ts->reset_gpio);
	}
	if (gpio_is_valid(ts->irq_gpio)) {
		TP_MSG(NULL, "irq_gpio is valid so free\n");
		gpio_free(ts->irq_gpio);
	}
#endif

#if defined(ILITEK_GPIO_DEBUG)
	if (gpio_is_valid(ts->test_gpio)) {
		TP_MSG(NULL, "test_gpio is valid so free\n");
		gpio_free(ts->test_gpio);
	}
#endif

	return 0;
}

static int ilitek_request_pen_input_dev(void)
{
	int error;
	struct input_dev *input;

	int x_min = ts->dev->screen_info.pen_x_min;
	int y_min = ts->dev->screen_info.pen_y_min;
	int x_max = ts->dev->screen_info.pen_x_max;
	int y_max = ts->dev->screen_info.pen_y_max;

	if (!(input = input_allocate_device()))
		return -ENOMEM;

	TP_DBG(NULL, "registering pen input device\n");

	__set_bit(INPUT_PROP_DIRECT, input->propbit);
	input->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);

	__set_bit(BTN_TOOL_PEN, input->keybit);		/* In Range */
	__set_bit(BTN_TOOL_RUBBER, input->keybit);	/* Invert */
	__set_bit(BTN_STYLUS, input->keybit);		/* Barrel Swtich */
	__set_bit(BTN_TOUCH, input->keybit);		/* Tip Switch */

	input->name = "ILITEK STYLUS";
	input->id.bustype = BUS_I2C;
	input->dev.parent = ts->device;

#if ILITEK_ROTATE_FLAG
	swap(x_min, y_min);
	swap(x_max, y_max);
#endif

 	input_set_abs_params(input, ABS_X, x_min, x_max, 0, 0);
	input_set_abs_params(input, ABS_Y, y_min, y_max, 0, 0);

	input_set_abs_params(input, ABS_PRESSURE,
			     ts->dev->screen_info.pressure_min,
			     ts->dev->screen_info.pressure_max, 0, 0);
	input_set_abs_params(input, ABS_TILT_X,
			     ts->dev->screen_info.x_tilt_min,
			     ts->dev->screen_info.x_tilt_max, 0, 0);
	input_set_abs_params(input, ABS_TILT_Y,
			     ts->dev->screen_info.y_tilt_min,
			     ts->dev->screen_info.y_tilt_max, 0, 0);

	if ((error = input_register_device(input))) {
		TP_ERR(NULL, "register pen device failed, err: %d\n", error);
		input_free_device(input);
		return error;
	}

	ts->pen_input_dev = input;

	return 0;
}

static int ilitek_request_input_dev(void)
{
	int error;
	int i;
	struct input_dev *input;

#ifdef ILITEK_USE_MTK_INPUT_DEV
	if (!(input = tpd->dev))
		return -ENOMEM;
#ifdef MTK_UNDTS
	if (tpd_dts_data.use_tpd_button) {
		for (i = 0; i < tpd_dts_data.tpd_key_num; i++)
			input_set_capability(input, EV_KEY,
					     tpd_dts_data.tpd_key_local[i]);
	}
#endif
#else
	int x_min = ts->dev->screen_info.x_min;
	int y_min = ts->dev->screen_info.y_min;
	int x_max = ts->dev->screen_info.x_max;
	int y_max = ts->dev->screen_info.y_max;

	if (!(input = input_allocate_device()))
		return -ENOMEM;
#endif

	TP_DBG(NULL, "registering touch input device\n");

#ifdef ILITEK_TOUCH_PROTOCOL_B
	INPUT_MT_INIT_SLOTS(input, ts->dev->tp_info.max_fingers);
#else
	input_set_abs_params(input, ABS_MT_TRACKING_ID, 0,
			     ts->dev->tp_info.max_fingers, 0, 0);
#endif

#ifdef ILITEK_REPORT_PRESSURE
	input_set_abs_params(input, ABS_MT_PRESSURE, 0, 255, 0, 0);
#endif

	for (i = 0; i < ts->dev->tp_info.key_num; i++)
		set_bit(ts->dev->key.info.keys[i].id & KEY_MAX, input->keybit);

	input_set_capability(input, EV_KEY, KEY_POWER);

#ifndef ILITEK_USE_MTK_INPUT_DEV
	input->name = ILITEK_TS_NAME;
	input->id.bustype = BUS_I2C;
	input->dev.parent = ts->device;

	__set_bit(INPUT_PROP_DIRECT, input->propbit);
	input->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
	input->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);

#ifdef ILITEK_USE_LCM_RESOLUTION
	x_min = 0; y_min = 0;
	x_max = TOUCH_SCREEN_X_MAX; y_max = TOUCH_SCREEN_Y_MAX;
#endif

#if ILITEK_ROTATE_FLAG
	swap(x_min, y_min);
	swap(x_max, y_max);
#endif

	input_set_abs_params(input, ABS_MT_POSITION_X, x_min, x_max, 0, 0);
	input_set_abs_params(input, ABS_MT_POSITION_Y, y_min, y_max, 0, 0);
	input_set_abs_params(input, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(input, ABS_MT_WIDTH_MAJOR, 0, 255, 0, 0);

	if ((error = input_register_device(input))) {
		TP_ERR(NULL, "input_register_device failed, err: %d\n", error);
		input_free_device(input);
		return error;
	}
#endif

	ts->input_dev = input;

	if (ts->dev->tp_info.pen_modes)
		ilitek_request_pen_input_dev();

	return 0;
}

static int ilitek_touch_down(int id, int x, int y, int p, int h, int w)
{
	struct input_dev *input = ts->input_dev;

#ifdef ILITEK_USE_LCM_RESOLUTION
	x = (x - ts->dev->screen_info.x_min) * TOUCH_SCREEN_X_MAX /
		(ts->dev->screen_info.x_max - ts->dev->screen_info.x_min);
	y = (y - ts->dev->screen_info.y_min) * TOUCH_SCREEN_Y_MAX /
		(ts->dev->screen_info.y_max - ts->dev->screen_info.y_min);
#endif

	input_report_key(input, BTN_TOUCH, 1);
#ifdef ILITEK_TOUCH_PROTOCOL_B
	input_mt_slot(input, id);
	input_mt_report_slot_state(input, MT_TOOL_FINGER, true);
#endif
	input_event(input, EV_ABS, ABS_MT_POSITION_X, x);
	input_event(input, EV_ABS, ABS_MT_POSITION_Y, y);
	input_event(input, EV_ABS, ABS_MT_TOUCH_MAJOR, h);
	input_event(input, EV_ABS, ABS_MT_WIDTH_MAJOR, w);
#ifdef ILITEK_REPORT_PRESSURE
	input_event(input, EV_ABS, ABS_MT_PRESSURE, p);
#endif
#ifndef ILITEK_TOUCH_PROTOCOL_B
	input_event(input, EV_ABS, ABS_MT_TRACKING_ID, id);
	input_mt_sync(input);
#endif

#if ILITEK_PLAT == ILITEK_PLAT_MTK
#ifdef CONFIG_MTK_BOOT
#ifndef MTK_UNDTS
	if (tpd_dts_data.use_tpd_button) {
		if (FACTORY_BOOT == get_boot_mode() || RECOVERY_BOOT == get_boot_mode()) {
			tpd_button(x, y, 1);
			TP_DBG(NULL, "tpd_button(x, y, 1) = tpd_button(%d, %d, 1)\n", x, y);
		}
	}
#endif
#endif
#endif
	return 0;
}

static int ilitek_touch_release(int id)
{
	struct input_dev *input = ts->input_dev;

#ifdef ILITEK_TOUCH_PROTOCOL_B
	if (ts->touch_flag[id] == 1) {
		TP_DBG(NULL, "release point id = %d\n", id);
		input_mt_slot(input, id);
		input_mt_report_slot_state(input, MT_TOOL_FINGER, false);
	}
#else
	input_report_key(input, BTN_TOUCH, 0);
	input_mt_sync(input);
#endif
	set_arr(ts->touch_flag, id, 0);

#if ILITEK_PLAT == ILITEK_PLAT_MTK
#ifdef CONFIG_MTK_BOOT
#ifndef MTK_UNDTS
	if (tpd_dts_data.use_tpd_button) {
		if (FACTORY_BOOT == get_boot_mode() ||
		    RECOVERY_BOOT == get_boot_mode()) {
			tpd_button(0, 0, 0);
			TP_DBG(NULL, "tpd_button(x, y, 0) = tpd_button(0, 0, 0)\n");
		}
	}
#endif
#endif
#endif

	return 0;
}

static int ilitek_touch_release_all_point(void)
{
	struct input_dev *input = ts->input_dev;
	int i = 0;

#ifdef ILITEK_TOUCH_PROTOCOL_B
	input_report_key(input, BTN_TOUCH, 0);
	for (i = 0; i < ts->dev->tp_info.max_fingers; i++)
		ilitek_touch_release(i);
#else
	for (i = 0; i < ts->dev->tp_info.max_fingers; i++)
		set_arr(ts->touch_flag, i, 0);
	ilitek_touch_release(0);
#endif
	ts->is_touched = false;
	input_sync(input);
	return 0;
}

static int ilitek_check_key_down(int x, int y)
{
	int j;

	for (j = 0; j < ts->dev->tp_info.key_num; j++) {
		if ((x >= ts->dev->key.info.keys[j].x &&
		     x <= ts->dev->key.info.keys[j].x +
			ts->dev->key.info.x_len) &&
		    (y >= ts->dev->key.info.keys[j].y &&
		     y <= ts->dev->key.info.keys[j].y +
			ts->dev->key.info.y_len)) {
#if ILITEK_PLAT != ILITEK_PLAT_MTK
			input_report_key(ts->input_dev, ts->dev->key.info.keys[j].id, 1);
#else
#ifndef MTK_UNDTS
			if (tpd_dts_data.use_tpd_button) {
				x = tpd_dts_data.tpd_key_dim_local[j].key_x;
				y = tpd_dts_data.tpd_key_dim_local[j].key_y;
				TP_DBG(NULL, "key index=%x, tpd_dts_data.tpd_key_local[%d]=%d key down\n",
					j, j, tpd_dts_data.tpd_key_local[j]);
				ilitek_touch_down(0, x, y, 10, 128, 1);
			}
#else
			x = touch_key_point_maping_array[j].point_x;
			y = touch_key_point_maping_array[j].point_y;
			ilitek_touch_down(0, x, y, 10, 128, 1);
#endif
#endif
			ts->dev->key.clicked[j] = true;
			ts->touch_key_hold_press = true;
			ts->is_touched = true;
			TP_DBG(NULL, "Key, Keydown ID=%d, X=%d, Y=%d, key_status=%d\n",
				ts->dev->key.info.keys[j].id, x, y,
				ts->dev->key.clicked[j]);
			break;
		}
	}
	return 0;
}

static int ilitek_check_key_release(int x, int y, int check_point)
{
	int j = 0;

	for (j = 0; j < ts->dev->tp_info.key_num; j++) {
		if (!ts->dev->key.clicked[j])
			continue;

		if (check_point) {
			if (x < ts->dev->key.info.keys[j].x ||
			    x > ts->dev->key.info.keys[j].x + ts->dev->key.info.x_len ||
			    y < ts->dev->key.info.keys[j].y ||
			    y > ts->dev->key.info.keys[j].y + ts->dev->key.info.y_len) {
#if ILITEK_PLAT != ILITEK_PLAT_MTK
				input_report_key(ts->input_dev,
						 ts->dev->key.info.keys[j].id, 0);
#else
#ifndef MTK_UNDTS
				if (tpd_dts_data.use_tpd_button) {
					TP_DBG(NULL, "key index=%x, tpd_dts_data.tpd_key_local[%d]=%d key up\n", j, j, tpd_dts_data.tpd_key_local[j]);
					ilitek_touch_release(0);
				}
#else
				ilitek_touch_release(0);
#endif
#endif
				ts->dev->key.clicked[j] = false;
				ts->touch_key_hold_press = false;
				TP_DBG(NULL, "Key, Keyout ID=%d, X=%d, Y=%d, key_status=%d\n",
					ts->dev->key.info.keys[j].id, x, y,
					ts->dev->key.clicked[j]);
				break;
			}
		} else {
#if ILITEK_PLAT != ILITEK_PLAT_MTK
			input_report_key(ts->input_dev, ts->dev->key.info.keys[j].id, 0);
#else
#ifndef MTK_UNDTS
			if (tpd_dts_data.use_tpd_button) {
				TP_DBG(NULL, "key index=%x, tpd_dts_data.tpd_key_local[%d]=%d key up\n", j, j, tpd_dts_data.tpd_key_local[j]);
				ilitek_touch_release(0);
			}
#else
			ilitek_touch_release(0);
#endif
#endif
			ts->dev->key.clicked[j] = false;
			ts->touch_key_hold_press = false;
			TP_DBG(NULL, "Key, Keyout ID=%d, X=%d, Y=%d, key_status=%d\n",
				ts->dev->key.info.keys[j].id, x, y,
				ts->dev->key.clicked[j]);
			break;
		}
	}
	return 0;
}

int event_spacing;
static uint8_t finger_state;
static int start_x;
static int start_y;
static int current_x;
static int current_y;

#if ILITEK_GET_TIME_FUNC == ILITEK_GET_TIME_FUNC_WITH_TIME
static struct timeval start_event_time;
#else
unsigned long start_event_time_jiffies;
#endif

static int ilitek_get_time_diff(void)
{
	int diff_milliseconds = 0;
#if ILITEK_GET_TIME_FUNC == ILITEK_GET_TIME_FUNC_WITH_TIME
	struct timeval time_now;

	do_gettimeofday(&time_now);
	diff_milliseconds += (time_now.tv_sec - start_event_time.tv_sec) * 1000;

	if (time_now.tv_usec < start_event_time.tv_usec) {
		diff_milliseconds -= 1000;
		diff_milliseconds += (1000 * 1000 + time_now.tv_usec - start_event_time.tv_usec) / 1000;
	} else
		diff_milliseconds += (time_now.tv_usec - start_event_time.tv_usec) / 1000;

	if (diff_milliseconds < (-10000))
		diff_milliseconds = 10000;
	TP_MSG(NULL, "time_now.tv_sec = %d start_event_time.tv_sec = %d time_now.tv_usec = %d start_event_time.tv_usec = %d diff_milliseconds = %d\n",
			(int)time_now.tv_sec, (int)start_event_time.tv_sec, (int)time_now.tv_usec, (int)start_event_time.tv_usec, diff_milliseconds);
#else
	diff_milliseconds = jiffies_to_msecs(jiffies) - jiffies_to_msecs(start_event_time_jiffies);
	TP_MSG(NULL, "jiffies_to_msecs(jiffies) = %u jiffies_to_msecs(start_event_time_jiffies) = %u diff_milliseconds = %d\n", jiffies_to_msecs(jiffies),
			jiffies_to_msecs(start_event_time_jiffies), diff_milliseconds);
#endif
	return diff_milliseconds;
}

static uint8_t ilitek_double_click_touch(int finger_id, int x, int y,
					 uint8_t finger_state)
{
	TP_MSG(NULL, "start finger_state = %d\n", finger_state);
	if (finger_id > 0) {
		finger_state = 0;
		goto out;
	}
	if (finger_state == 0 || finger_state == 5) {

		finger_state = 1;
		start_x = x;
		start_y = y;
		current_x = 0;
		current_y = 0;
		event_spacing = 0;
#if ILITEK_GET_TIME_FUNC == ILITEK_GET_TIME_FUNC_WITH_TIME
		do_gettimeofday(&start_event_time);
#else
		start_event_time_jiffies = jiffies;
#endif
	} else if (finger_state == 1) {
		event_spacing = ilitek_get_time_diff();
		if (event_spacing > DOUBLE_CLICK_ONE_CLICK_USED_TIME)
			finger_state = 4;
	} else if (finger_state == 2) {
		finger_state = 3;
		current_x = x;
		current_y = y;
		event_spacing = ilitek_get_time_diff();
		if (event_spacing > (DOUBLE_CLICK_ONE_CLICK_USED_TIME + DOUBLE_CLICK_NO_TOUCH_TIME))
			finger_state = 0;
	} else if (finger_state == 3) {
		current_x = x;
		current_y = y;
		event_spacing = ilitek_get_time_diff();
		if (event_spacing > DOUBLE_CLICK_TOTAL_USED_TIME) {
			start_x = current_x;
			start_y = current_y;
			finger_state = 4;
		}
	}
out:
	TP_MSG(NULL, "finger_state = %d event_spacing = %d\n", finger_state, event_spacing);
	return finger_state;
}

static uint8_t ilitek_double_click_release(uint8_t finger_state)
{
	TP_MSG(NULL, "start finger_state = %d\n", finger_state);
	if (finger_state == 1) {
		finger_state = 2;
		event_spacing = ilitek_get_time_diff();
		if (event_spacing > DOUBLE_CLICK_ONE_CLICK_USED_TIME)
			finger_state = 0;
	}
	if (finger_state == 3) {
		event_spacing = ilitek_get_time_diff();
		if ((event_spacing < DOUBLE_CLICK_TOTAL_USED_TIME && event_spacing > 50) && (ABSSUB(current_x, start_x) < DOUBLE_CLICK_DISTANCE)
				&& ((ABSSUB(current_y, start_y) < DOUBLE_CLICK_DISTANCE))) {
			finger_state = 5;
			goto out;
		} else
			finger_state = 0;
	} else if (finger_state == 4)
		finger_state = 0;
out:
	TP_MSG(NULL, "finger_state = %d event_spacing = %d\n", finger_state, event_spacing);
	return finger_state;
}

void __maybe_unused ilitek_gesture_handle(bool touch, int idx, int x, int y)
{
	struct input_dev *input = ts->input_dev;

	if (ts->gesture_status == Gesture_Double_Click) {
		if (touch) {
			finger_state = ilitek_double_click_touch(idx, x, y, finger_state);
			return;
		}
		finger_state = ilitek_double_click_release(finger_state);

		if (finger_state != 5)
			return;
	}

#ifdef ILITEK_WAKELOCK_SUPPORT
	wake_lock_timeout(&ilitek_wake_lock, 5 * HZ);
#endif

	input_report_key(input, KEY_POWER, 1);
	input_sync(input);
	input_report_key(input, KEY_POWER, 0);
	input_sync(input);

	ts->system_suspend = false;
}

static void ilitek_report_touch_event(uint8_t cnt, struct touch_fmt touch[40],
				      void *_private)
{
	struct input_dev *input = ts->input_dev;
	uint8_t i, released_cnt = 0;

	uint16_t x_max = ts->dev->screen_info.x_max;
	uint16_t x_min = ts->dev->screen_info.x_min;
	uint16_t y_max = ts->dev->screen_info.y_max;
	uint16_t y_min = ts->dev->screen_info.y_min;

	/*
	 * ISR may be activated after registering irq and
	 * before creating input_dev
	 */
	if (!input) {
		TP_ERR(NULL, "input_dev is not registerred\n");
		return;
	}

	for (i = 0; i < cnt; i++) {
		if (!touch[i].status) {
			released_cnt++;
#ifdef ILITEK_TOUCH_PROTOCOL_B
			ilitek_touch_release(touch[i].id);
#endif
			continue;
		}

		set_arr(ts->touch_flag, touch[i].id, 1);

		touch[i].x = (ILITEK_REVERT_X) ?
			x_max - touch[i].x + x_min : touch[i].x;
		touch[i].y = (ILITEK_REVERT_Y) ?
			y_max - touch[i].y + y_min : touch[i].y;

#if ILITEK_ROTATE_FLAG
	swap(touch[i].x, touch[i].y);
#endif

#if 1
	touch[i].y = (uint16_t)((touch[i].y / 600.0) * 480);
#endif

		if (ts->system_suspend) {
			TP_MSG(NULL, "system is suspend not report point\n");
			ilitek_gesture_handle(true, i, touch[i].x, touch[i].y);
			continue;
		}

		if (!ts->is_touched)
			ilitek_check_key_down(touch[i].x, touch[i].y);

		if (!ts->touch_key_hold_press) {
			ts->is_touched = true;
			ilitek_touch_down(touch[i].id, touch[i].x,
					  touch[i].y, touch[i].p,
					  touch[i].h, touch[i].w);
		} else {
			ilitek_check_key_release(touch[i].x, touch[i].y, 1);
		}
	}

	if (cnt == released_cnt) {
		if (ts->is_touched)
			ilitek_touch_release_all_point();

		ilitek_check_key_release(0, 0, 0);
		ts->is_touched = false;

		if (ts->system_suspend)
			ilitek_gesture_handle(false, 0, 0, 0);
	}

	input_sync(input);
}

static void ilitek_report_pen_event(struct pen_fmt *pen, void *_private)
{
	static int curr_tool = BTN_TOOL_PEN;
	struct input_dev *pen_input = ts->pen_input_dev;
	int tool;

	uint16_t x_max = ts->dev->screen_info.pen_x_max;
	uint16_t x_min = ts->dev->screen_info.pen_x_min;
	uint16_t y_max = ts->dev->screen_info.pen_y_max;
	uint16_t y_min = ts->dev->screen_info.pen_y_min;

	if (!pen_input)
		return;

	tool = (pen->in_range && pen->invert) ? BTN_TOOL_RUBBER : BTN_TOOL_PEN;

	if (curr_tool != tool) {
		input_report_key(pen_input, curr_tool, 0);
		input_sync(pen_input);
		curr_tool = tool;
	}

	pen->x = (ILITEK_REVERT_X) ? x_max - pen->x + x_min : pen->x;
	pen->y = (ILITEK_REVERT_Y) ? y_max - pen->y + y_min : pen->y;

#if ILITEK_ROTATE_FLAG
	swap(pen->x, pen->y);
#endif

	input_report_key(pen_input, BTN_TOUCH, pen->tip_sw || pen->eraser);
	input_report_key(pen_input, curr_tool, pen->in_range);
	input_report_key(pen_input, BTN_STYLUS, pen->barrel_sw);
	input_event(pen_input, EV_ABS, ABS_X, pen->x);
	input_event(pen_input, EV_ABS, ABS_Y, pen->y);
	input_event(pen_input, EV_ABS, ABS_PRESSURE, pen->pressure);
	input_event(pen_input, EV_ABS, ABS_TILT_X, pen->x_tilt);
	input_event(pen_input, EV_ABS, ABS_TILT_Y, pen->y_tilt);

	input_sync(pen_input);
}

static void ilitek_report_buf(uint8_t *buf, int size, void *_private)
{
	ilitek_udp_reply(buf, size);
}

int ilitek_read_data_and_report(void)
{
	int error;
	uint8_t i, count;
	struct ilitek_report report;

	memset(&report, 0, sizeof(report));
	report.cb.report_touch_event = ilitek_report_touch_event;
	report.cb.report_pen_event = ilitek_report_pen_event;
	report.cb.report_buf = ilitek_report_buf;

	switch (ts->irq_handle_type) {
	case irq_type_c_model:
		memset(ts->buf, 0, sizeof(ts->buf));
		
		for (i = 0, count = 1; i < count; i++) {
			ilitek_read(ts->buf, ts->irq_read_len);
			ilitek_udp_reply(ts->buf, ts->irq_read_len);
			count = ts->buf[ts->irq_read_len - 1];
		}

		break;

	case irq_type_debug:
	case irq_type_normal:
	default:
		if ((error = ilitek_update_report(ts->dev, &report)) < 0) {
			if (ts->is_touched) {
				ilitek_touch_release_all_point();
				ilitek_check_key_release(0, 0, 0);
			}

			return error;
		}

		break;
	}

	return 0;
}

static ISR_FUNC(ilitek_i2c_isr)
{
	int error;

	TP_DBG(NULL, "%s\n", __func__);

	atomic_set(&ts->get_INT, 1);
	ilitek_gpio_dbg();

	ts->esd_skip = true;

	if (atomic_read(&ts->firmware_updating)) {
		TP_DBG(NULL, "firmware_updating return\n");
		goto exit;
	}

#ifdef ILITEK_ISR_PROTECT
	ilitek_irq_disable();
#endif

	if (!ts->unhandle_irq) {
		mutex_lock(&ts->ilitek_mutex);
		if ((error = ilitek_read_data_and_report()) < 0)
			TP_ERR(NULL, "process error\n");
		mutex_unlock(&ts->ilitek_mutex);
	}

#ifdef ILITEK_ISR_PROTECT
	ilitek_irq_enable();
#endif

exit:
	ts->esd_skip = false;

	ISR_RETURN(IRQ_HANDLED);
}

static int ilitek_request_irq(void)
{
	int error;

#ifdef MTK_UNDTS
	mt_set_gpio_mode(ILITEK_IRQ_GPIO, GPIO_CTP_EINT_PIN_M_EINT);
	mt_set_gpio_dir(ILITEK_IRQ_GPIO, GPIO_DIR_IN);
	mt_set_gpio_pull_enable(ILITEK_IRQ_GPIO, GPIO_PULL_ENABLE);
	mt_set_gpio_pull_select(ILITEK_IRQ_GPIO, GPIO_PULL_UP);

	mt_eint_set_hw_debounce(CUST_EINT_TOUCH_PANEL_NUM, CUST_EINT_TOUCH_PANEL_DEBOUNCE_CN);
	mt_eint_registration(CUST_EINT_TOUCH_PANEL_NUM, CUST_EINT_TOUCH_PANEL_TYPE, ilitek_i2c_isr, 1);
	mt_eint_unmask(CUST_EINT_TOUCH_PANEL_NUM);
#else

#if ILITEK_PLAT == ILITEK_PLAT_MTK
	struct device_node *node;

	node = of_find_matching_node(NULL, touch_of_match);
	if (node)
		ts->irq = irq_of_parse_and_map(node, 0);
#else
	ts->irq = gpio_to_irq(ts->irq_gpio);
#endif

	TP_MSG(NULL, "ts->irq: %d\n", ts->irq);
	if (ts->irq <= 0)
		return -EINVAL;

	error = request_threaded_irq(ts->irq, NULL, ilitek_i2c_isr,
				     ts->irq_trigger_type | IRQF_ONESHOT,
				     "ilitek_touch_irq", ts);
	if (error) {
		TP_ERR(NULL, "request threaded irq failed, err: %d\n", error);
		return error;
	}
#endif

	ts->irq_registerred = true;
	atomic_set(&ts->irq_enabled, 1);

	return 0;
}

static int ilitek_read_fw(char *filename, unsigned char *buf, int size, void *data)
{
	int error, fw_size;
	const struct firmware *fw;
	struct device *device = (struct device *)data;

	if ((error = request_firmware(&fw, filename, device))) {
		TP_ERR(NULL, "request fw: %s failed, err:%d\n", filename, error);
		return error;
	}

	if (size < fw->size) {
		fw_size = -EFBIG;
		goto release_fw;
	}

	fw_size = fw->size;
	memcpy(buf, fw->data, fw->size);

release_fw:
	release_firmware(fw);

	return fw_size;
}

struct ilitek_update_callback update_cb = {
	.read_fw = ilitek_read_fw,
	.update_progress = NULL,
	.update_fw_file_info = NULL,

	.slave_update_notify = NULL,
	.update_fw_ic_info = NULL,
};

int ilitek_upgrade_firmware(char *filename)
{
	int error;
	struct ilitek_fw_handle *handle;
	struct ilitek_fw_settings setting;

	ilitek_irq_disable();
	mutex_lock(&ts->ilitek_mutex);
	atomic_set(&ts->firmware_updating, 1);
	ts->operation_protection = true;

	handle = ilitek_update_init(ts->dev, &update_cb, ts->device);

	setting.force_update = false;
	setting.fw_check_only = false;
	setting.fw_ver_check = false;
	setting.retry = 3;
	ilitek_update_setting(handle, &setting);

	if ((error = ilitek_update_load_fw(handle, filename)) < 0 ||
	    (error = ilitek_update_start(handle)) < 0)
		goto err_return;

err_return:
	ilitek_update_exit(handle);

	ts->operation_protection = false;
	atomic_set(&ts->firmware_updating, 0);
	mutex_unlock(&ts->ilitek_mutex);
	ilitek_irq_enable();

	return error;
}


static int __maybe_unused ilitek_update_thread(void *arg)
{
#ifdef ILITEK_BOOT_UPDATE
	int error;

	TP_MSG(NULL, "%s\n", __func__);

	if (kthread_should_stop()) {
		TP_MSG(NULL, "ilitek_update_thread, stop\n");
		return -1;
	}

	mdelay(100);

	if ((error = ilitek_upgrade_firmware("ilitek.ili")) < 0 &&
	    (error = ilitek_upgrade_firmware("ilitek.hex")) < 0 &&
	    (error = ilitek_upgrade_firmware("ilitek.bin")) < 0)
		return error;

	error = ilitek_request_input_dev();
	if (error)
		return (error < 0) ? error : -EFAULT;
#endif

	return 0;
}

void ilitek_suspend(void)
{
	TP_MSG(NULL, "%s\n", __func__);

	ts->esd_skip = true;
	if (ts->esd_check && ts->esd_workq)
		cancel_delayed_work_sync(&ts->esd_work);

	if (ts->operation_protection || atomic_read(&ts->firmware_updating)) {
		TP_MSG(NULL, "operation_protection or firmware_updating return\n");
		return;
	}

	if (ts->gesture_status) {
		ts->wake_irq_enabled = (enable_irq_wake(ts->irq) == 0);

		if (ts->low_power_status == Low_Power_Idle) {
			mutex_lock(&ts->ilitek_mutex);
			if (api_set_idle(ts->dev, true) < 0)
				TP_ERR(NULL, "enable Idle mode failed\n");
			mutex_unlock(&ts->ilitek_mutex);
		}
	} else {
		/*
		 * Must disable irq before sleep cmd,
		 * Avoid getting into ISR handling (and do i2c read),
		 * after sending sleep cmd.
		 */
		ilitek_irq_disable();
	
		if (ts->low_power_status == Low_Power_Sleep) {
			mutex_lock(&ts->ilitek_mutex);
			if (api_protocol_set_cmd(ts->dev, SET_IC_SLEEP,
						 NULL) < 0)
				TP_ERR(NULL, "set tp sleep failed\n");
			mutex_unlock(&ts->ilitek_mutex);
		}
	}

	ts->system_suspend = true;
}

void ilitek_resume(void)
{
	TP_MSG(NULL, "%s\n", __func__);

	if (ts->operation_protection || atomic_read(&ts->firmware_updating)) {
		TP_MSG(NULL, "operation_protection or firmware_updating return\n");
		return;
	}

	if (ts->gesture_status) {
		ilitek_irq_disable();

		if (ts->low_power_status == Low_Power_Idle) {
			mutex_lock(&ts->ilitek_mutex);
			api_set_idle(ts->dev, false);
			mutex_unlock(&ts->ilitek_mutex);
		}

		if (ts->gesture_status == Gesture_Double_Click)
			finger_state = 0;

		if (ts->wake_irq_enabled) {
			disable_irq_wake(ts->irq);
			ts->wake_irq_enabled = false;
		}
	} else {
		if (ts->dev->protocol.flag == PTL_V3) {
			/*
			 * If ILITEK_SLEEP is defined and FW support wakeup cmd,
			 * the hw reset can be mark.
			 */
		 	ilitek_reset(ts->dev->reset_time);
		}

		if (ts->low_power_status == Low_Power_Sleep) {
			mutex_lock(&ts->ilitek_mutex);
			api_protocol_set_cmd(ts->dev, SET_IC_WAKE, NULL);
			mutex_unlock(&ts->ilitek_mutex);
		}
	}

	ts->esd_skip = false;
	if (ts->esd_check && ts->esd_workq)
		queue_delayed_work(ts->esd_workq, &ts->esd_work, ts->esd_delay);

	ilitek_touch_release_all_point();
	ilitek_check_key_release(0, 0, 0);

	ts->system_suspend = false;

	ilitek_irq_enable();
}

#if ILITEK_PLAT == ILITEK_PLAT_ALLWIN
int ilitek_suspend_allwin(struct i2c_client *client, pm_message_t mesg)
{
	ilitek_suspend();
	return 0;
}

int ilitek_resume_allwin(struct i2c_client *client)
{
	ilitek_resume();
	return 0;
}
#endif

#if ILITEK_PLAT != ILITEK_PLAT_MTK
#if defined(CONFIG_FB) || defined(CONFIG_QCOM_DRM)
static int __maybe_unused ilitek_notifier_callback(struct notifier_block *self,
		unsigned long event, void *data) {
#ifdef CONFIG_QCOM_DRM
	struct msm_drm_notifier *ev_data = data;
#else
	struct fb_event *ev_data = data;
#endif
	int *blank;
	TP_MSG(NULL, "FB EVENT event: %lu\n", event);

#ifdef CONFIG_QCOM_DRM
	if (!ev_data || (ev_data->id != 0))
		return 0;
#endif
	if (ev_data && ev_data->data && event == ILITEK_EVENT_BLANK) {
		blank = ev_data->data;
		TP_MSG(NULL, "blank: %d\n", *blank);
		if (*blank == ILITEK_BLANK_POWERDOWN) {
			ilitek_suspend();
		}
		else if (*blank == ILITEK_BLANK_UNBLANK || *blank == ILITEK_BLANK_NORMAL) {
			ilitek_resume();
		}
	}

	return 0;
}
#elif defined(CONFIG_HAS_EARLYSUSPEND)
static void __maybe_unused ilitek_early_suspend(struct early_suspend *h)
{
	ilitek_suspend();
}

static void __maybe_unused ilitek_late_resume(struct early_suspend *h)
{
	ilitek_resume();
}
#endif
#endif

static void ilitek_get_gpio_num(void)
{
#ifdef ILITEK_GET_GPIO_NUM
#if ILITEK_PLAT == ILITEK_PLAT_ALLWIN
	TP_MSG(NULL, "(config_info.wakeup_gpio.gpio) = %d (config_info.int_number) = %d\n", (config_info.wakeup_gpio.gpio), (config_info.int_number));
	ts->reset_gpio = (config_info.wakeup_gpio.gpio);
	ts->irq_gpio = (config_info.int_number);
#else
#ifdef CONFIG_OF
	ts->reset_gpio = of_get_named_gpio(ts->device->of_node, "ilitek,reset-gpio", 0);
	if (ts->reset_gpio < 0)
		TP_ERR(NULL, "reset_gpio = %d\n", ts->reset_gpio);
	ts->irq_gpio = of_get_named_gpio(ts->device->of_node, "ilitek,irq-gpio", 0);
	if (ts->irq_gpio < 0)
		TP_ERR(NULL, "irq_gpio = %d\n", ts->irq_gpio);
#endif
#endif
#else
	ts->reset_gpio = ILITEK_RESET_GPIO;
	ts->irq_gpio = ILITEK_IRQ_GPIO;
#endif

	TP_MSG(NULL, "reset_gpio = %d irq_gpio = %d\n", ts->reset_gpio, ts->irq_gpio);


#if defined(ILITEK_GPIO_DEBUG)
	do {
		ts->test_gpio = of_get_named_gpio(ts->device->of_node, "ilitek,test-gpio", 0);
		if (ts->test_gpio < 0) {
			TP_ERR(NULL, "test_gpio: %d\n", ts->test_gpio);
			break;
		}

		TP_MSG(NULL, "test_gpio: %d\n", ts->test_gpio);

		if (gpio_request(ts->test_gpio, "ilitek-test-gpio")) {
			TP_ERR(NULL, "request test_gpio failed\n");
			break;
		}

		gpio_direction_output(ts->test_gpio, 1);

	} while (0);
#endif
}

static int ilitek_request_gpio(void)
{
	int ret = 0;

	ts->irq_gpio = -ENODEV;
	ts->reset_gpio = -ENODEV;

	ilitek_get_gpio_num();

#if ILITEK_PLAT != ILITEK_PLAT_MTK
	if (ts->reset_gpio > 0) {
		ret = gpio_request(ts->reset_gpio, "ilitek-reset-gpio");
		if (ret) {
			TP_ERR(NULL, "Failed to request reset_gpio so free retry\n");
			gpio_free(ts->reset_gpio);
			ret = gpio_request(ts->reset_gpio, "ilitek-reset-gpio");
			if (ret)
				TP_ERR(NULL, "Failed to request reset_gpio\n");
		}
		if (ret) {
			TP_ERR(NULL, "Failed to request reset_gpio\n");
		} else {
			ret = gpio_direction_output(ts->reset_gpio, 1);
			if (ret)
				TP_ERR(NULL, "Failed to direction output rest gpio err\n");
		}
	}
	if (ts->irq_gpio > 0) {
		ret = gpio_request(ts->irq_gpio, "ilitek-irq-gpio");
		if (ret) {
			TP_ERR(NULL, "Failed to request irq_gpio so free retry\n");
			gpio_free(ts->irq_gpio);
			ret = gpio_request(ts->irq_gpio, "ilitek-irq-gpio");
			if (ret)
				TP_ERR(NULL, "Failed to request irq_gpio\n");
		}
		if (ret) {
			TP_ERR(NULL, "Failed to request irq_gpio\n");
		} else {
			ret = gpio_direction_input(ts->irq_gpio);
			if (ret)
				TP_ERR(NULL, "Failed to direction input irq gpio err\n");
		}
	}
#endif
	return ret;
}

int ilitek_create_esd_check_workqueue(void)
{
	TP_MSG(NULL, "start to create esd workqueue\n");

	INIT_DELAYED_WORK(&ts->esd_work, ilitek_esd_check);
	ts->esd_workq = create_singlethread_workqueue("ilitek_esd_wq");
	if (!ts->esd_workq)
		return -ENOMEM;

	ts->esd_skip = false;
	ts->esd_delay = 2 * HZ;
	queue_delayed_work(ts->esd_workq, &ts->esd_work, ts->esd_delay);

	return 0;
}

void ilitek_remove_esd_check_workqueue(void)
{
	TP_MSG(NULL, "start to remove esd workqueue\n");

	if (ts->esd_workq) {
		cancel_delayed_work_sync(&ts->esd_work);
		destroy_workqueue(ts->esd_workq);
		ts->esd_workq = NULL;
	}
}

static int ilitek_register_resume_suspend(void)
{
#ifdef ILITEK_REGISTER_SUSPEND_RESUME
	int error;
#if ILITEK_PLAT != ILITEK_PLAT_MTK
#if defined(CONFIG_FB) || defined(CONFIG_QCOM_DRM)
	ts->fb_notif.notifier_call = ilitek_notifier_callback;

#ifdef CONFIG_QCOM_DRM
	error = msm_drm_register_client(&ts->fb_notif);
#else
	error = fb_register_client(&ts->fb_notif);
#endif
	if (error)
		TP_ERR(NULL, "register fb_notifier failed, err: %d\n", error);

#elif defined(CONFIG_HAS_EARLYSUSPEND)
	ts->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	ts->early_suspend.suspend = ilitek_early_suspend;
	ts->early_suspend.resume = ilitek_late_resume;
	register_early_suspend(&ts->early_suspend);
#endif
#endif /* ILITEK_PLAT != ILITEK_PLAT_MTK */

#if ILITEK_PLAT == ILITEK_PLAT_ALLWIN
	device_enable_async_suspend(ts->device);
	pm_runtime_set_active(ts->device);
	pm_runtime_get(ts->device);
	pm_runtime_enable(ts->device);
#endif

#endif /* ILITEK_REGISTER_SUSPEND_RESUME */

	return 0;
}

static void __maybe_unused ilitek_release_resume_suspend(void)
{
#ifdef ILITEK_REGISTER_SUSPEND_RESUME

#if defined(CONFIG_FB) || defined(CONFIG_QCOM_DRM)
#ifdef CONFIG_QCOM_DRM
	msm_drm_unregister_client(&ts->fb_notif);
#else
	fb_unregister_client(&ts->fb_notif);
#endif
#elif defined(CONFIG_HAS_EARLYSUSPEND)
	unregister_early_suspend(&ts->early_suspend);
#endif

#endif /* ILITEK_REGISTER_SUSPEND_RESUME */
}

static int ilitek_init_netlink(void)
{
#ifdef ILITEK_TUNING_MESSAGE
	NETLINK_KERNEL_CFG_DECLARE(cfg, udp_receive);

	ilitek_netlink_sock = NETLINK_KERNEL_CREATE(NETLINK_USERSOCK, &cfg, 
						    udp_receive);

	if (!ilitek_netlink_sock)
		TP_ERR(NULL, "netlink_kernel_create failed\n");

	ilitek_debug_flag = false;
#endif
	return 0;
}

static int __maybe_unused ilitek_alloc_dma(void)
{
#ifdef ILITEK_ENABLE_DMA
	tpd->dev->dev.coherent_dma_mask = DMA_BIT_MASK(32);
	I2CDMABuf_va = (u8 *) dma_alloc_coherent(&tpd->dev->dev, ILITEK_DMA_SIZE, &I2CDMABuf_pa, GFP_KERNEL);
	if (!I2CDMABuf_va) {
		TP_ERR(NULL, "ilitek [TPD] tpd->dev->dev dma_alloc_coherent error\n");
		I2CDMABuf_va = (u8 *) dma_alloc_coherent(NULL, ILITEK_DMA_SIZE, &I2CDMABuf_pa, GFP_KERNEL);
		if (!I2CDMABuf_va) {
			TP_ERR(NULL, "ilitek [TPD] NULL dma_alloc_coherent error\n");
			return -ENOMEM;
		}
	}
	memset(I2CDMABuf_va, 0, ILITEK_DMA_SIZE);
#endif

	return 0;
}

static int __maybe_unused ilitek_free_dma(void)
{
#ifdef ILITEK_ENABLE_DMA
	if (I2CDMABuf_va) {
		dma_free_coherent(&tpd->dev->dev, ILITEK_DMA_SIZE,
				  I2CDMABuf_va, I2CDMABuf_pa);

		I2CDMABuf_va = NULL;
		I2CDMABuf_pa = 0;

	}
#endif
	return 0;
}

static int __maybe_unused ilitek_power_on(bool status)
{
#ifdef ILITEK_ENABLE_REGULATOR_POWER_ON
	int error;

	TP_MSG(NULL, "%s\n", status ? "POWER ON" : "POWER OFF");

#if ILITEK_PLAT == ILITEK_PLAT_ALLWIN
	input_set_power_enable(&(config_info.input_type), status);
#else

	if (status) {
		if (ts->vdd && (error = regulator_enable(ts->vdd)) < 0) {
			TP_ERR(NULL, "regulator_enable vdd fail\n");
			return error;
		}
		if (ts->vdd_i2c &&
		    (error = regulator_enable(ts->vdd_i2c)) < 0) {
			TP_ERR(NULL, "regulator_enable vdd_i2c fail\n");
			return error;
		}
	} else {
		if (ts->vdd && (error = regulator_disable(ts->vdd)) < 0) {
			TP_ERR(NULL, "regulator_enable vdd fail\n");
			return error;
		}
		if (ts->vdd_i2c &&
		    (error = regulator_disable(ts->vdd_i2c)) < 0) {
			TP_ERR(NULL, "regulator_enable vdd_i2c fail\n");
			return error;
		}
	}

#ifdef MTK_UNDTS
	if (status)
		hwPowerOn(PMIC_APP_CAP_TOUCH_VDD, VOL_3300, "TP");
#endif
#endif
#endif

	return 0;
}

static int __maybe_unused ilitek_request_regulator(struct ilitek_ts_data *ts)
{
#ifdef ILITEK_ENABLE_REGULATOR_POWER_ON
	int ret = 0;
	char *vdd_name = "vdd";
	char *vcc_i2c_name = "vcc_i2c";

#if ILITEK_PLAT == ILITEK_PLAT_MTK
	vdd_name = "vtouch";
	ts->vdd = regulator_get(tpd->tpd_dev, vdd_name);
	tpd->reg = ts->vdd;
	if (IS_ERR(ts->vdd)) {
		TP_ERR(NULL, "regulator_get vdd fail\n");
		ts->vdd = NULL;
	} else {
		ret = regulator_set_voltage(ts->vdd, 3000000, 3300000);
		if (ret)
			TP_ERR(NULL, "Could not set vdd to 3000~3300mv.\n");
	}
#elif ILITEK_PLAT != ILITEK_PLAT_ALLWIN
	ts->vdd = regulator_get(ts->device, vdd_name);
	if (IS_ERR(ts->vdd)) {
		TP_ERR(NULL, "regulator_get vdd fail\n");
		ts->vdd = NULL;
	} else {
		ret = regulator_set_voltage(ts->vdd, 3000000, 3300000);
		if (ret)
			TP_ERR(NULL, "Could not set vdd to 3000~3300mv.\n");

	}

	ts->vdd_i2c = regulator_get(ts->device, vcc_i2c_name);
	if (IS_ERR(ts->vdd_i2c)) {
		TP_ERR(NULL, "regulator_get vdd_i2c fail\n");
		ts->vdd_i2c = NULL;
	} else {
		ret = regulator_set_voltage(ts->vdd_i2c, 3000000, 3300000);
		if (ret)
			TP_ERR(NULL, "Could not set i2c to 3000~3300mv.\n");
	}
#endif /* ILITEK_PLAT == ILITEK_PLAT_MTK */
#endif /* ILITEK_ENABLE_REGULATOR_POWER_ON */

	return 0;
}

static void __maybe_unused ilitek_release_regulator(void)
{
#if defined(ILITEK_ENABLE_REGULATOR_POWER_ON) && ILITEK_PLAT != ILITEK_PLAT_ALLWIN
	if (ts->vdd)
		regulator_put(ts->vdd);
	if (ts->vdd_i2c)
		regulator_put(ts->vdd_i2c);
#endif
}

void ilitek_register_gesture(struct ilitek_ts_data *ts, bool init)
{
	if (init) {
		device_init_wakeup(ts->device, 1);

#ifdef ILITEK_WAKELOCK_SUPPORT
		wake_lock_init(&ilitek_wake_lock, WAKE_LOCK_SUSPEND, "ilitek wakelock");
#endif
		return;
	}

	device_init_wakeup(ts->device, 0);

#ifdef ILITEK_WAKELOCK_SUPPORT
	wake_lock_destroy(&ilitek_wake_lock);
#endif
}

static int _ilitek_write_then_read(unsigned char *wbuf, int wlen,
				   unsigned char *rbuf, int rlen, void *data)
{
	return ilitek_write_and_read(wbuf, wlen, 1, rbuf, rlen);
}

static int ilitek_read_interrupt_in(unsigned char *rbuf, int rlen,
				    unsigned int timeout_ms, void *data)
{
	return ilitek_write_and_read(NULL, 0, 0, rbuf, rlen);
}

static void _ilitek_init_ack(void *data)
{
	ilitek_irq_enable();
	ts->unhandle_irq = true;
	atomic_set(&ts->get_INT, 0);
}

static int _ilitek_wait_ack(uint8_t cmd, unsigned int tout_ms, void *data)
{
	unsigned int t_ms = 0;
	int tmp, error = -ETIME;

	UNUSED(cmd);

	do {
		if ((tmp = atomic_read(&ts->get_INT))) {
			error = 0;
			break;
		}

		udelay(1000);
		t_ms++;
	} while (t_ms < tout_ms);

	ts->unhandle_irq = false;
	ilitek_irq_disable();

	return error;
}

static void _ilitek_delay(unsigned int delay_ms)
{
	mdelay(delay_ms);
}

static int _ilitek_reset(unsigned int delay_ms, void *data)
{
	/* return error if no reset gpio found */
	if (ts->reset_gpio < 0)
		return -ENODEV;

	ilitek_reset(delay_ms);
	return 0;
}

struct ilitek_ts_callback dev_cb = {
	.write_then_read = _ilitek_write_then_read,
	.read_interrupt_in = ilitek_read_interrupt_in,
	.init_ack = _ilitek_init_ack,
	.wait_ack = _ilitek_wait_ack,
	.hw_reset = _ilitek_reset,
	.re_enum = NULL,
	.delay_ms = _ilitek_delay,
	.msg = NULL,

	.write_direct = NULL,
	.mode_switch_notify = NULL,
};

int ilitek_main_probe(void *client, struct device *device)
{
	struct ilitek_ts_settings setting;

	TP_MSG(NULL, "driver version: %hhu.%hhu.%hhu.%hhu.%hhu.%hhu.%hhu\n",
		driver_ver[0], driver_ver[1], driver_ver[2], driver_ver[3],
		driver_ver[4], driver_ver[5], driver_ver[6]);

	if (!(ts = kzalloc(sizeof(*ts), GFP_KERNEL))) {
		TP_ERR(NULL, "allocate ts failed\n");
		return -ENOMEM;
	}

	ts->client = client;
	ts->device = device;

	mutex_init(&ts->ilitek_mutex);
	ts->unhandle_irq = false;

	ilitek_alloc_dma();
	ilitek_request_regulator(ts);
	ilitek_power_on(true);
	ilitek_request_gpio();

	ilitek_reset(600);

	ts->dev = ilitek_dev_init(interface_i2c, "0", &dev_cb, ts);
	if (!ts->dev)
		goto err_free_gpio;

	memset(&setting, 0, sizeof(setting));
	setting.sensor_id_mask = ILITEK_SENSOR_ID_MASK;
	ilitek_dev_setting(ts->dev, &setting);

	if (api_update_ts_info(ts->dev) < 0)
		goto err_dev_exit;

	ts->irq_trigger_type = (ts->dev->protocol.flag == PTL_V6) ?
		IRQF_TRIGGER_RISING : IRQF_TRIGGER_FALLING;

	if (ilitek_request_irq())
		goto err_dev_exit;

#ifdef ILITEK_BOOT_UPDATE
	ts->update_thread = kthread_run(ilitek_update_thread, NULL,
					"ilitek_update_thread");
	if (IS_ERR(ts->update_thread))
		goto err_free_irq;
#else
	if (ilitek_request_input_dev())
		goto err_free_irq;
#endif

	ilitek_register_resume_suspend();
	ilitek_create_sysfsnode();
	ilitek_create_tool_node();
	ilitek_init_netlink();

	if ((ts->esd_check = ILITEK_ESD_CHECK_ENABLE))
		ilitek_create_esd_check_workqueue();

	if ((ts->gesture_status = ILITEK_GESTURE_DEFAULT))
		ilitek_register_gesture(ts, true);

	ts->low_power_status = ILITEK_LOW_POWER_DEFAULT;

	return 0;

err_free_irq:
	free_irq(ts->irq, ts);

err_dev_exit:
	ilitek_dev_exit(ts->dev);

err_free_gpio:
	ilitek_free_gpio();
	ilitek_power_on(false);
	ilitek_release_regulator();
	ilitek_free_dma();
	kfree(ts);

	return -ENODEV;
}

int ilitek_main_remove(void *client)
{
	TP_MSG(NULL, "%s\n", __func__);

	if (!ts)
		return 0;

	if (ts->gesture_status)
		ilitek_register_gesture(ts, false);

	ilitek_remove_esd_check_workqueue();

#ifdef ILITEK_TUNING_MESSAGE
	if (ilitek_netlink_sock)
		netlink_kernel_release(ilitek_netlink_sock);
#endif

	ilitek_remove_tool_node();
	ilitek_remove_sys_node();
	ilitek_release_resume_suspend();

	if (ts->pen_input_dev)
		input_unregister_device(ts->pen_input_dev);

	if (ts->input_dev)
		input_unregister_device(ts->input_dev);

#ifndef MTK_UNDTS
	free_irq(ts->irq, ts);
#endif

	ilitek_dev_exit(ts->dev);

	ilitek_free_gpio();
	ilitek_power_on(false);
	ilitek_release_regulator();
	ilitek_free_dma();

	kfree(ts);

	return 0;
}
