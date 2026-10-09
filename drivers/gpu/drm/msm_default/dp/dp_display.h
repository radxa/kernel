/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2017-2020, The Linux Foundation. All rights reserved.
 */

#ifndef _DP_DISPLAY_H_
#define _DP_DISPLAY_H_

#include "dp_panel.h"
#include "dp_mst_drm.h"
#include <sound/hdmi-codec.h>
#include "disp/msm_disp_snapshot.h"

#define DP_MAX_PIXEL_CLK_KHZ	675000

struct msm_dp {
	struct drm_device *drm_dev;
	struct platform_device *pdev;
	struct device *codec_dev;
	unsigned int id;
	struct drm_connector *connector;
	struct drm_bridge *next_bridge;
	bool link_ready;
	bool audio_enabled;
	bool audio_stream_active;
	bool prepared;
	bool mst_active;
	unsigned int connector_type;
	bool is_edp;
	bool internal_hpd;

	struct msm_dp_mst *msm_dp_mst;
	hdmi_codec_plugged_cb plugged_cb;

	struct msm_dp_audio *msm_dp_audio;
	bool psr_supported;
};

int msm_dp_display_set_plugged_cb(struct msm_dp *msm_dp_display,
		hdmi_codec_plugged_cb fn, struct device *codec_dev);
int msm_dp_display_get_modes(struct msm_dp *msm_dp_display);
bool msm_dp_display_check_video_test(struct msm_dp *msm_dp_display);
int msm_dp_display_get_test_bpp(struct msm_dp *msm_dp_display);
void msm_dp_display_signal_audio_start(struct msm_dp *msm_dp_display);
void msm_dp_display_signal_audio_complete(struct msm_dp *msm_dp_display);
void msm_dp_display_set_psr(struct msm_dp *dp, bool enter);
void msm_dp_display_debugfs_init(struct msm_dp *msm_dp_display, struct dentry *dentry, bool is_edp);
void msm_dp_display_atomic_post_disable(struct msm_dp *dp_display);
void msm_dp_display_atomic_disable(struct msm_dp *dp_display);
void msm_dp_display_atomic_enable(struct msm_dp *dp_display);
void msm_dp_display_audio_reenable(struct msm_dp *dp_display);
void msm_dp_display_atomic_prepare(struct msm_dp *dp);
void msm_dp_display_mode_set(struct msm_dp *dp,
			     const struct drm_display_mode *mode,
			     const struct drm_display_mode *adjusted_mode);
enum drm_mode_status msm_dp_display_mode_valid(struct msm_dp *dp,
					       const struct drm_display_info *info,
					       const struct drm_display_mode *mode);
int msm_dp_display_set_stream_info(struct msm_dp *dp,
				   struct msm_dp_panel *panel, u32 strm_id,
				   u32 start_slot, u32 num_slots, u32 pbn, int vcpi);
void msm_dp_display_enable_helper(struct msm_dp *msm_dp, struct msm_dp_panel *msm_dp_panel);
void msm_dp_display_disable_helper(struct msm_dp *msm_dp, struct msm_dp_panel *msm_dp_panel);
void msm_dp_display_mode_set_helper(struct msm_dp *msm_dp,
				    const struct drm_display_mode *mode,
				    const struct drm_display_mode *adjusted_mode,
				    struct msm_dp_panel *msm_dp_panel);
void msm_dp_display_atomic_post_disable_helper(struct msm_dp *msm_dp,
					       struct msm_dp_panel *msm_dp_panel);

void msm_dp_display_unprepare(struct msm_dp *dp);

int msm_dp_display_get_active_stream_cnt(struct msm_dp *msm_dp);

struct msm_dp_panel *msm_dp_display_get_panel(struct msm_dp *dp_display);

#endif /* _DP_DISPLAY_H_ */
