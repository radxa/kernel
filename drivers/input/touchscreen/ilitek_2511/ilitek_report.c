// SPDX-License-Identifier: GPL-2.0
/*
 * This file is part of ILITEK CommonFlow
 *
 * Copyright (c) 2022 ILI Technology Corp.
 * Copyright (c) 2022 Luca Hsu <luca_hsu@ilitek.com>
 * Copyright (c) 2022 Joe Hung <joe_hung@ilitek.com>
 */

#include "ilitek_report.h"
#include "ilitek_crypto.h"

static bool is_debug_packet_id(uint8_t id, uint8_t _interface)
{
	return ((_interface == interface_i2c && id == 0xdb) ||
		(_interface == interface_usb && id == 0xaa));
}

static bool is_pen_packet_id(uint8_t id)
{
	return (id == 0x0c || id == 0x0d);
}

static void touch_decode(struct ilitek_ts_device *dev,
			 struct ilitek_report *report,
			 uint8_t *buf, uint8_t cnt)
{
	struct touch_fmt touch[40], *parser;
	uint8_t i, j;

	memset(touch, 0, sizeof(touch));

	for (i = 0; i < cnt && i < dev->tp_info.max_fingers; i++) {
		if (dev->protocol.flag == PTL_V3 &&
		    dev->_interface == interface_i2c) {
			touch[i].id = i;
			touch[i].status = buf[1 + i * 5] >> 7;
			touch[i].x = ((buf[1 + i * 5] & 0x3F) << 8) +
				buf[2 + i * 5];
			touch[i].y = ((buf[3 + i * 5] & 0x3F) << 8) +
				buf[4 + i * 5];

			touch[i].p = 10;
			touch[i].h = 128;
			touch[i].w = 1;
		} else {
			parser = (struct touch_fmt *)(buf + 1 +
						      i * dev->finger.size);
			touch[i].id = parser->id;
			touch[i].status = parser->status;
			touch[i].x = parser->x;
			touch[i].y = parser->y;

			switch (dev->tp_info.format) {
			case touch_fmt_1:
				touch[i].p = parser->p;
				break;
			case touch_fmt_2:
				touch[i].w = parser->w;
				touch[i].h = parser->h;
				break;
			case touch_fmt_3:
				touch[i].p = parser->p;
				touch[i].w = parser->w;
				touch[i].h = parser->h;
				break;
			}
		}

		TP_DBG(dev->id, "[touch-report] id:%hhu, status:%hhu, "
			"x:%hu, y:%hu, p:%hhu, w:%hu, h:%hu\n",
			touch[i].id, touch[i].status, touch[i].x, touch[i].y,
			touch[i].p, touch[i].w, touch[i].h);

		if (touch[i].id >= dev->tp_info.max_fingers) {
			TP_ERR(dev->id, "invalid touch id: %hhu >= %hhu\n",
				touch[i].id, dev->tp_info.max_fingers);
			return;
		}

		/*
		 * if x/y within key's range, skip touch range check.
		 */
		for (j = 0; j < dev->tp_info.key_num; j++) {
			if ((touch[i].x < dev->key.info.keys[j].x &&
			     touch[i].x > dev->key.info.keys[j].x +
			     	dev->key.info.x_len) &&
			    (touch[i].y < dev->key.info.keys[j].y &&
			     touch[i].y > dev->key.info.keys[j].y +
			     	dev->key.info.y_len))
				continue;

			goto skip_touch_range_check;
		}

		if (touch[i].status &&
		    (touch[i].x > dev->screen_info.x_max ||
		     touch[i].y > dev->screen_info.y_max ||
		     touch[i].x < dev->screen_info.x_min ||
		     touch[i].y < dev->screen_info.y_min)) {
			TP_ERR(dev->id, "Point[%d]: (%d, %d), Limit: (%d:%d, %d:%d) OOB\n",
				touch[i].id, touch[i].x, touch[i].y,
				dev->screen_info.x_min, dev->screen_info.x_max,
				dev->screen_info.y_min, dev->screen_info.y_max);
			return;
		}

skip_touch_range_check:
		continue;
	}

	/*
	 * report touch event callback,
	 * which includes actual count of finger report just parsed above.
	 */
	if (report->cb.report_touch_event)
		report->cb.report_touch_event(i, touch, report->_private);
}

static void pen_decode(struct ilitek_ts_device *dev,
		       struct ilitek_report *report,
		       uint8_t *buf)
{
	struct pen_fmt pen, *parser = (struct pen_fmt *)(buf + 1);

	memcpy(&pen, parser, sizeof(struct pen_fmt));

	TP_DBG(dev->id, "[stylus-report] state:0x%hhx, x:%hu, y:%hu, "
		"pressure: %hu, x_tilt: %hd, y_tilt: %hd\n",
		pen.modes, pen.x, pen.y, pen.pressure, pen.x_tilt, pen.y_tilt);

	/* report pen event callback */
	if (report->cb.report_pen_event)
		report->cb.report_pen_event(&pen, report->_private);
}

static void dmsg_decode(struct ilitek_ts_device *dev,
			struct ilitek_report *report,
			uint8_t *buf, int buf_size)
{
	if ((int)buf[1] >= buf_size)
		return;

	buf[buf[1]] = '\0';
	TP_DBG(dev->id, "%s\n", (char *)(buf + 2));

	if (report->cb.report_dmsg)
		report->cb.report_dmsg((char *)(buf + 2),
					buf_size - 2, report->_private);
}

static bool is_checksum_matched(uint8_t checksum, int start, int end,
				uint8_t *buf, int buf_size)
{
	uint8_t check;

	check = ~(get_checksum(start, end, buf, buf_size)) + 1;
	if (check != checksum) {
		TP_ERR_ARR(NULL, "[data]", TYPE_U8, end - start, buf + start);
		TP_ERR(NULL, "checksum : %hhx/%hhx not matched\n",
			check, checksum);
		return false;
	}

	return true;
}

static void report_update_buf(struct ilitek_report *report,
			      uint8_t *buf)
{
	if (report->cb.report_buf)
		report->cb.report_buf(buf, 64, report->_private);
}

/* return touch finger's count or negative value as error code */
static int report_get_raw_v3(struct ilitek_ts_device *dev,
			     struct ilitek_report *report,
			     uint8_t *buf, int buf_size)
{
	int error;
	uint8_t mode_status;

	uint8_t cnt;

	UNUSED(buf_size);

	if (dev->_interface == interface_i2c) {
		if (dev->quirks & DEV_QUIRK_DAEMON_V3_I2C_REPORT_HANDLING) {
			if ((error = read_interrupt_in(dev, buf, 64, 1000)) < 0)
				return error;
		} else {
			dev->wbuf[0] = 0x10;
			if ((error = write_then_read(dev, dev->wbuf, 1,
						     buf, 32)) < 0)
				return error;

			mode_status = buf[31];
			buf[31] = 0;

			if (buf[0] == 2 &&
			    (error = write_then_read(dev, NULL, 0,
			    			     buf + 31, 20)) < 0)
				return error;

			buf[62] = mode_status;
		}

		switch (dev->rbuf[0]) {
		default:
		case 1: cnt = 6; break;
		case 0: cnt = 0; break;
		case 2: cnt = 10; break;
		}
	} else {
		if ((error = read_interrupt_in(dev, buf, 64, 1000)) < 0)
			return error;

		cnt = buf[55];
	}

	report_update_buf(report, buf);

	return cnt;
}

/* return touch finger's count or negative value as error code */
static int report_get_raw_v6(struct ilitek_ts_device *dev,
			     struct ilitek_report *report,
			     uint8_t *buf, int buf_size)
{
	int error;
	uint8_t i, cnt, size, max_cnt, tmp;

	size = dev->finger.size;
	max_cnt = dev->finger.max_cnt;

	if ((error = read_interrupt_in(dev, buf, 64, 1000)) < 0)
		return error;

	if (dev->tp_info.format == 5)
		ilitek_decrypt(buf + 1, 48);

	/*
	 * don't check checksum for debug packet and pen packet w/ USB.
	 */
	if (!is_debug_packet_id(buf[0], dev->_interface) &&
	    !(dev->_interface == interface_usb && is_pen_packet_id(buf[0])) &&
	    !is_checksum_matched(buf[63], 0, 63, buf, buf_size))
		return -EPROTO;

	report_update_buf(report, buf);

	/*
	 * no need to check contact count byte for debug packet and pen packet.
	 */
	if (is_pen_packet_id(buf[0]) ||
	    is_debug_packet_id(buf[0], dev->_interface))
		return 0;

	cnt = dev->rbuf[61];
	for (i = 1; i < DIV_ROUND_UP(cnt, max_cnt); i++) {
		tmp = dev->rbuf[i * size * max_cnt];

		if ((error = read_interrupt_in(dev, buf + i * size * max_cnt,
					       64, 1000)) < 0)
			return error;

		if (dev->tp_info.format == 5)
			ilitek_decrypt(buf + 1, 48);

		if (!is_checksum_matched(buf[i * size * max_cnt + 63],
					 0, 63, buf + i * size * max_cnt,
					 buf_size - i * size * max_cnt))
			return -EPROTO;

		report_update_buf(report, buf + i * size * max_cnt);

		dev->rbuf[i * size * max_cnt] = tmp;
	}

	return cnt;
}

int ilitek_update_report(struct ilitek_ts_device *dev,
			 struct ilitek_report *report)
{ 
	int cnt;

	if (!dev)
		return -EINVAL;

	memset(dev->rbuf, 0, sizeof(dev->rbuf));

	switch (dev->protocol.flag) {
	default: return -EPERM;
	case PTL_V3:
		if ((cnt = report_get_raw_v3(dev, report, dev->rbuf,
					     sizeof(dev->rbuf))) < 0)
			return cnt;

		break;

	case PTL_V6:
		if ((cnt = report_get_raw_v6(dev, report, dev->rbuf,
					     sizeof(dev->rbuf))) < 0)
			return cnt;

		/* pen packet (V6 only) */
		if (is_pen_packet_id(dev->rbuf[0])) {
			pen_decode(dev, report, dev->rbuf);
			return 0;
		}

		break;
	}

	/* debug message packet */
	if (is_debug_packet_id(dev->rbuf[0], dev->_interface)) {
		dmsg_decode(dev, report, dev->rbuf, sizeof(dev->rbuf));
		return 0;
	}

	touch_decode(dev, report, dev->rbuf, cnt);

	return 0;
}