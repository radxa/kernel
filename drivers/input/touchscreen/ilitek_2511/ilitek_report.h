/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This file is part of ILITEK CommonFlow
 *
 * Copyright (c) 2022 ILI Technology Corp.
 * Copyright (c) 2022 Luca Hsu <luca_hsu@ilitek.com>
 * Copyright (c) 2022 Joe Hung <joe_hung@ilitek.com>
 */

#ifndef __ILITEK_REPORT_H__
#define __ILITEK_REPORT_H__

#include "ilitek_def.h"
#include "ilitek_protocol.h"

typedef void(*report_touch_event_t)(uint8_t, struct touch_fmt[40], void *);
typedef void(*report_pen_event_t)(struct pen_fmt *, void *);
typedef void(*report_dmsg_t)(char *, int, void *);
typedef void(*report_buf_t)(uint8_t *, int, void *);

struct ilitek_report_callback {
	/* touch/pen event report */
	report_touch_event_t report_touch_event;
	report_pen_event_t report_pen_event;
	report_dmsg_t report_dmsg;
	report_buf_t report_buf;
};

struct ilitek_report {
	struct ilitek_report_callback cb;
	void *_private;
};

#ifdef __cplusplus
extern "C" {
#endif
	int __DLL ilitek_update_report(struct ilitek_ts_device *dev,
				       struct ilitek_report *report);

#ifdef __cplusplus
}
#endif

#endif
