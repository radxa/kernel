// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <drm/drm_edid.h>
#include <drm/drm_fixed.h>
#include <drm/drm_atomic_helper.h>
#include <drm/display/drm_dp_mst_helper.h>

#include "dp_mst_drm.h"
#include "dp_panel.h"

struct msm_dp_mst_encoder {
	struct drm_encoder *enc;
	int stream_id;
	struct msm_dp_panel *dp_panel;
};

struct msm_dp_mst {
	struct drm_dp_mst_topology_mgr mst_mgr;
	struct msm_dp_mst_encoder mst_encoders[DP_STREAM_MAX];
	struct msm_dp *msm_dp;
	struct drm_dp_aux *dp_aux;
	u32 max_streams;
};

int msm_dp_mst_mgr_init(struct msm_dp *dp_display, u32 max_streams, struct drm_dp_aux *drm_aux)
{
	struct drm_device *dev = dp_display->drm_dev;
	struct msm_dp_mst *mst;
	int ret;

	mst = kzalloc_obj(*mst);
	if (!mst)
		return -ENOMEM;

	mst->msm_dp = dp_display;
	mst->max_streams = max_streams;
	mst->dp_aux = drm_aux;

	ret = drm_dp_mst_topology_mgr_init(&mst->mst_mgr, dev,
					   drm_aux,
					   16,
					   max_streams,
					   dp_display->connector->base.id);
	if (ret) {
		drm_err(dev, "[MST] topology manager init failed\n");
		kfree(mst);
		return ret;
	}

	dp_display->msm_dp_mst = mst;
	return 0;
}

void msm_dp_mst_mgr_destroy(struct msm_dp *dp_display)
{
	struct msm_dp_mst *mst = dp_display->msm_dp_mst;

	if (!mst)
		return;

	drm_dp_mst_topology_mgr_destroy(&mst->mst_mgr);
	dp_display->msm_dp_mst = NULL;
	kfree(mst);
}
