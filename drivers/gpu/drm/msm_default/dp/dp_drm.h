/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2017-2020, The Linux Foundation. All rights reserved.
 */

#ifndef _DP_DRM_H_
#define _DP_DRM_H_

#include <linux/types.h>
#include <linux/workqueue.h>
#include <drm/drm_bridge.h>

#include "msm_drv.h"
#include "dp_display.h"

struct msm_dp_bridge {
	struct drm_bridge bridge;
	struct msm_dp *msm_dp_display;
	struct work_struct audio_work;
	struct drm_crtc_commit *audio_commit;
};

#define to_dp_bridge(x)     container_of((x), struct msm_dp_bridge, bridge)

struct drm_connector *msm_dp_drm_connector_init(struct msm_dp *msm_dp_display,
					    struct drm_encoder *encoder);
int msm_dp_bridge_init(struct msm_dp *msm_dp_display, struct drm_device *dev,
		   struct drm_encoder *encoder,
		   bool yuv_supported);

void msm_dp_bridge_hpd_enable(struct drm_bridge *bridge);
void msm_dp_bridge_hpd_disable(struct drm_bridge *bridge);
void msm_dp_bridge_hpd_notify(struct drm_bridge *bridge,
			  enum drm_connector_status status);

#endif /* _DP_DRM_H_ */
