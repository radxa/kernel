// SPDX-License-Identifier: GPL-2.0
/*
 * This file is part of ILITEK CommonFlow
 *
 * Copyright (c) 2022 ILI Technology Corp.
 * Copyright (c) 2022 Luca Hsu <luca_hsu@ilitek.com>
 * Copyright (c) 2022 Joe Hung <joe_hung@ilitek.com>
 */

#include "ilitek_protocol.h"

typedef int (*protocol_func_t)(struct ilitek_ts_device *, void *);

struct protocol_map {
	uint8_t cmd;
	uint8_t flag;
	protocol_func_t func;
	const char *desc;
};

#define X(_cmd, _protocol, _cmd_id, _api) \
	static int _api(struct ilitek_ts_device *, void *);
ILITEK_CMD_MAP
#undef X

#define X(_cmd, _protocol, _cmd_id, _api) {_cmd, _protocol, _api, #_cmd_id},
struct protocol_map protocol_maps[] = { ILITEK_CMD_MAP };
#undef X

uint16_t le16(const uint8_t *p)
{
	return p[0] | p[1] << 8;
}

uint16_t be16(const uint8_t *p)
{
	return p[1] | p[0] << 8;
}

uint32_t le32(const uint8_t *p, int bytes)
{
	uint32_t val = 0;

	while (bytes--)
		val += (p[bytes] << (8 * bytes));

	return val;
}

uint32_t be32(const uint8_t *p, int bytes)
{
	uint32_t val = 0;

	while (bytes--)
		val = (val << 8) | (*p++);

	return val;
}

bool is_29xx(void *handle)
{
	struct ilitek_ts_device *dev = (struct ilitek_ts_device *)handle;
	
	if (!dev)
		return false;

	if (!strcmp(dev->mcu_info.ic_name, "2900") ||
	    !strcmp(dev->mcu_info.ic_name, "2901") ||
	    !strcmp(dev->mcu_info.ic_name, "2910") ||
	    !strcmp(dev->mcu_info.ic_name, "2911") ||
	    !strcmp(dev->mcu_info.ic_name, "2531") ||
	    !strcmp(dev->mcu_info.ic_name, "2532"))
		return true;

	return false;
}

bool is_231x(void *handle)
{
	struct ilitek_ts_device *dev = (struct ilitek_ts_device *)handle;

	if (!dev)
		return false;

	if (!strcmp(dev->mcu_info.ic_name, "2312") ||
	    !strcmp(dev->mcu_info.ic_name, "2315"))
		return true;

	return false;
}

void grid_reset(struct grids *grid)
{
	grid->mc.need_update = false;
	grid->sc_x.need_update = false;
	grid->sc_y.need_update = false;
	grid->pen_x.need_update = false;
	grid->pen_y.need_update = false;

	grid->key_mc.need_update = false;
	grid->key_x.need_update = false;
	grid->key_y.need_update = false;

	grid->self.need_update = false;

	if (grid->mc.data)
		memset(grid->mc.data, 0,
		       grid->mc.X * grid->mc.Y * sizeof(int32_t));
	if (grid->sc_x.data)
		memset(grid->sc_x.data, 0,
		       grid->sc_x.X * grid->sc_x.Y * sizeof(int32_t));
	if (grid->sc_y.data)
		memset(grid->sc_y.data, 0,
		       grid->sc_y.X * grid->sc_y.Y * sizeof(int32_t));
	if (grid->pen_x.data)
		memset(grid->pen_x.data, 0,
		       grid->pen_x.X * grid->pen_x.Y * sizeof(int32_t));
	if (grid->pen_y.data)
		memset(grid->pen_y.data, 0,
		       grid->pen_y.X * grid->pen_y.Y * sizeof(int32_t));

	if (grid->key_mc.data)
		memset(grid->key_mc.data, 0,
		       grid->key_mc.X * grid->key_mc.Y * sizeof(int32_t));
	if (grid->key_x.data)
		memset(grid->key_x.data, 0,
		       grid->key_x.X * grid->key_x.Y * sizeof(int32_t));
	if (grid->key_y.data)
		memset(grid->key_y.data, 0,
		       grid->key_y.X * grid->key_y.Y * sizeof(int32_t));

	if (grid->self.data)
		memset(grid->self.data, 0,
		       grid->self.X * grid->self.Y * sizeof(int32_t));

	grid->dmsg.pen_need_update = false;
	grid->dmsg.touch_need_update = false;
	memset(grid->dmsg.touch, 0, sizeof(grid->dmsg.touch));
	memset(grid->dmsg.pen, 0, sizeof(grid->dmsg.pen));
}

void grid_free(struct grids *grid)
{
	if (grid->mc.data)
		CFREE(grid->mc.data);
	if (grid->sc_x.data)
		CFREE(grid->sc_x.data);
	if (grid->sc_y.data)
		CFREE(grid->sc_y.data);
	if (grid->pen_x.data)
		CFREE(grid->pen_x.data);
	if (grid->pen_y.data)
		CFREE(grid->pen_y.data);

	if (grid->key_mc.data)
		CFREE(grid->key_mc.data);
	if (grid->key_x.data)
		CFREE(grid->key_x.data);
	if (grid->key_y.data)
		CFREE(grid->key_y.data);

	if (grid->self.data)
		CFREE(grid->self.data);
}

int grid_alloc(void *handle, struct grids *grid)
{
	struct ilitek_ts_device *dev = (struct ilitek_ts_device *)handle;
	int X, Y, key;

	memset(grid, 0, sizeof(*grid));

	if (!dev)
		return -EINVAL;

	X = dev->tp_info.x_ch;
	Y = dev->tp_info.y_ch;
	key = dev->tp_info.key_num;

	if (!(grid->mc.data = (int32_t *)CALLOC(X * Y, sizeof(int32_t))) ||
	    !(grid->sc_x.data = (int32_t *)CALLOC(X, sizeof(int32_t))) ||
	    !(grid->sc_y.data = (int32_t *)CALLOC(Y, sizeof(int32_t))) ||
	    !(grid->pen_x.data = (int32_t *)CALLOC(X * 8, sizeof(int32_t))) ||
	    !(grid->pen_y.data = (int32_t *)CALLOC(Y * 8, sizeof(int32_t))) ||
	    !(grid->key_mc.data = (int32_t *)CALLOC(key, sizeof(int32_t))) ||
	    !(grid->key_x.data = (int32_t *)CALLOC(key, sizeof(int32_t))) ||
	    !(grid->key_y.data = (int32_t *)CALLOC(1, sizeof(int32_t))) ||
	    !(grid->self.data = (int32_t *)CALLOC(4, sizeof(int32_t))))
		goto err_free;

	grid->mc.X = X; grid->mc.Y = Y;
	grid->sc_x.X = X; grid->sc_x.Y = 1;
	grid->sc_y.X = 1; grid->sc_y.Y = Y;
	grid->pen_x.X = X; grid->pen_x.Y = 8;
	grid->pen_y.X = 8; grid->pen_y.Y = Y;

	grid->key_mc.X = key; grid->key_mc.Y = 1;
	grid->key_x.X = key; grid->key_x.Y = 1;
	grid->key_y.X = 1; grid->key_y.Y = 1;

	grid->self.X = 4, grid->self.Y = 1;

	grid_reset(grid);

	return 0;

err_free:
	grid_free(grid);

	return -ENOMEM;
}

static uint16_t update_crc(uint16_t crc, uint8_t newbyte)
{
	char i;
	const uint16_t crc_poly = 0x8408;

	crc ^= newbyte;

	for (i = 0; i < 8; i++) {
		if (crc & 0x01)
			crc = (crc >> 1) ^ crc_poly;
		else
			crc = crc >> 1;
	}

	return crc;
}

uint16_t get_crc(uint32_t start, uint32_t end,
		 uint8_t *buf, uint32_t buf_size)
{
	uint16_t crc = 0;
	uint32_t i;

	if (end > buf_size || start > buf_size) {
		TP_WARN(NULL, "start/end addr: %#x/%#x buf size: %#x OOB\n",
			start, end, buf_size);
		return 0;
	}

	for (i = start; i < end && i < buf_size; i++)
		crc = update_crc(crc, buf[i]);

	return crc;
}

uint32_t get_checksum(uint32_t start, uint32_t end,
		      uint8_t *buf, uint32_t buf_size)
{
	uint32_t sum = 0;
	uint32_t i;

	if (end > buf_size || start > buf_size) {
		TP_WARN(NULL, "start/end addr: %#x/%#x buf size: %#x OOB\n",
			start, end, buf_size);
		return 0;
	}

	for (i = start; i < end && i < buf_size; i++)
		sum += buf[i];

	return sum;
}

bool support_sensor_id(void *handle)
{
	struct ilitek_ts_device *dev = (struct ilitek_ts_device *)handle;

	if ((dev->ic[0].mode == 0x55 && dev->protocol.ver < 0x010803) ||
	    (dev->ic[0].mode == 0x5a && dev->protocol.ver < 0x060004))
		return false;

	return true;
}

bool support_production_info(void *handle)
{
	struct ilitek_ts_device *dev = (struct ilitek_ts_device *)handle;

	if ((dev->ic[0].mode == 0x55 && dev->protocol.ver < 0x010803) ||
	    (dev->ic[0].mode == 0x5a && dev->protocol.ver < 0x060007))
		return false;

	return true;
}

bool support_fwid(void *handle)
{
	struct ilitek_ts_device *dev = (struct ilitek_ts_device *)handle;

	if ((dev->ic[0].mode == 0x55 && dev->protocol.ver < 0x010802) ||
	    (dev->ic[0].mode == 0x5a && dev->protocol.ver < 0x060007))
		return false;

	return true;
}

int reset_helper(void *handle)
{
	struct ilitek_ts_device *dev = (struct ilitek_ts_device *)handle;
	bool need_re_enum = true;

	if (dev->_interface == interface_i2c) {
		/* sw reset if no reset-gpio found */
		if (!dev->cb.hw_reset ||
		    dev->cb.hw_reset(dev->reset_time, dev->_private) < 0)
			return api_protocol_set_cmd(dev, SET_SW_RST,
						    &need_re_enum);

		return 0;
	}

	return api_protocol_set_cmd(dev, SET_SW_RST, &need_re_enum);
}

static int re_enum_helper(struct ilitek_ts_device *dev, uint8_t enum_type)
{
	int error;
	int retry = 5;

	if (!dev->cb.re_enum)
		return -EINVAL;

	do {
		if (!(error = dev->cb.re_enum(enum_type, dev->_private)))
			return 0;

		TP_WARN(dev->id, "re-enum failed, error: %d, retry: %d\n", error, retry);
		dev->cb.delay_ms(500);
	} while (!dev->setting.no_retry && retry--);

	TP_ERR(dev->id, "re-enum retry failed\n");

	return -ENODEV;
}

int read_interrupt_in(void *handle, uint8_t *buf, int rlen,
		      unsigned int timeout_ms)
{
	struct ilitek_ts_device *dev = (struct ilitek_ts_device *)handle;
	int error;

	if (!dev->cb.read_interrupt_in)
		return -EINVAL;

	if ((error = dev->cb.read_interrupt_in(buf, rlen, timeout_ms,
					       dev->_private)) < 0)
		return error;

	TP_PKT_ARR(dev->id, "[int-in]:", TYPE_U8, rlen, buf);

	return 0;
}

int write_then_read(void *handle, uint8_t *cmd, int wlen,
		    uint8_t *buf, int rlen)
{
	struct ilitek_ts_device *dev = (struct ilitek_ts_device *)handle;
	int error;

	if (!dev->cb.write_then_read)
		return -EINVAL;

	if (wlen > 0)
		TP_PKT_ARR(dev->id, "[wbuf]:", TYPE_U8, wlen, cmd);

	error = dev->cb.write_then_read(cmd, wlen, buf, rlen, dev->_private);

	if (rlen > 0)
		TP_PKT_ARR(dev->id, "[rbuf]:", TYPE_U8, rlen, buf);
	
	return (error < 0) ? error : 0;
}

int write_then_wait_ack(struct ilitek_ts_device *dev,
			uint8_t *cmd, int wlen, int timeout_ms)
{
	int error;
	struct ilitek_ts_callback *cb = &dev->cb;

	if (!dev->setting.no_INT_ack && cb->init_ack)
		cb->init_ack(dev->_private);

	if ((error = write_then_read(dev, cmd, wlen, NULL, 0)) < 0)
		return error;

	if (dev->setting.no_INT_ack)
		goto check_busy;

	/* cmd[0] should be ILITEK cmd code */
	if ((error = cb->wait_ack(cmd[0], timeout_ms, dev->_private)) < 0) {
		TP_WARN(dev->id, "wait ack %d ms timeout, err: %d\n", timeout_ms, error);
		goto check_busy;
	}

	return 0;

check_busy:
	/* 
	 * for no-INT-ack flow, add delay to prevent
	 * interrupting FW flow too soon, while FW should
	 * be handling previous write command. ex. 0xcd/ 0xc3
	 */
	cb->delay_ms(5);
	return api_check_busy(dev, timeout_ms, 100);
}

/* Common APIs */
static int api_protocol_get_scrn_res(struct ilitek_ts_device *dev, void *data)
{
	int error;
	struct ilitek_screen_info *screen_info;

	UNUSED(data);

	if ((error = write_then_read(dev, dev->wbuf, 1, dev->rbuf, 28)) < 0)
		return error;

	screen_info = (struct ilitek_screen_info *)dev->rbuf;

	dev->screen_info.x_min = screen_info->x_min;
	dev->screen_info.y_min = screen_info->y_min;
	dev->screen_info.x_max = screen_info->x_max;
	dev->screen_info.y_max = screen_info->y_max;

	TP_DBG(dev->id, "screen x: %hu~%hu, screen y: %hu~%hu\n",
		dev->screen_info.x_min, dev->screen_info.x_max,
		dev->screen_info.y_min, dev->screen_info.y_max);

	dev->screen_info.pressure_min = 0;
	dev->screen_info.pressure_max = 0;
	dev->screen_info.x_tilt_min = 0;
	dev->screen_info.x_tilt_max = 0;
	dev->screen_info.y_tilt_min = 0;
	dev->screen_info.y_tilt_max = 0;
	if (dev->protocol.ver > 0x60006) {
		dev->screen_info.pressure_min = screen_info->pressure_min;
		dev->screen_info.pressure_max = screen_info->pressure_max;
		dev->screen_info.x_tilt_min = screen_info->x_tilt_min;
		dev->screen_info.x_tilt_max = screen_info->x_tilt_max;
		dev->screen_info.y_tilt_min = screen_info->y_tilt_min;
		dev->screen_info.y_tilt_max = screen_info->y_tilt_max;

		dev->screen_info.pen_x_min = screen_info->pen_x_min;
		dev->screen_info.pen_y_min = screen_info->pen_y_min;
		dev->screen_info.pen_x_max = screen_info->pen_x_max;
		dev->screen_info.pen_y_max = screen_info->pen_y_max;
	}

	return 0;
}

static int api_protocol_get_tp_info_v3(struct ilitek_ts_device *dev, void *data)
{
	int error;
	struct ilitek_tp_info_v3 *tp_info;

	UNUSED(data);

	if ((error = write_then_read(dev, dev->wbuf, 1, dev->rbuf, 15)) < 0)
		return error;

	tp_info = (struct ilitek_tp_info_v3 *)dev->rbuf;
	dev->tp_info.x_resolution = tp_info->x_resolution;
	dev->tp_info.y_resolution = tp_info->y_resolution;
	dev->tp_info.x_ch = tp_info->x_ch;
	dev->tp_info.y_ch = tp_info->y_ch;
	dev->tp_info.max_fingers = tp_info->max_fingers;
	dev->tp_info.key_num = tp_info->key_num;

	dev->tp_info.support_modes = tp_info->support_modes;
	if (dev->tp_info.support_modes > 3 || !dev->tp_info.support_modes)
		dev->tp_info.support_modes = 1;

	return 0;
}

static int api_protocol_get_tp_info_v6(struct ilitek_ts_device *dev, void *data)
{
	int error;
	struct ilitek_tp_info_v6 *tp_info;
	uint8_t i;

#define X(_enum, _code, _name) {_code, _name},
	const struct {
		const int code;
		const char *str;
	} pen_modes[] = { STYLUS_MODES };
#undef X

	UNUSED(data);

	if ((error = write_then_read(dev, dev->wbuf, 1, dev->rbuf, 21)) < 0)
		return error;

	tp_info = (struct ilitek_tp_info_v6 *)dev->rbuf;
	dev->tp_info.x_resolution = tp_info->x_resolution;
	dev->tp_info.y_resolution = tp_info->y_resolution;
	dev->tp_info.x_ch = tp_info->x_ch;
	dev->tp_info.y_ch = tp_info->y_ch;
	dev->tp_info.max_fingers = tp_info->max_fingers;
	dev->tp_info.key_num = tp_info->key_num;
	dev->tp_info.ic_num = tp_info->ic_num;
	dev->tp_info.format = tp_info->format;
	dev->tp_info.support_modes = tp_info->support_modes;

	if (dev->protocol.ver > 0x60002) {
		dev->tp_info.block_num = tp_info->block_num;
		TP_MSG(dev->id, "[Panel Information] Block Number: %hhu\n",
			dev->tp_info.block_num);
	}

	if (dev->tp_info.ic_num > ARRAY_SIZE(dev->ic)) {
		TP_ERR(dev->id, "invalid ic_num: %hhu\n", dev->tp_info.ic_num);
		return -EINVAL;
	}
	TP_MSG(dev->id, "[Panel Information] Chip count: %u\n", dev->tp_info.ic_num);

	dev->tp_info.pen_modes = 0;
	memset(dev->pen_mode, 0, sizeof(dev->pen_mode));
	if (dev->protocol.ver > 0x60006) {
		dev->tp_info.pen_modes = tp_info->pen_modes;
		if (!dev->tp_info.pen_modes)
			_strcpy(dev->pen_mode, "Disable",
				sizeof(dev->pen_mode));
		for (i = 0; i < ARRAY_SIZE(pen_modes); i++) {
			if (!(tp_info->pen_modes & pen_modes[i].code))
				continue;
			_strcat(dev->pen_mode, pen_modes[i].str,
				sizeof(dev->pen_mode));
			_strcat(dev->pen_mode, ",",
				sizeof(dev->pen_mode));
		}

		TP_DBG(dev->id, "pen_modes: %hhu\n", dev->tp_info.pen_modes);
		TP_MSG(dev->id, "[Panel Information] Pen Mode: %s\n", dev->pen_mode);

		dev->tp_info.pen_format = tp_info->pen_format;
		dev->tp_info.pen_x_resolution = tp_info->pen_x_resolution;
		dev->tp_info.pen_y_resolution = tp_info->pen_y_resolution;
		TP_MSG(dev->id, "[Panel Information] Pen Format: 0x%hhx\n",
			dev->tp_info.pen_format);
		TP_MSG(dev->id, "[Panel Information] Pen X/Y resolution: %hu/%hu\n",
			dev->tp_info.pen_x_resolution,
			dev->tp_info.pen_y_resolution);
	}

	if (dev->tp_info.max_fingers > 40) {
		TP_ERR(dev->id, "invalid max tp: %d > 40\n",
			dev->tp_info.max_fingers);
		return -EINVAL;
	}

	if (dev->tp_info.format == 5)
		api_protocol_set_cmd(dev, GET_CRYPTO_INFO, NULL);

	return 0;
}

static int api_protocol_get_tp_info(struct ilitek_ts_device *dev, void *data)
{
	int error;

#define X(_enum, _id, _size, _cnt)	{_size, _cnt},
	const struct {
		const unsigned int size;
		const unsigned int max_cnt;
	} finger_fmts[] = { ILITEK_TOUCH_REPORT_FORMAT };
#undef X

	if (dev->protocol.flag == PTL_V3)
		error = api_protocol_get_tp_info_v3(dev, data);
	else if (dev->protocol.flag == PTL_V6)
		error = api_protocol_get_tp_info_v6(dev, data);
	else
		return -EINVAL;

	if (error < 0)
		return error;

	if (dev->tp_info.max_fingers > 40) {
		TP_ERR(dev->id, "invalid max fingers: %d > 40\n",
			dev->tp_info.max_fingers);
		return -EINVAL;
	}

	switch (dev->tp_info.format) {
	case touch_fmt_1:
	case touch_fmt_2:
	case touch_fmt_3:
		dev->finger.size = finger_fmts[dev->tp_info.format].size;
		dev->finger.max_cnt = finger_fmts[dev->tp_info.format].max_cnt;
		break;
	default:
	case touch_fmt_0:
		dev->finger.size = finger_fmts[touch_fmt_0].size;
		dev->finger.max_cnt = finger_fmts[touch_fmt_0].max_cnt;
		break;
	}

	TP_MSG(dev->id, "[Panel Information] X/Y resolution: %hu/%hu\n",
		dev->tp_info.x_resolution, dev->tp_info.y_resolution);
	TP_MSG(dev->id, "[Panel Information] X/Y channel: %hu/%hu\n",
		dev->tp_info.x_ch, dev->tp_info.y_ch);
	TP_MSG(dev->id, "[Panel Information] Support %hhu Fingers\n",
		dev->tp_info.max_fingers);
	TP_MSG(dev->id, "[Panel Information] Support %hhu Keys\n",
		dev->tp_info.key_num);
	
	TP_MSG(dev->id, "[Panel Information] Support %hhu modes\n",
			dev->tp_info.support_modes);

	TP_DBG(dev->id, "touch format: %hhu, size: %u bytes, max cnt: %u per packet\n",
 		dev->tp_info.format, dev->finger.size, dev->finger.max_cnt);

	if (dev->tp_info.key_num > 0 &&
	    (error = api_protocol_set_cmd(dev, GET_KEY_INFO, NULL)) < 0)
		return error;

	return 0;
}

static int api_protocol_get_key_info_v3(struct ilitek_ts_device *dev,
					void *data)
{
	int error;
	struct ilitek_key_info_v3 *key_info;
	unsigned int i;

	UNUSED(data);

	/* Only i2c interface has key for V3 */
	if (dev->_interface != interface_i2c)
		return 0;

	if (dev->tp_info.key_num > 20) {
		TP_ERR(dev->id, "key count: %hhu invalid\n", dev->tp_info.key_num);
		return -EINVAL;
	}

	if ((error = write_then_read(dev, dev->wbuf, 1, dev->rbuf, 29)) < 0)
		return error;

	for (i = 0; dev->tp_info.key_num > 5U &&
	     i < TP_DIV_ROUND_UP(dev->tp_info.key_num, 5U) - 1U; i++) {
		TP_MSG(dev->id, "read keyinfo again, i: %u\n", i);
		if ((error = write_then_read(dev, NULL, 0,
					     dev->rbuf + 29 + 5 * i,
					     25)) < 0)
			return error;
	}

	key_info = (struct ilitek_key_info_v3 *)dev->rbuf;
	dev->key.info.x_len = be16(key_info->x_len);
	dev->key.info.y_len = be16(key_info->y_len);
	TP_MSG(dev->id, "key_x_len: %hu, key_y_len: %hu\n",
		dev->key.info.x_len, dev->key.info.y_len);

	for (i = 0; i < dev->tp_info.key_num; i++) {
		dev->key.info.keys[i].id = key_info->keys[i].id;
		dev->key.info.keys[i].x = be16(key_info->keys[i].x);
		dev->key.info.keys[i].y = be16(key_info->keys[i].y);
		TP_MSG(dev->id, "key[%u] id: %hhu, x: %hu, y: %hu\n", i,
			dev->key.info.keys[i].id, dev->key.info.keys[i].x,
			dev->key.info.keys[i].y);
	}

	return 0;
}

static int api_protocol_get_key_info_v6(struct ilitek_ts_device *dev,
					void *data)
{
	int error;
	struct ilitek_key_info_v6 *key_info;
	unsigned int i;

	UNUSED(data);

	if (dev->tp_info.key_num > ARRAY_SIZE(dev->key.info.keys)) {
		TP_ERR(dev->id, "exception keycount %hhu > %d\n", dev->tp_info.key_num,
			(int)ARRAY_SIZE(dev->key.info.keys));
		return -EINVAL;
	}

	switch (dev->_interface) {
	case interface_i2c:
		if ((error = write_then_read(dev, dev->wbuf, 1, dev->rbuf,
			5 + dev->tp_info.key_num * 5)) < 0)
			return error;
		key_info = (struct ilitek_key_info_v6 *)dev->rbuf;
		break;

	case interface_usb:
		if ((error = write_then_read(dev, dev->wbuf, 1,
			NULL, 0)) < 0 ||
		    (error = write_then_read(dev, NULL, 0,
		    	dev->rbuf, 256)) < 0)
			return error;
		key_info = (struct ilitek_key_info_v6 *)(dev->rbuf + 6);
		break;
	case interface_hid_over_i2c:
		if ((error = write_then_read(dev, dev->wbuf, 1,
			NULL, 0)) < 0 ||
		    (error = write_then_read(dev, NULL, 0,
		    	dev->rbuf, 256)) < 0)
			return error;
		key_info = (struct ilitek_key_info_v6 *)(dev->rbuf + 4);
		break;
	default:
		return -EINVAL;
	};

	dev->key.info.mode = key_info->mode;
	TP_MSG(dev->id, "[Panel Information] key mode: %hhu\n", dev->key.info.mode);

	dev->key.info.x_len = key_info->x_len;
	dev->key.info.y_len = key_info->y_len;
	TP_MSG(dev->id, "key_x_len: %hu, key_y_len: %hu\n",
		dev->key.info.x_len, dev->key.info.y_len);

	for (i = 0; i < dev->tp_info.key_num; i++) {
		dev->key.info.keys[i].id = key_info->keys[i].id;
		dev->key.info.keys[i].x = key_info->keys[i].x;
		dev->key.info.keys[i].y = key_info->keys[i].y;
		TP_MSG(dev->id, "key[%u] id: %hhu, x: %hu, y: %hu\n", i,
			dev->key.info.keys[i].id, dev->key.info.keys[i].x,
			dev->key.info.keys[i].y);
	}

	return 0;
}

static int api_protocol_get_key_info(struct ilitek_ts_device *dev, void *data)
{
	if (dev->protocol.flag == PTL_V3)
		return api_protocol_get_key_info_v3(dev, data);
	else if (dev->protocol.flag == PTL_V6)
		return api_protocol_get_key_info_v6(dev, data);

	return -EINVAL;
}

static int api_protocol_get_ptl_ver(struct ilitek_ts_device *dev, void *data)
{
	int error;

	UNUSED(data);

	dev->protocol.flag = PTL_V6;
	dev->reset_time = 1000;
	if ((error = write_then_read(dev, dev->wbuf, 1, dev->rbuf, 3)) < 0)
		return error;

	dev->protocol.ver = (dev->rbuf[0] << 16) + (dev->rbuf[1] << 8) +
			     dev->rbuf[2];
	TP_MSG(dev->id, "[Protocol Version]: %x.%x.%x\n",
		(dev->protocol.ver >> 16) & 0xFF,
		(dev->protocol.ver >> 8) & 0xFF,
		dev->protocol.ver & 0xFF);

	if (((dev->protocol.ver >> 16) & 0xFF) == 0x3 ||
	    (dev->protocol.ver & 0xFFFF00) == BL_PROTOCOL_V1_6 ||
	    (dev->protocol.ver & 0xFFFF00) == BL_PROTOCOL_V1_7) {
		dev->reset_time = 200;
		dev->protocol.flag = PTL_V3;
	} else if (((dev->protocol.ver >> 16) & 0xFF) == 0x6 ||
		 (dev->protocol.ver & 0xFFFF00) == BL_PROTOCOL_V1_8) {
		 dev->reset_time = 600;
		dev->protocol.flag = PTL_V6;
	}

	return 0;
}

static int api_protocol_get_fw_ver(struct ilitek_ts_device *dev, void *data)
{
	int error;

	UNUSED(data);

	if ((error = write_then_read(dev, dev->wbuf, 1, dev->rbuf, 8)) < 0)
		return error;

	memcpy(dev->fw_ver, dev->rbuf, 8);

	if (dev->ic[0].mode == 0x55) {
		TP_MSG_ARR(dev->id, "[BL Firmware Version]", TYPE_U8,
			   8, dev->fw_ver);
	} else {
		TP_MSG_ARR(dev->id, "[FW Version]", TYPE_U8, 4, dev->fw_ver);
		TP_MSG_ARR(dev->id, "[Customer Version]", TYPE_U8,
			   4, dev->fw_ver + 4);
	}

	return 0;
}

static int api_protocol_get_mcu_mode(struct ilitek_ts_device *dev, void *data)
{
	int error;
	uint8_t i, ic_num = (data) ? *(uint8_t *)data : 1;

	if (ic_num > ARRAY_SIZE(dev->ic))
		return -EINVAL;

	if ((error = write_then_read(dev, dev->wbuf, 1, dev->rbuf,
					     2 * ic_num)) < 0)
		return error;

	for (i = 0; i < ic_num; i++) {
		dev->ic[i].mode = dev->rbuf[i * 2];

		if (dev->ic[i].mode == 0x5a)
			_sprintf(dev->ic[i].mode_str, 0, "AP");
		else if (dev->ic[i].mode == 0x55)
			_sprintf(dev->ic[i].mode_str, 0, "BL");
		else
			_sprintf(dev->ic[i].mode_str, 0, "UNKNOWN");
	}

	TP_MSG(dev->id, "[Current Mode] Master: 0x%hhx %s\n", dev->ic[0].mode,
		dev->ic[0].mode_str);
	for (i = 1; i < ic_num; i++)
		TP_MSG(dev->id, "[Current Mode] Slave[%hhu]: 0x%hhx %s\n", i,
			dev->ic[i].mode, dev->ic[i].mode_str);

	return 0;
}

static int api_protocol_get_sensor_id(struct ilitek_ts_device *dev, void *data)
{
	int error;

	UNUSED(data);

	/* return 0 to skip error check */
	if (!support_sensor_id(dev))
		return 0;

	if ((error = write_then_read(dev, dev->wbuf, 1, dev->rbuf, 3)) < 0)
		return error;

	dev->sensor.header = be16(dev->rbuf);
	dev->sensor.id = dev->rbuf[2];

	TP_MSG(dev->id, "[Sensor ID] header: 0x%hx, id: 0x%hhx\n",
		dev->sensor.header,
		(uint8_t)(dev->sensor.id & dev->setting.sensor_id_mask));

	return 0;
}

static int api_protocol_get_product_info(struct ilitek_ts_device *dev, void *data)
{
	int error;

	UNUSED(data);

	/* return 0 to skip error check */
	if (!support_production_info(dev))
		return 0;

	if ((error = write_then_read(dev, dev->wbuf, 1, dev->rbuf, 8)) < 0)
		return error;

	memcpy(dev->product_info, dev->rbuf, 8);

	TP_MSG_ARR(dev->id, "[Production Info]", TYPE_U8, 8, dev->product_info);

	return 0;
}

static int api_protocol_get_fwid(struct ilitek_ts_device *dev, void *data)
{
	int error;

	UNUSED(data);

	/* return 0 to skip error check */
	if (!support_fwid(dev))
		return 0;

	if ((error = write_then_read(dev, dev->wbuf, 1, dev->rbuf, 4)) < 0)
		return error;

	dev->customer_id = le16(dev->rbuf);
	dev->fwid = le16(dev->rbuf + 2);

	TP_MSG(dev->id, "[Customer ID] %#x\n", dev->customer_id);
	TP_MSG(dev->id, "[FWID] %#x\n", dev->fwid);

	return 0;
}

static int api_protocol_get_crypto_info(struct ilitek_ts_device *dev,
					void *data)
{
	uint16_t crypto_ver;
	uint32_t crypto_opt;

	UNUSED(data);

	/*
	 * encrypted report format should be supported after AP v6.0.8
	 * set report format to 0 if protocol version not matched or
	 * crypto info say it's not supported.
	 */
	if (dev->protocol.ver < 0x060008 ||
	    write_then_read(dev, dev->wbuf, 1, dev->rbuf, 6) < 0) {
		dev->tp_info.format = 0;
		return 0;
	}

	crypto_ver = le16(dev->rbuf);
	crypto_opt = le32(dev->rbuf + 2, 4);

	TP_MSG(dev->id, "[Encrypt Ver.] %#x\n", crypto_ver);
	TP_MSG(dev->id, "[Encrypt Options] %#x\n", crypto_opt);

	if (!(crypto_opt & 1))
		dev->tp_info.format = 0;

	return 0;
}

static int api_protocol_get_hid_info(struct ilitek_ts_device *dev, void *data)
{
	int error;

	UNUSED(data);

	if (dev->protocol.ver < 0x060009)
		return 0;

	if ((error = write_then_read(dev, dev->wbuf, 1, dev->rbuf, 6)) < 0)
		return error;

	memcpy(&dev->hid_info, dev->rbuf, sizeof(dev->hid_info));

	TP_MSG(dev->id, "vid/pid/rev: %#x/%#x/%#x\n",
		dev->hid_info.vid, dev->hid_info.pid, dev->hid_info.rev);

	return 0;
}

static bool is_special_char(char c)
{
	return ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
		(c >= '0' && c <= '9')) ? false : true;
}

static int api_protocol_get_mcu_ver(struct ilitek_ts_device *dev, void *data)
{
	int error;
	unsigned int i;
	struct ilitek_ts_kernel_ver *parser;

	UNUSED(data);

	/*
	 * GET_MCU_INFO (0x62) cmd support V6 and BL > v1.8.2 and AP > v6.0.7
	 * otherwise, use GET_MCU_VER (0x61) cmd
	 */
	if (dev->protocol.flag == PTL_V6 &&
	    ((dev->ic[0].mode == 0x55 && dev->protocol.ver > 0x010802) ||
	     (dev->ic[0].mode == 0x5a && dev->protocol.ver > 0x060008))) {
		if ((error = api_protocol_set_cmd(dev, GET_MCU_INFO,
						  NULL)) < 0)
			return error;
	} else {
		if ((error = write_then_read(dev, dev->wbuf, 1,
			dev->rbuf, 32)) < 0)
			return error;

		parser = (struct ilitek_ts_kernel_ver *)dev->rbuf;

		memset(dev->mcu_info.ic_name, 0,
			sizeof(dev->mcu_info.ic_name));
		_sprintf(dev->mcu_info.ic_name, 0, "%04x", parser->ic_name);

		memset(dev->mcu_info.module_name, 0,
			sizeof(dev->mcu_info.module_name));
		memcpy(dev->mcu_info.module_name, parser->module_name,
			sizeof(parser->module_name));
	}

	if (dev->protocol.flag == PTL_V6) {
		if (is_29xx(dev)) {
			/* modify reset time to 100ms for 29xx ICs */
			dev->reset_time = 100;

			dev->mcu_info.mm_addr = MM_ADDR_29XX;
			dev->mcu_info.min_addr = START_ADDR_29XX;
			dev->mcu_info.max_addr = END_ADDR_LEGO;
		} else {
			dev->mcu_info.mm_addr = MM_ADDR_LEGO;
			dev->mcu_info.min_addr = START_ADDR_LEGO;
			dev->mcu_info.max_addr = END_ADDR_LEGO;
		}
	}

	for (i = 0; i < sizeof(dev->mcu_info.module_name); i++) {
		if (is_special_char(dev->mcu_info.module_name[i]))
			dev->mcu_info.module_name[i] = 0;
	}
	if (!strcmp(dev->mcu_info.ic_name, "2133"))
		_sprintf(dev->mcu_info.ic_name, 0, "2132S");

	TP_MSG(dev->id, "[MCU Kernel Version] ILI%s\n", dev->mcu_info.ic_name);
	TP_MSG(dev->id, "[Module Name]: [%s]\n", dev->mcu_info.module_name);

	return 0;
}

static int api_protocol_get_mcu_info(struct ilitek_ts_device *dev, void *data)
{
	int error;
	unsigned int i;

	UNUSED(data);

	/*
	 * GET_MCU_INFO (0x62) cmd only support V6 and BL > v1.8.2 and AP > v6.0.7
	 * otherwise, return 0 to skip this command.
	 */
	if (dev->protocol.flag != PTL_V6 ||
	    (dev->ic[0].mode == 0x55 && dev->protocol.ver < 0x010803) ||
	    (dev->ic[0].mode == 0x5a && dev->protocol.ver < 0x060009))
		return 0;

	if ((error = write_then_read(dev, dev->wbuf, 1, dev->rbuf, 32)) < 0)
		return error;

	memcpy(&dev->mcu_info.parser, dev->rbuf,
		sizeof(dev->mcu_info.parser));

	memset(dev->mcu_info.ic_name, 0, sizeof(dev->mcu_info.ic_name));
	memcpy(dev->mcu_info.ic_name, dev->mcu_info.parser.ic_name,
		sizeof(dev->mcu_info.parser.ic_name));

	memcpy(dev->mcu_info.module_name,
		dev->mcu_info.parser.module_name,
		sizeof(dev->mcu_info.parser.module_name));
	dev->mcu_info.mm_addr = le32(dev->mcu_info.parser.mm_addr, 3);

	for (i = 0; i < sizeof(dev->mcu_info.module_name); i++) {
		if (is_special_char(dev->mcu_info.module_name[i]))
			dev->mcu_info.module_name[i] = 0;
	}

	return 0;
}

static int api_protocol_set_fs_info(struct ilitek_ts_device *dev, void *data)
{
	struct freq_settings *freq = (struct freq_settings *)data;

	if (!data)
		return -EINVAL;

	dev->wbuf[1] = freq->sine.start & 0xFF;
	dev->wbuf[2] = freq->sine.start >> 8;
	dev->wbuf[3] = freq->sine.end & 0xFF;
	dev->wbuf[4] = freq->sine.end >> 8;
	dev->wbuf[5] = freq->sine.step;
	dev->wbuf[6] = freq->mc_swcap.start & 0xFF;
	dev->wbuf[7] = freq->mc_swcap.start >> 8;
	dev->wbuf[8] = freq->mc_swcap.end & 0xFF;
	dev->wbuf[9] = freq->mc_swcap.end >> 8;
	dev->wbuf[10] = freq->mc_swcap.step;
	dev->wbuf[11] = freq->sc_swcap.start & 0xFF;
	dev->wbuf[12] = freq->sc_swcap.start >> 8;
	dev->wbuf[13] = freq->sc_swcap.end & 0xFF;
	dev->wbuf[14] = freq->sc_swcap.end >> 8;
	dev->wbuf[15] = freq->sc_swcap.step;
	dev->wbuf[16] = freq->frame_cnt & 0xFF;
	dev->wbuf[17] = freq->frame_cnt >> 8;
	dev->wbuf[18] = freq->scan_type;

	if (dev->protocol.ver < 0x60009)
		return write_then_read(dev, dev->wbuf, 19, dev->rbuf, 5);

	dev->wbuf[16] = freq->dump1.start & 0xFF;
	dev->wbuf[17] = freq->dump1.start >> 8;
	dev->wbuf[18] = freq->dump1.end & 0xFF;
	dev->wbuf[19] = freq->dump1.end >> 8;
	dev->wbuf[20] = freq->dump1.step;
	dev->wbuf[21] = freq->dump1_val;
	dev->wbuf[22] = freq->dump2.start & 0xFF;
	dev->wbuf[23] = freq->dump2.start >> 8;
	dev->wbuf[24] = freq->dump2.end & 0xFF;
	dev->wbuf[25] = freq->dump2.end >> 8;
	dev->wbuf[26] = freq->dump2.step;
	dev->wbuf[27] = freq->dump2_val;
	dev->wbuf[28] = freq->frame_cnt & 0xFF;
	dev->wbuf[29] = freq->frame_cnt >> 8;
	dev->wbuf[30] = freq->scan_type;

	return write_then_read(dev, dev->wbuf, 31, dev->rbuf, 5);
}

static int api_protocol_set_short_info(struct ilitek_ts_device *dev, void *data)
{
	struct short_settings *_short = (struct short_settings *)data;

	if (!data)
		return -EINVAL;

	TP_DBG(dev->id, "short info dump1: 0x%hhx, dump2: 0x%hhx, vref: 0x%hhx, postidle: 0x%hx\n",
		_short->dump_1, _short->dump_2,
		_short->v_ref_L, _short->post_idle);

	dev->wbuf[1] = _short->dump_1;
	dev->wbuf[2] = _short->dump_2;
	dev->wbuf[3] = _short->v_ref_L;
	dev->wbuf[4] = _short->post_idle & 0xFF;
	dev->wbuf[5] = (_short->post_idle >> 8) & 0xFF;

	return write_then_read(dev, dev->wbuf, 6, NULL, 0);
}

static int api_protocol_set_open_info(struct ilitek_ts_device *dev, void *data)
{
	struct open_settings *open = (struct open_settings *)data;

	if (!data)
		return -EINVAL;

	TP_DBG(dev->id, "open info freq.: %hu, gain: 0x%hhx, gain_rfb: 0x%hhx, afe_res_sel: 0x%hhx, mc_fsel: 0x%hhx\n",
		open->freq, open->gain, open->gain_rfb,
		open->afe_res_sel, open->mc_fsel);

	dev->wbuf[1] = open->freq & 0xFF;
	dev->wbuf[2] = (open->freq >> 8) & 0xFF;
	dev->wbuf[3] = open->gain;
	dev->wbuf[4] = open->gain_rfb;
	dev->wbuf[5] = open->afe_res_sel;
	dev->wbuf[6] = open->mc_fsel;

	return write_then_read(dev, dev->wbuf, 7, NULL, 0);
}

static int api_protocol_set_p2p_info(struct ilitek_ts_device *dev, void *data)
{
	struct p2p_settings *p2p = (struct p2p_settings *)data;

	if (!data)
		return -EINVAL;

	TP_DBG(dev->id, "p2p info frame_cnt.: %hu\n", p2p->frame_cnt);

	dev->wbuf[1] = p2p->frame_cnt & 0xFF;
	dev->wbuf[2] = (p2p->frame_cnt >> 8) & 0xFF;

	return write_then_read(dev, dev->wbuf, 3, NULL, 0);
}

static int api_protocol_set_pen_fs_info(struct ilitek_ts_device *dev,
					void *data)
{
	int error;
	struct freq_settings *freq = (struct freq_settings *)data;

	if (!data)
		return -EINVAL;

	dev->wbuf[1] = freq->pen.mode;
	dev->wbuf[2] = freq->pen.start & 0xFF;
	dev->wbuf[3] = freq->pen.start >> 8;
	dev->wbuf[4] = freq->pen.end & 0xFF;
	dev->wbuf[5] = freq->pen.end >> 8;
	dev->wbuf[6] = freq->frame_cnt & 0xFF;
	dev->wbuf[7] = freq->frame_cnt >> 8;

	if ((error = write_then_read(dev, dev->wbuf, 8, dev->rbuf, 0)) < 0)
		return error;

	return 0;
}

static int api_protocol_get_core_ver(struct ilitek_ts_device *dev, void *data)
{
	int error;

	UNUSED(data);

	if ((error = write_then_read(dev, dev->wbuf, 1, dev->rbuf, 8)) < 0)
		return error;

	memcpy(dev->core_ver, dev->rbuf, 8);

	TP_MSG_ARR(dev->id, "[CoreVersion]", TYPE_U8, 4, dev->core_ver);

	return 0;
}

static int api_protocol_set_sw_reset(struct ilitek_ts_device *dev, void *data)
{
	int error;
	int wlen = 1;
	bool need_re_enum = (data) ? *(bool *)data : false;

	/* Do not software reset for I2C-HID interface */
	if (dev->_interface == interface_hid_over_i2c)
		return 0;

	dev->wbuf[1] = 0;
	if ((error = write_then_read(dev, dev->wbuf, wlen, dev->rbuf, 0)) < 0)
		return error;

	dev->cb.delay_ms(dev->reset_time);

	if (dev->_interface == interface_usb && need_re_enum)
		return re_enum_helper(dev, enum_sw_reset);

	return 0;
}

static int api_protocol_get_sys_busy(struct ilitek_ts_device *dev, void *data)
{
	int error;

	if (data)
		*(uint8_t *)data = 0;

	memset(dev->rbuf, 0, 64);
	if ((error = write_then_read(dev, dev->wbuf, 1, dev->rbuf, 1)) < 0)
		return error;

	if (data)
		*(uint8_t *)data = dev->rbuf[0];

	return 0;
}

static int api_protocol_get_ap_crc_v6(struct ilitek_ts_device *dev, void *data)
{
	int error;
	uint8_t i, ic_num = (data) ? *(uint8_t *)data : 1;

	if (ic_num > ARRAY_SIZE(dev->ic))
		return -EINVAL;

	if ((error = write_then_read(dev, dev->wbuf, 1,
				     dev->rbuf, 2 * ic_num)) < 0)
		return  error;

	dev->ic[0].crc[0] = le16(dev->rbuf);
	TP_MSG(dev->id, "[FW CRC] Master: 0x%x\n", dev->ic[0].crc[0]);

	for (i = 1; i < ic_num; i++) {
		dev->ic[i].crc[0] = le16(dev->rbuf + 2 * i);
		TP_MSG(dev->id, "[FW CRC] Slave[%hhu]: 0x%x\n", i, dev->ic[i].crc[0]);
	}

	return 0;
}

static int api_protocol_get_ap_crc_v3(struct ilitek_ts_device *dev, void *data)
{
	int error, rlen;

	UNUSED(data);

	rlen = 2;
	if (is_231x(dev))
		rlen = 4;

	if (dev->_interface == interface_i2c) {
		if ((error = write_then_read(dev, dev->wbuf, 1, NULL, 0)) < 0)
			return  error;
		dev->cb.delay_ms(600);
		if ((error = write_then_read(dev, NULL, 0, dev->rbuf, rlen)) < 0)
			return error;
	} else {
		if ((error = write_then_read(dev, dev->wbuf, 1, dev->rbuf, rlen)) < 0)
			return error;
	}

	dev->ic[0].crc[0] = le16(dev->rbuf);
	if (is_231x(dev))
		dev->ic[0].crc[0] |= (le16(dev->rbuf + 2) << 16);

	TP_MSG(dev->id, "[Check Code] AP: %#x\n", dev->ic[0].crc[0]);

	return 0;
}


static int api_protocol_get_ap_crc(struct ilitek_ts_device *dev, void *data)
{

	if (dev->protocol.flag == PTL_V6)
		return api_protocol_get_ap_crc_v6(dev, data);
	else if (dev->protocol.flag == PTL_V3)
		return api_protocol_get_ap_crc_v3(dev, data);

	return -EINVAL;
}


static int api_protocol_set_mode_v3(struct ilitek_ts_device *dev, void *data)
{
	int error;
	uint8_t mode = dev->wbuf[1];

	UNUSED(data);

	if ((error = write_then_read(dev, dev->wbuf, 2, NULL, 0)) < 0)
		return error;

	/*
	 * Bridge with V3 IC need to set bridge into/out test mode additionally.
	 */
	if (dev->quirks & DEV_QUIRK_SET_BRIDGE_ENG_MODE_AFTER_F2 &&
	    dev->_interface == interface_i2c) {
		memset(dev->wbuf, 0, sizeof(dev->wbuf));
		dev->wbuf[0] = 0x03;
		dev->wbuf[1] = 0xf2;
		dev->wbuf[2] = mode;
		error = dev->cb.write_direct(dev->wbuf, 3, dev->_private);
	}

	return (error < 0) ? error : 0;
}

static int api_protocol_write_enable(struct ilitek_ts_device *dev, void *data)
{
	int error;
	bool in_ap = (data) ? *(bool *)data : true;

	if ((error = write_then_read(dev, dev->wbuf,
				     (in_ap) ? 3 : 10, NULL, 0)) < 0)
		return error;

	/*
	 * V3 need AP/BL mode switch delay
	 */
	if (in_ap)
		dev->cb.delay_ms(is_231x(dev) ? 1000 : 100);
	else
		dev->cb.delay_ms(10);

	return 0;
}

static int api_protocol_write_data_v3(struct ilitek_ts_device *dev, void *data)
{
	UNUSED(data);

	return write_then_read(dev, dev->wbuf, 33, NULL, 0);
}

static int api_protocol_get_df_crc(struct ilitek_ts_device *dev,  void *data)
{
	int error;

	UNUSED(data);

	dev->ic[0].crc[1] = 0;
	if ((error = write_then_read(dev, dev->wbuf, 1, dev->rbuf, 4)) < 0)
		return error;

	dev->ic[0].crc[1] = le16(dev->rbuf + 2) << 16 | le16(dev->rbuf);
	TP_MSG(dev->id, "[Check Code] Data: %#x\n", dev->ic[0].crc[1]);

	return 0;
}

static int api_protocol_set_mode_v6(struct ilitek_ts_device *dev, void *data)
{
	UNUSED(data);

	return write_then_read(dev, dev->wbuf, 3, NULL, 0);
}

static int api_protocol_get_crc_by_addr(struct ilitek_ts_device *dev,
					void *data)
{
	int error;
	uint8_t type = (data) ? *(uint8_t *)data : 0;
	uint32_t start, end, t_ms;

	dev->wbuf[1] = type;

	if (type == CRC_CALCULATE) {
		start = le32(dev->wbuf + 2, 3);
		end = le32(dev->wbuf + 5, 3);
		t_ms = ((end - start) / 4096 + 1) * 100;

		TP_DBG(NULL, "start: %u, end: %u, t_ms: %u\n",
			start, end, t_ms);

		if ((error = write_then_wait_ack(dev, dev->wbuf, 8, t_ms)) < 0)
			return error;
		type = CRC_GET;
		return api_protocol_set_cmd(dev, GET_BLK_CRC_ADDR, &type);
	}

	return write_then_read(dev, dev->wbuf, 2, dev->rbuf, 2);
}

static int api_protocol_get_crc_by_num(struct ilitek_ts_device *dev,
					void *data)
{
	int error;
	uint8_t type = (data) ? *(uint8_t *)data : 0;

	dev->wbuf[1] = type;

	if (type == CRC_CALCULATE) {
		if ((error = write_then_wait_ack(dev, dev->wbuf, 3, 5000)) < 0)
			return error;
		type = CRC_GET;
		return api_protocol_set_cmd(dev, GET_BLK_CRC_NUM, &type);
	}

	return write_then_read(dev, dev->wbuf, 2, dev->rbuf, 2);
}

static int api_protocol_read_flash(struct ilitek_ts_device *dev, void *data)
{
	int error;
	uint32_t code = *(uint32_t *)data;
	bool prepare;
	int rlen;

	if (dev->ic[0].mode != 0x55)
		return -EINVAL;

	if (dev->protocol.flag == PTL_V3) {
		if ((dev->protocol.ver & 0xFFFF00) == BL_PROTOCOL_V1_7 &&
		     dev->fw_ver[3] < 3) {
			TP_ERR(dev->id, "BL: %#x, FW: 0x%hhx-0x%hhx-0x%hhx-0x%hhx not support cmd: 0x%hhx\n",
				dev->protocol.ver, dev->fw_ver[0],
				dev->fw_ver[1], dev->fw_ver[2], dev->fw_ver[3],
				dev->wbuf[0]);
			return -EINVAL;
		}

		return write_then_read(dev, dev->wbuf, 1, dev->rbuf, 32);
	}

	if (!data)
		return -EINVAL;

	prepare = (code >> 16) ? true : false;
	rlen = code & 0xFFFF;

	if (prepare) {
		error = write_then_read(dev, dev->wbuf, 2, NULL, 0);
		dev->cb.delay_ms(100);

		return error;
	}

	if (dev->_interface == interface_i2c)
		error = write_then_read(dev, dev->wbuf, 2, dev->rbuf, rlen);
	else
		error = write_then_read(dev, NULL, 0, dev->rbuf, rlen);

	return error;
}

static int api_protocol_set_flash_addr(struct ilitek_ts_device *dev, void *data)
{
	int error;
	uint32_t addr = *(uint32_t *)data;

	if (!data)
		return -EINVAL;

	if (dev->protocol.flag == PTL_V3) {
		dev->wbuf[3] = addr & 0xFF;
		dev->wbuf[2] = (addr >> 8) & 0xFF;
		dev->wbuf[1] = (addr >> 16) & 0xFF;

		if ((error = write_then_read(dev, dev->wbuf, 4, NULL, 0)) < 0)
			return error;

		dev->cb.delay_ms(5);

		return 0;
	}

	dev->wbuf[1] = addr & 0xFF;
	dev->wbuf[2] = (addr >> 8) & 0xFF;
	dev->wbuf[3] = (addr >> 16) & 0xFF;

	return write_then_read(dev, dev->wbuf, 4, NULL, 0);
}

static int api_protocol_set_data_len(struct ilitek_ts_device *dev, void *data)
{
	UNUSED(data);

	return write_then_read(dev, dev->wbuf, 3, NULL, 0);
}

static int api_protocol_set_flash_enable(struct ilitek_ts_device *dev,
					 void *data)
{
	int error;
	uint8_t type = (data) ? *(uint8_t *)data : 0;
	int wlen, rlen;
	bool in_ap = ((type & 0x1) != 0) ? true : false;
	bool is_slave = ((type & 0x2) != 0) ? true : false;

	uint32_t set_start, set_end, get_start, get_end;

	if (!is_slave) {
		wlen = (in_ap) ? 3 : 9;
		rlen = (in_ap || dev->protocol.ver < 0x010803) ? 0 : 6;

		set_start = le32(dev->wbuf + 3, 3);
		set_end = le32(dev->wbuf + 6, 3);

		if ((error = write_then_read(dev, dev->wbuf, wlen,
					     dev->rbuf, rlen)) < 0)
			return error;

		if (in_ap || dev->protocol.ver < 0x010803)
			return 0;

		get_start = le32(dev->rbuf, 3);
		get_end = le32(dev->rbuf + 3, 3);

		if (set_start != get_start || set_end != get_end) {
			TP_ERR(dev->id, "start/end addr.: %#x/%#x vs. %#x/%#x not match\n",
				set_start, set_end, get_start, get_end);
			return -EINVAL;
		}
		
		return 0;
	}

	if ((error = write_then_wait_ack(dev, dev->wbuf, 9, 20000)) < 0)
		return error;
	dev->cb.delay_ms(2000);

	return (dev->_interface == interface_usb) ?
		re_enum_helper(dev, enum_sw_reset) : 0;
}

static int api_protocol_write_data_v6(struct ilitek_ts_device *dev, void *data)
{
	int wlen;

	if (!data)
		return -EINVAL;

	wlen = *(int *)data;

	return write_then_wait_ack(dev, dev->wbuf, wlen, 200);
}

static int api_protocol_write_data_m2v(struct ilitek_ts_device *dev, void *data)
{
	int wlen;

	if (!data)
		return -EINVAL;

	wlen = *(int *)data;

	return write_then_wait_ack(dev, dev->wbuf, wlen, 30000);
}

static int api_protocol_access_slave(struct ilitek_ts_device *dev, void *data)
{
	int error;
	struct ilitek_slave_access *access;

	if (!data)
		return -EINVAL;

	access = (struct ilitek_slave_access *)data;

	dev->wbuf[1] = access->slave_id;
	dev->wbuf[2] = access->func;
	memset(dev->rbuf, 0, sizeof(dev->rbuf));

	switch (access->func) {
	case M2V_GET_CHECKSUM:
		error = write_then_read(dev, dev->wbuf, 3, dev->rbuf, 3);

		*((uint32_t *)access->data) = (le16(dev->rbuf + 1) << 8) +
			dev->rbuf[0];

		break;

	case M2V_GET_MOD:
		error = write_then_read(dev, dev->wbuf, 3, dev->rbuf, 1);

		*((uint8_t *)access->data) = dev->rbuf[0];

		break;

	case M2V_GET_FW_VER:
		error = write_then_read(dev, dev->wbuf, 3, dev->rbuf, 8);

		memcpy((uint8_t *)access->data, dev->rbuf, 8);

		break;

	case M2V_WRITE_ENABLE:
		dev->wbuf[3] = ((uint8_t *)access->data)[0];
		dev->wbuf[4] = ((uint8_t *)access->data)[1];
		dev->wbuf[5] = ((uint8_t *)access->data)[2];
		dev->wbuf[6] = ((uint8_t *)access->data)[3];
		dev->wbuf[7] = ((uint8_t *)access->data)[4];
		dev->wbuf[8] = ((uint8_t *)access->data)[5];

		//error = write_then_read(dev, dev->wbuf, 9, NULL, 0);
		////Reed Add : 20230721（这里需要等Ack）
		error = write_then_wait_ack(dev, dev->wbuf, 9, 5000);
		break;

	default:
		error = write_then_wait_ack(dev, dev->wbuf, 3, 5000);
		break;
	};

	return error;
}

static int api_protocol_set_ap_mode(struct ilitek_ts_device *dev, void *data)
{
	int error;

	UNUSED(data);

	error = write_then_read(dev, dev->wbuf, 1, NULL, 0);

	if (dev->cb.mode_switch_notify)
		dev->cb.mode_switch_notify(false, dev->_private);

	return error;
}

static int api_protocol_set_bl_mode(struct ilitek_ts_device *dev, void *data)
{
	int error;

	UNUSED(data);

	error = write_then_read(dev, dev->wbuf, 1, NULL, 0);

	if (dev->cb.mode_switch_notify)
		dev->cb.mode_switch_notify(true, dev->_private);

	return error;
}

static int api_protocol_set_idle(struct ilitek_ts_device *dev, void *data)
{
	UNUSED(data);

	return write_then_read(dev, dev->wbuf, 2, NULL, 0);
}

static int api_protocol_set_sleep(struct ilitek_ts_device *dev, void *data)
{
	UNUSED(data);

	return write_then_read(dev, dev->wbuf, 1, NULL, 0);
}

static int api_protocol_set_wakeup(struct ilitek_ts_device *dev, void *data)
{
	UNUSED(data);

	return write_then_read(dev, dev->wbuf, 1, NULL, 0);
}

static int api_protocol_set_func_mode(struct ilitek_ts_device *dev, void *data)
{
	int error;
	bool get = (data) ? *(bool *)data : true;

	if (!data)
		return -EINVAL;

	if (get) {
		if ((error = write_then_read(dev, dev->wbuf, 1, dev->rbuf, 3)) < 0)
			return error;

		dev->func_mode = dev->rbuf[2];
		TP_MSG(dev->id, "[FW Mode] 0x%hhx\n", dev->func_mode);

		return 0;
	}

	if (dev->protocol.flag == PTL_V3) {
		if ((error = write_then_read(dev, dev->wbuf, 4, NULL, 0)) < 0 ||
		    (error = api_check_busy(dev, 1000, 100)) < 0)
			return error;
		return 0;
	} else if (dev->protocol.flag == PTL_V6) {
		if ((error = write_then_wait_ack(dev, dev->wbuf, 4, 1000)) < 0)
			return error;
		return 0;
	}

	return -EINVAL;
}

static int api_protocol_c_model_info(struct ilitek_ts_device *dev, void *data)
{
	UNUSED(data);

	if (dev->protocol.ver < 0x060008)
		return write_then_read(dev, dev->wbuf, 12, NULL, 0);

	return write_then_read(dev, dev->wbuf, 18, NULL, 0);
}

static int api_protocol_tuning_para_v3(struct ilitek_ts_device *dev, void *data)
{
	UNUSED(data);

	return write_then_read(dev, dev->wbuf, 2, NULL, 0);
}

static int api_protocol_tuning_para_v6(struct ilitek_ts_device *dev, void *data)
{
	int error;
	struct tuning_para_settings tuning =
		*(struct tuning_para_settings *)data;
	uint32_t rlen;

	int header;

	if (!data)
		return -EINVAL;

	dev->wbuf[1] = tuning.func;
	dev->wbuf[2] = tuning.ctrl;
	dev->wbuf[3] = tuning.type;

	if (tuning.func == 0x0)
		return write_then_wait_ack(dev, dev->wbuf, 4, 5000);

	rlen = 2048;
	if (dev->protocol.ver < 0x060004)
		rlen = 1024;

	if ((error = write_then_read(dev, dev->wbuf, 4, dev->rbuf, rlen)) < 0)
		return error;

	header = 6;
	if (dev->_interface == interface_i2c)
		header = 5;

	memcpy(tuning.buf, dev->rbuf + header, tuning.read_len);

	return 0;
}

static int api_protocol_set_cdc_init_v3(struct ilitek_ts_device *dev,
					void *data)
{
	int error;
	int wlen;
	struct cdc_settings *set = (struct cdc_settings *)data;

	if (!data)
		return -EINVAL;

	if (set->is_freq) {
		dev->wbuf[1] = 0x0F;
		dev->wbuf[2] = set->freq.sine.start;
		dev->wbuf[3] = set->freq.sine.end;
		dev->wbuf[4] = set->freq.sine.step;

		if ((error = write_then_read(dev, dev->wbuf, 5, NULL, 0)) < 0)
			return error;

		dev->cb.delay_ms(200);
	} else {
		dev->wbuf[1] = set->cmd;
		dev->wbuf[2] = 0;
		dev->wbuf[3] = set->config & 0xFF;
		wlen = 4;

		if (set->config & 0xFF00) {
			dev->wbuf[3] = (set->config >> 8) & 0xFF;
			dev->wbuf[4] = set->config & 0xFF;
			wlen = 5;
		}

		if ((error = write_then_read(dev, dev->wbuf, wlen,
					     NULL, 0)) < 0)
			return error;
	}

	return api_check_busy(dev, 15000, 50);
}

static int api_protocol_get_cdc_v6(struct ilitek_ts_device *dev, void *data)
{
	UNUSED(data);

	return write_then_wait_ack(dev, dev->wbuf, 1, 5000);
}

static int api_protocol_set_cdc_init_v6(struct ilitek_ts_device *dev,
					void *data)
{
	struct cdc_settings *set = (struct cdc_settings *)data;

	if (!data)
		return -EINVAL;

	dev->wbuf[1] = set->cmd;

	if (set->is_freq)
		return write_then_wait_ack(dev, dev->wbuf, 2,
					   set->freq.frame_cnt * 3000);

	if (set->is_p2p)
		return write_then_wait_ack(dev, dev->wbuf, 2,
					   set->p2p.frame_cnt * 3000);

	if (dev->protocol.ver < 0x60009)
		return write_then_wait_ack(dev, dev->wbuf, 2, 5000);

	dev->wbuf[2] = set->config & 0xFF;

	return write_then_wait_ack(dev, dev->wbuf, 3, 5000);
}

static int api_protocol_get_cdc_info_v3(struct ilitek_ts_device *dev,
					void *data)
{
	int error;
	uint32_t *cdc_info = (uint32_t *)data;

	if (!data)
		return -EINVAL;

	if ((error = write_then_read(dev, dev->wbuf, 1, dev->rbuf, 4)) < 0)
		return error;

	*cdc_info = le32(dev->rbuf, 4);

	return 0;
}

int api_protocol_set_cmd(void *handle, uint8_t idx, void *data)
{
	struct ilitek_ts_device *dev = (struct ilitek_ts_device *)handle;
	int error;

	if (!dev || idx >= ARRAY_SIZE(protocol_maps))
		return -EINVAL;

	if (!(dev->protocol.flag & protocol_maps[idx].flag) &&
	    protocol_maps[idx].flag != PTL_ANY) {
		TP_ERR(dev->id, "Unexpected cmd: %s for 0x%hhx only, now is 0x%hhx\n",
			protocol_maps[idx].desc, protocol_maps[idx].flag,
			dev->protocol.flag);
		return -EINVAL;
	}

	dev->wbuf[0] = protocol_maps[idx].cmd;
	if ((error = protocol_maps[idx].func(dev, data)) < 0) {
		TP_ERR(dev->id, "failed to execute cmd: 0x%hhx %s, err: %d\n",
			protocol_maps[idx].cmd, protocol_maps[idx].desc, error);
		return error;
	}

	return 0;
}

int api_set_ctrl_mode(void *handle, uint8_t mode, bool eng)
{
	struct ilitek_ts_device *dev = (struct ilitek_ts_device *)handle;
	int error;
	uint8_t cmd = 0;

	memset(dev->wbuf, 0, sizeof(dev->wbuf));

	if (dev->protocol.flag == PTL_V3) {
		/* V3 only support suspend and normal mode */
		if (mode != mode_normal &&
		    mode != mode_suspend &&
		    mode != mode_test)
			return -EPROTONOSUPPORT;
		dev->wbuf[1] = (mode == mode_normal) ? 0x00 : 0x01;
		cmd = SET_TEST_MOD;
	} else if (dev->protocol.flag == PTL_V6) {
		dev->wbuf[1] = mode;
		dev->wbuf[2] = (eng) ? 0x01 : 0x00;
		cmd = SET_MOD_CTRL;
	}

	if ((error = api_protocol_set_cmd(dev, cmd, NULL)) < 0)
		return error;

	dev->cb.delay_ms(100);

	return 0;
}

uint16_t api_get_block_crc_by_addr(void *handle, uint8_t type,
				   uint32_t start, uint32_t end)
{
	struct ilitek_ts_device *dev = (struct ilitek_ts_device *)handle;

	memset(dev->wbuf, 0, 64);

	dev->wbuf[2] = start;
	dev->wbuf[3] = (start >> 8) & 0xFF;
	dev->wbuf[4] = (start >> 16) & 0xFF;
	dev->wbuf[5] = end & 0xFF;
	dev->wbuf[6] = (end >> 8) & 0xFF;
	dev->wbuf[7] = (end >> 16) & 0xFF;
	if (api_protocol_set_cmd(dev, GET_BLK_CRC_ADDR, &type) < 0)
		return 0;

	return le16(dev->rbuf);
}

uint16_t api_get_block_crc_by_num(void *handle, uint8_t type,
				  uint8_t block_num)
{
	struct ilitek_ts_device *dev = (struct ilitek_ts_device *)handle;

	memset(dev->wbuf, 0, 64);

	dev->wbuf[2] = block_num;
	if (api_protocol_set_cmd(dev, GET_BLK_CRC_NUM, &type) < 0)
		return 0;

	return le16(dev->rbuf);
}

int api_set_data_len(void *handle, uint16_t data_len)
{
	struct ilitek_ts_device *dev = (struct ilitek_ts_device *)handle;

	memset(dev->wbuf, 0, 64);

	dev->wbuf[1] = data_len & 0xFF;
	dev->wbuf[2] = (data_len >> 8) & 0xFF;

	return api_protocol_set_cmd(dev, SET_DATA_LEN, NULL);
}

int api_write_enable_v6(void *handle, bool in_ap, bool is_slave,
			uint32_t start, uint32_t end)
{
	struct ilitek_ts_device *dev = (struct ilitek_ts_device *)handle;
	uint8_t type;

	memset(dev->wbuf, 0, 64);
	dev->wbuf[1] = 0x5A;
	dev->wbuf[2] = 0xA5;
	dev->wbuf[3] = start & 0xFF;
	dev->wbuf[4] = (start >> 8) & 0xFF;
	dev->wbuf[5] = start >> 16;
	dev->wbuf[6] = end & 0xFF;
	dev->wbuf[7] = (end >> 8) & 0xFF;
	dev->wbuf[8] = end >> 16;

	type = (in_ap) ? 0x1 : 0x0;
	type |= (is_slave) ? 0x2 : 0x0;

	return api_protocol_set_cmd(dev, SET_FLASH_EN, &type);
}

int api_write_data_v6(void *handle, int wlen)
{
	return api_protocol_set_cmd(handle, WRITE_DATA_V6, &wlen);
}

int api_access_slave(void *handle, uint8_t id, uint8_t func, void *data)
{
	struct ilitek_slave_access access;

	access.slave_id = id;
	access.func = func;
	access.data = data;

	return api_protocol_set_cmd(handle, ACCESS_SLAVE, &access);
}

int api_write_enable_v3(void *handle, bool in_ap, bool write_ap,
			uint32_t end, uint32_t checksum)
{
	struct ilitek_ts_device *dev = (struct ilitek_ts_device *)handle;

	memset(dev->wbuf, 0, 64);
	dev->wbuf[1] = 0x5A;
	dev->wbuf[2] = 0xA5;
	dev->wbuf[3] = (write_ap) ? 0x0 : 0x1;
	dev->wbuf[4] = (end >> 16) & 0xFF;
	dev->wbuf[5] = (end >> 8) & 0xFF;
	dev->wbuf[6] = end & 0xFF;
	dev->wbuf[7] = (checksum >> 16) & 0xFF;
	dev->wbuf[8] = (checksum >> 8) & 0xFF;
	dev->wbuf[9] = checksum & 0xFF;

	return api_protocol_set_cmd(dev, WRITE_ENABLE, &in_ap);
}

int api_write_data_v3(void *handle)
{
	return api_protocol_set_cmd(handle, WRITE_DATA_V3, NULL);
}

int api_check_busy(void *handle, int timeout_ms, int delay_ms)
{
	struct ilitek_ts_device *dev = (struct ilitek_ts_device *)handle;
	uint8_t busy;

	memset(dev->wbuf, 0, 64);

	while (timeout_ms > 0) {
		api_protocol_set_cmd(dev, GET_SYS_BUSY, &busy);
		if (busy == ILITEK_TP_SYSTEM_READY)
			return 0;

		/* delay ms for each check busy */
		dev->cb.delay_ms(delay_ms);
		timeout_ms -= delay_ms;

		/* if caller set no_retry then skip check busy retry */
		if (dev->setting.no_retry)
			break;
	}

	TP_WARN(dev->id, "check busy timeout: %d ms, state: 0x%hhx\n",
		timeout_ms, busy);

	return -ETIME;
}

int api_to_bl_mode(void *handle, bool to_bl, uint32_t start, uint32_t end)
{
	struct ilitek_ts_device *dev = (struct ilitek_ts_device *)handle;
	int cnt = 0, retry = 15;
	const uint8_t target_mode = (to_bl) ? 0x55 : 0x5A;

	do {
		if (api_protocol_set_cmd(dev, GET_MCU_MOD, NULL) < 0)
			continue;

		if (dev->ic[0].mode == target_mode)
			goto success_change_mode;

		if (to_bl) {
			if (dev->protocol.flag == PTL_V3 &&
			    api_write_enable_v3(dev, true, false, 0, 0) < 0)
				continue;
			else if (dev->protocol.flag == PTL_V6 &&
				 api_write_enable_v6(dev, true, false,
				 		     0, 0) < 0)
				continue;

			api_protocol_set_cmd(dev, SET_BL_MODE, NULL);

			/*
			 * Lego's old BL may trigger unexpected INT after 0xC2,
			 * so make I2C driver handle it ASAP.
			 */
			if ((dev->quirks & DEV_QUIRK_SKIP_INT_AFTER_C2) &&
			    dev->protocol.flag == PTL_V6 &&
			    dev->_interface == interface_i2c)
				dev->cb.init_ack(dev->_private);
		} else {
			if (dev->protocol.flag == PTL_V3 &&
			    api_write_enable_v3(dev, true, false, 0, 0) < 0)
				continue;
			else if (dev->protocol.flag == PTL_V6 &&
				 api_write_enable_v6(dev, false, false,
				 		     start, end) < 0)
				continue;

			api_protocol_set_cmd(dev, SET_AP_MODE, NULL);
		}

		switch (dev->_interface) {
		case interface_hid_over_i2c:
		case interface_i2c:
			dev->cb.delay_ms(1000 + 100 * cnt);
			break;
		case interface_usb:
			do {
				if (!re_enum_helper(dev, enum_ap_bl))
					break;
			} while (!dev->setting.no_retry && cnt++ < retry);
			break;
		}
	} while (!dev->setting.no_retry && cnt++ < retry);

	TP_ERR(dev->id, "current mode: 0x%hhx, change to %s mode failed\n",
		dev->ic[0].mode, (to_bl) ? "BL" : "AP");
	return -EFAULT;

success_change_mode:
	TP_MSG(dev->id, "current mode: 0x%hhx %s mode\n", dev->ic[0].mode,
		(to_bl) ? "BL" : "AP");

	/* update fw ver. in AP/BL mode */
	api_protocol_set_cmd(dev, GET_FW_VER, NULL);

	/* update protocol ver. in AP/BL mode */
	api_protocol_set_cmd(dev, GET_PTL_VER, NULL);

	return 0;
}

int api_set_idle(void *handle, bool enable)
{
	struct ilitek_ts_device *dev = (struct ilitek_ts_device *)handle;

	memset(dev->wbuf, 0, 64);
	dev->wbuf[1] = (enable) ? 1 : 0;
	return api_protocol_set_cmd(dev, SET_MCU_IDLE, NULL);
}

int api_set_func_mode(void *handle, uint8_t mode)
{
	struct ilitek_ts_device *dev = (struct ilitek_ts_device *)handle;
	int error;
	bool get = false;

	memset(dev->wbuf, 0, 64);

	switch (dev->protocol.flag) {
	case PTL_V3:
		dev->wbuf[1] = 0x55;
		dev->wbuf[2] = 0xAA;
		break;
	case PTL_V6:
		dev->wbuf[1] = 0x5A;
		dev->wbuf[2] = 0xA5;
		break;
	default:
		TP_ERR(dev->id, "unrecognized protocol: %x, flag: %hhu",
			dev->protocol.ver, dev->protocol.flag);
		return -EINVAL;
	}
	dev->wbuf[3] = mode;

	if (dev->protocol.ver < 0x30400) {
		TP_ERR(dev->id, "protocol: 0x%x not support\n",
			dev->protocol.ver);
		return -EINVAL;
	}

	if ((error = api_protocol_set_cmd(dev, SET_FUNC_MOD, &get)) < 0 ||
	    (error = api_get_func_mode(dev)) < 0)
		return error;

	return (dev->func_mode == mode) ? 0 : -EFAULT;
}

int api_get_func_mode(void *handle)
{
	bool get = true;

	return api_protocol_set_cmd(handle, SET_FUNC_MOD, &get);
}

int api_erase_data_v3(void *handle)
{
	struct ilitek_ts_device *dev = (struct ilitek_ts_device *)handle;
	int error;

	memset(dev->wbuf, 0xff, sizeof(dev->wbuf));

	TP_INFO(dev->id, "erase data flash for %s, mode: %hhx\n",
		dev->mcu_info.ic_name, dev->ic[0].mode);

	if (is_231x(dev)) {
		/* V3 231x only support erase data flash in AP mode */
		if (dev->ic[0].mode != 0x5a) {
			TP_WARN(dev->id, "invalid mode: %hhx for data erase\n",
				dev->ic[0].mode);
			return 0;
		}

		if ((error = api_write_enable_v3(dev, true, false, 0, 0)) < 0)
			return error;

		dev->cb.delay_ms(100);

		dev->wbuf[1] = 0x02;
		if ((error = api_protocol_set_cmd(dev, TUNING_PARA_V3,
						  NULL)) < 0)
			return error;

		switch (dev->_interface) {
		case interface_usb:
			return re_enum_helper(dev, enum_ap_bl);
		default:
			dev->cb.delay_ms(1500);
			break;
		}
	} else {
		/* V3 251x only support erase data flash in BL mode */
		if (dev->ic[0].mode != 0x55) {
			TP_WARN(dev->id, "invalid mode: %hhx for data erase\n",
				dev->ic[0].mode);
			return 0;
		}

		if ((error = api_write_enable_v3(dev, false, false,
			0xf01f, 0)) < 0)
			return error;

		dev->cb.delay_ms(5);

		memset(dev->wbuf + 1, 0xFF, 32);
		if ((error = api_write_data_v3(dev)) < 0)
			return error;

		dev->cb.delay_ms(500);
	}

	return 0;
}

static int api_read_flash_v3(struct ilitek_ts_device *dev, uint8_t *buf,
			     uint32_t start, uint32_t len)
{
	int error;
	uint32_t addr, end = start + len, copied;

	for (addr = start, copied = 0; addr < end;
	     addr += 32, copied += 32) {
		if ((error = api_protocol_set_cmd(dev, SET_ADDR, &addr)) < 0 ||
		    (error = api_protocol_set_cmd(dev, READ_FLASH, NULL)) < 0)
			return error;

		memcpy(buf + copied, dev->rbuf, 32);
	}

	return 0;
}

static int api_read_flash_v6(struct ilitek_ts_device *dev, uint8_t *buf,
			     uint32_t start, uint32_t len)
{
	int error;
	uint32_t code;
	uint32_t addr, end = start + len, copied;
	uint16_t data_len;

	if (dev->ic[0].mode != 0x55)
		return -EINVAL;

	for (addr = start, copied = 0; addr < end;
	     addr += data_len, copied += data_len) {
		if (end - addr > 1024)
			data_len = 2048;
		else if (end - addr > 256)
			data_len = 1024;
		else if (end - addr > 64)
			data_len = 256;
		else
			data_len = 64;

		if ((error = api_set_data_len(dev, data_len)) < 0 ||
		    (error = api_protocol_set_cmd(dev, SET_ADDR, &addr)) < 0)
			return error;

		dev->wbuf[1] = 0x1; code = 1 << 16;
		if ((error = api_protocol_set_cmd(dev, READ_FLASH, &code)) < 0)
			return error;

		dev->wbuf[1] = 0x0; code = data_len & 0xFFFF;
		if ((error = api_protocol_set_cmd(dev, READ_FLASH, &code)) < 0)
			return error;

		if (dev->_interface == interface_hid_over_i2c)
			memcpy(buf + copied, dev->rbuf + 5, data_len);
		else
			memcpy(buf + copied, dev->rbuf, data_len);
	}

	return 0;	
}

int api_read_flash(void *handle, uint8_t *buf,
		   uint32_t start, uint32_t len)
{
	struct ilitek_ts_device *dev = (struct ilitek_ts_device *)handle;

	if (dev->protocol.flag == PTL_V3)
		return api_read_flash_v3(dev, buf, start, len);

	return api_read_flash_v6(dev, buf, start, len);
}

int api_read_mp_result(void *handle, uint8_t *buf, int buf_size)
{
	struct ilitek_ts_device *dev = (struct ilitek_ts_device *)handle;
	int error;
	struct tuning_para_settings tuning;

	uint32_t addr = (is_29xx(dev)) ? 0x2e000 : 0x3e000;

	/* 1000 bytes data/ 2 bytes crc/ 1 bytes checksum */
	tuning.read_len = 1003;
	tuning.buf = buf;

	if (buf_size < 1000)
		return -EOVERFLOW;

	if ((error = api_set_ctrl_mode(dev, mode_suspend, false)) < 0)
		return error;

	if (dev->ic[0].mode == 0x55) {
		if ((error = api_read_flash_v6(dev, buf, addr, 1000)) < 0)
			return error;
	} else {
		tuning.func = 0x0; tuning.ctrl = 0x4; tuning.type = 0x10;
		if ((error = api_protocol_set_cmd(dev, TUNING_PARA_V6,
						  &tuning)) < 0)
			return error;

		tuning.func = 0x1; tuning.ctrl = 0x4; tuning.type = 0x10;
		if ((error = api_protocol_set_cmd(dev, TUNING_PARA_V6,
						  &tuning)) < 0)
			return error;
	} 

	return api_set_ctrl_mode(dev, mode_normal, false);
}

int api_write_data_m2v(void *handle, int wlen)
{
	return api_protocol_set_cmd(handle, WRITE_DATA_M2V, &wlen);
}

int api_to_bl_mode_m2v(void *handle, bool to_bl)
{
	struct ilitek_ts_device *dev = (struct ilitek_ts_device *)handle;
	int cnt = 0, retry = 15;
	const uint8_t target_mode = (to_bl) ? 0x55 : 0x5A;
	uint8_t mode;

	if (dev->_interface != interface_usb)
		return -EINVAL;

	do {
		dev->cb.delay_ms(100);//Reed Add : 20230927

		if (api_access_slave(dev, 0x80, M2V_GET_MOD, &mode) < 0)
			continue;

		if (mode == target_mode)
			goto success_change_mode;

		//if (to_bl && api_access_slave(dev, 0x80, SLAVE_SET_BL,
		//			      NULL) < 0)
		//	continue;
		//else if (api_access_slave(dev, 0x80, SLAVE_SET_AP, NULL) < 0)
		//	continue;
		// //Reed Add : 20230721（原本写法有问题，切BLMode成功后，又会执行else if,切回到APMode）
		dev->cb.delay_ms(300);//Reed Add : 20230927

		if (to_bl && api_access_slave(dev, 0x80, SLAVE_SET_BL, NULL) < 0)
			continue;
		else if (!to_bl && api_access_slave(dev, 0x80, SLAVE_SET_AP, NULL) < 0)
			continue;

		do {
			dev->cb.delay_ms(100);//Reed Add : 20230927
			// //Reed Add : 20230721（M2V切换Mode，2326不会重载，所以不能枚举设备，枚举设备会Fail）
			if (/*!re_enum_helper(dev, enum_ap_bl) &&*/
			    !api_access_slave(dev, 0x80, M2V_GET_MOD, &mode) &&
			    mode == target_mode)
				goto success_change_mode;
			dev->cb.delay_ms(5000);
		} while (!dev->setting.no_retry && cnt++ < retry);
		break;
	} while (!!dev->setting.no_retry && cnt++ < retry);

	TP_ERR(dev->id, "M2V current mode: 0x%hhx, change to %s mode failed\n",
		mode, (to_bl) ? "BL" : "AP");
	return -EFAULT;

success_change_mode:
	TP_MSG(dev->id, "M2V current mode: 0x%hhx %s mode\n", mode,
		(to_bl) ? "BL" : "AP");

	return 0;
}

int api_update_protocol_ver(void *handle)
{
	int error;
	struct ilitek_ts_device *dev = (struct ilitek_ts_device *)handle;

	if (!handle)
		return -EINVAL;

	dev->protocol.flag = PTL_V6;
	dev->tp_info.ic_num = 1;

	if ((error = api_set_ctrl_mode(dev, mode_suspend, false)) < 0 ||
	    (error = api_protocol_set_cmd(dev, GET_PTL_VER, NULL)) < 0 ||
	    (error = api_set_ctrl_mode(dev, mode_normal, false)) < 0)
		return error;

	return 0;
}

int api_update_ts_info(void *handle)
{
	int error;
	struct ilitek_ts_device *dev = (struct ilitek_ts_device *)handle;

	/*
	 * V3/V6 set ctrl mode command is different,
	 * so make sure V3/V6 protocol first.
	 */
	if ((error = api_update_protocol_ver(dev)) < 0 ||
	    (error = api_set_ctrl_mode(dev, mode_suspend, false)) < 0 ||
	    (error = api_protocol_set_cmd(dev, GET_MCU_MOD, NULL)) < 0 ||
	    (error = api_protocol_set_cmd(dev, GET_MCU_VER, NULL)) < 0 ||
	    (error = api_protocol_set_cmd(dev, GET_FW_VER, NULL)) < 0 ||
	    (error = api_protocol_set_cmd(dev, GET_AP_CRC, NULL)) < 0)
		return error;

	if (dev->protocol.flag == PTL_V6 &&
	    ((error = api_protocol_set_cmd(dev, GET_PRODUCT_INFO, NULL)) < 0 ||
	     (error = api_protocol_set_cmd(dev, GET_FWID, NULL)) < 0 ||
	     (error = api_protocol_set_cmd(dev, GET_SENSOR_ID, NULL)) < 0 ||
	     (error = api_protocol_set_cmd(dev, GET_HID_INFO, NULL)) < 0))
		return error;

	/* BL mode should perform FW upgrade afterward */
	if (dev->ic[0].mode != 0x5A)
		return 0;

	/* V3 need to get DF CRC */
	if (dev->protocol.flag == PTL_V3 &&
	    (error = api_protocol_set_cmd(dev, GET_DF_CRC, NULL)) < 0)
		return error;

	if ((error = api_protocol_set_cmd(dev, GET_CORE_VER, NULL)) < 0 ||
	    (error = api_protocol_set_cmd(dev, GET_SCRN_RES, NULL)) < 0 ||
	    (error = api_protocol_set_cmd(dev, GET_TP_INFO, NULL)) < 0 ||
	    (error = api_get_func_mode(dev)) < 0)
		return error;

	if (dev->tp_info.ic_num > 1 &&
	    ((error = api_protocol_set_cmd(dev, GET_AP_CRC,
	    	&dev->tp_info.ic_num)) < 0 ||
	     (error = api_protocol_set_cmd(dev, GET_MCU_MOD,
		&dev->tp_info.ic_num)) < 0))
		return error;

	if ((error = api_set_ctrl_mode(dev, mode_normal, false)) < 0)
		return error;

	return 0;
}

void __ilitek_get_ts_info(void *handle, struct ilitek_tp_info_v6 *tp_info)
{
	struct ilitek_ts_device *dev = (struct ilitek_ts_device *)handle;

	if (!tp_info || !dev)
		return;

	memcpy(tp_info, &dev->tp_info, sizeof(struct ilitek_tp_info_v6));
}

void ilitek_dev_set_quirks(void *handle, uint32_t quirks)
{
	struct ilitek_ts_device *dev = (struct ilitek_ts_device *)handle;

	if (!handle)
		return;

	dev->quirks = quirks;
}

void ilitek_dev_set_sys_info(void *handle, struct ilitek_sys_info *sys)
{
	struct ilitek_ts_device *dev = (struct ilitek_ts_device *)handle;

	if (!handle)
		return;

	memcpy(&dev->sys, sys, sizeof(struct ilitek_sys_info));
}

void ilitek_dev_setting(void *handle, struct ilitek_ts_settings *setting)
{
	struct ilitek_ts_device *dev = (struct ilitek_ts_device *)handle;

	if (!handle)
		return;

	memcpy(&dev->setting, setting, sizeof(struct ilitek_ts_settings));

	TP_MSG(dev->id, "no-retry: %d, no-INT-ack: %d\n",
		dev->setting.no_retry, dev->setting.no_INT_ack);
}

void *ilitek_dev_init(uint8_t _interface, char *id,
		      struct ilitek_ts_callback *callback, void *_private)
{
	struct ilitek_ts_device *dev;

	dev = (struct ilitek_ts_device *)MALLOC(sizeof(*dev));
	if (!dev)
		return NULL;

	TP_MSG(NULL, "commonflow code version: %#x\n",
		COMMONFLOW_CODE_VERSION);

	/* initial all member to 0/ false/ NULL */
	memset(dev, 0, sizeof(*dev));

	_strcpy(dev->id, id, sizeof(dev->id));
	if (callback) {
		memcpy(&dev->cb, callback, sizeof(struct ilitek_ts_callback));
		if (dev->cb.msg)
			g_msg = dev->cb.msg;
	}

	dev->_interface = _interface;
	dev->_private = _private;

	return dev;
}

void ilitek_dev_exit(void *handle)
{
	struct ilitek_ts_device *dev = (struct ilitek_ts_device *)handle;

	/*
	 * LIBUSB would kill /dev/hidraw* and make system stop handling
	 * device's usb. sw reset is required to re-enum usb then /dev/hidraw*
	 * would be created and system would start to handle touch event.
	 */
	if (dev->sys.libusb)
		api_protocol_set_cmd(dev, SET_SW_RST, NULL);

	if (dev)
		FREE(dev);
}
