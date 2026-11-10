/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2017-2020, The Linux Foundation. All rights reserved.
 */

#ifndef _DP_DISPLAY_H_
#define _DP_DISPLAY_H_

#include "dp_panel.h"
#include "disp/msm_disp_snapshot.h"

#define DP_MAX_PIXEL_CLK_KHZ	675000

struct msm_dp_bridge_state;

struct msm_dp {
	struct drm_device *drm_dev;
	struct platform_device *pdev;
	struct drm_connector *connector;
	struct drm_bridge *next_bridge;
	struct drm_bridge *bridge;
	bool audio_enabled;
	bool audio_restore_pending;
	u32 active_stream_mask;
	bool mst_active;
	bool link_ready;
	unsigned int connector_type;
	bool is_edp;

	void *msm_dp_mst;

	struct msm_dp_audio *msm_dp_audio;
	bool psr_supported;
};

int msm_dp_display_get_modes(struct msm_dp *msm_dp_display);
bool msm_dp_display_check_video_test(struct msm_dp *msm_dp_display);
int msm_dp_display_set_video_test(struct msm_dp *dp, bool enable);
void msm_dp_display_signal_audio_start(struct msm_dp *msm_dp_display);
void msm_dp_display_signal_audio_complete(struct msm_dp *msm_dp_display);
void msm_dp_display_set_psr(struct msm_dp *dp, bool enter);
void msm_dp_display_debugfs_init(struct msm_dp *msm_dp_display, struct dentry *dentry, bool is_edp);
void msm_dp_display_atomic_post_disable(struct msm_dp *dp_display);
void msm_dp_display_atomic_disable(struct msm_dp *dp_display);
void msm_dp_display_atomic_pre_enable(struct msm_dp *dp_display,
				      struct drm_atomic_commit *state);
void msm_dp_display_atomic_enable(struct msm_dp *dp_display,
				  struct drm_atomic_commit *state);
enum drm_mode_status msm_dp_display_mode_valid(struct msm_dp *dp,
					       const struct drm_display_info *info,
					       const struct drm_display_mode *mode);

int msm_dp_display_atomic_check(struct msm_dp *dp, struct msm_dp_bridge_state *state,
				const struct drm_display_mode *mode,
				const struct drm_connector_state *conn_state);

int msm_dp_display_enable_helper(struct msm_dp *msm_dp_display,
				  struct msm_dp_panel *msm_dp_panel,
				  const struct msm_dp_mst_slot_info *slots,
				  const struct msm_dp_bridge_state *state);
void msm_dp_display_disable_helper(struct msm_dp *msm_dp_display,
				   struct msm_dp_panel *msm_dp_panel,
				   const struct msm_dp_mst_slot_info *slots);
void msm_dp_display_atomic_post_disable_helper(struct msm_dp *msm_dp_display,
					       struct msm_dp_panel *msm_dp_panel);
int msm_dp_display_set_mode_helper(struct msm_dp *msm_dp_display,
				   struct drm_atomic_commit *state,
				   struct drm_encoder *drm_encoder,
				   struct msm_dp_panel *msm_dp_panel,
				   const struct msm_dp_bridge_state *dp_state);
int msm_dp_display_prepare_link(struct msm_dp *msm_dp_display,
				const struct msm_dp_bridge_state *state);
void msm_dp_display_unprepare(struct msm_dp *dp);

int msm_dp_get_mst_max_stream(struct msm_dp *msm_dp_display);
int msm_dp_mst_register(struct msm_dp *msm_dp_display);
void msm_dp_mst_unregister(struct msm_dp *msm_dp_display);
struct msm_dp_panel *msm_dp_display_get_panel(struct msm_dp *msm_dp_display,
					      enum msm_dp_stream_id stream_id);
bool msm_dp_display_is_plugged(struct msm_dp *msm_dp_display);
#endif /* _DP_DISPLAY_H_ */
