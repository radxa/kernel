// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <drm/drm_edid.h>
#include <drm/drm_fixed.h>
#include <drm/drm_atomic_helper.h>
#include <drm/display/drm_dp_mst_helper.h>
#include <linux/pm_runtime.h>

#include "dp_mst_drm.h"
#include "dp_panel.h"

#define to_dp_mst_connector(x) \
		container_of((x), struct msm_dp_mst_connector, connector)

struct msm_dp_mst_connector {
	struct drm_connector connector;
	struct drm_dp_mst_port *mst_port;
	struct msm_dp_mst *dp_mst;
};

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

static void dp_mst_connector_destroy(struct drm_connector *connector)
{
	struct msm_dp_mst_connector *mst_conn = to_dp_mst_connector(connector);

	drm_connector_cleanup(connector);
	drm_dp_mst_put_port_malloc(mst_conn->mst_port);
	kfree(mst_conn);
}

static const struct drm_connector_funcs msm_dp_drm_mst_connector_funcs = {
	.reset = drm_atomic_helper_connector_reset,
	.destroy = dp_mst_connector_destroy,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

/* DP MST Connector OPs */
static int
msm_dp_mst_connector_detect(struct drm_connector *connector,
			    struct drm_modeset_acquire_ctx *ctx,
			    bool force)
{
	struct msm_dp_mst_connector *mst_conn = to_dp_mst_connector(connector);
	struct msm_dp_mst *mst = mst_conn->dp_mst;
	struct msm_dp *dp_display = mst->msm_dp;
	struct device *dev = &dp_display->pdev->dev;
	enum drm_connector_status status = connector_status_disconnected;
	int ret;

	if (!dp_display->mst_active)
		return status;

	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0)
		return status;

	status = drm_dp_mst_detect_port(connector, ctx, &mst->mst_mgr,
					mst_conn->mst_port);

	pm_runtime_put_autosuspend(dev);

	return status;
}

static int msm_dp_mst_connector_get_modes(struct drm_connector *connector)
{
	struct msm_dp_mst_connector *mst_conn = to_dp_mst_connector(connector);
	struct msm_dp_mst *mst = mst_conn->dp_mst;
	const struct drm_edid *drm_edid;
	int rc;

	drm_edid = drm_dp_mst_edid_read(connector, &mst->mst_mgr, mst_conn->mst_port);
	drm_edid_connector_update(connector, drm_edid);

	rc = drm_edid_connector_add_modes(connector);

	drm_edid_free(drm_edid);

	return rc;
}

static enum drm_mode_status msm_dp_mst_connector_mode_valid(struct drm_connector *connector,
							    const struct drm_display_mode *mode)
{
	struct msm_dp_mst_connector *mst_conn;
	struct drm_dp_mst_port *mst_port;
	struct msm_dp *dp_display;
	int required_pbn;

	if (drm_connector_is_unregistered(connector))
		return 0;

	mst_conn = to_dp_mst_connector(connector);
	mst_port = mst_conn->mst_port;
	dp_display = mst_conn->dp_mst->msm_dp;

	/* Use minimum possible bpp (6bpc RGB, 8b10b) to avoid rejecting modes
	 * that would be feasible at the actual link bpp. mode_valid must not
	 * depend on negotiated parameters.
	 */
	required_pbn = drm_dp_calc_pbn_mode(mode->clock, (6 * 3) << 4);

	if (required_pbn > mst_port->full_pbn) {
		drm_dbg_dp(dp_display->drm_dev, "mode:%s not supported.\n", mode->name);
		return MODE_CLOCK_HIGH;
	}

	return msm_dp_display_mode_valid(dp_display, &connector->display_info, mode);
}

static int msm_dp_mst_connector_atomic_check(struct drm_connector *connector,
					     struct drm_atomic_commit *state)
{
	struct msm_dp_mst_connector *mst_conn = to_dp_mst_connector(connector);
	struct msm_dp_mst *mst = mst_conn->dp_mst;

	return drm_dp_atomic_release_time_slots(state, &mst->mst_mgr, mst_conn->mst_port);
}

static int msm_dp_mst_encoder_stream_id(struct msm_dp_mst *mst,
					struct drm_encoder *enc)
{
	int i;

	for (i = 0; i < mst->max_streams; i++) {
		if (mst->mst_encoders[i].enc == enc)
			return mst->mst_encoders[i].stream_id;
	}
	return -1;
}

int msm_dp_mst_attach_encoder(struct msm_dp *dp_display, unsigned int stream_id,
			      struct drm_encoder *encoder)
{
	struct msm_dp_mst *mst = dp_display->msm_dp_mst;
	struct msm_dp_panel *dp_panel;

	dp_panel = msm_dp_display_get_panel(dp_display, stream_id);
	if (!dp_panel) {
		drm_err(dp_display->drm_dev,
			"[MST] failed to allocate panel for stream %d\n", stream_id);
		return -ENOMEM;
	}

	mst->mst_encoders[stream_id].enc = encoder;
	mst->mst_encoders[stream_id].stream_id = stream_id;
	mst->mst_encoders[stream_id].dp_panel = dp_panel;

	return 0;
}

static struct drm_encoder *
msm_dp_mst_atomic_best_encoder(struct drm_connector *connector, struct drm_atomic_commit *state)
{
	struct msm_dp_mst_connector *mst_conn = to_dp_mst_connector(connector);
	struct msm_dp_mst *mst = mst_conn->dp_mst;
	struct drm_connector_state *conn_state;
	struct drm_connector *iter;
	struct drm_connector_list_iter conn_iter;
	u32 stream_mask = 0;
	u32 i;

	conn_state = drm_atomic_get_new_connector_state(state, connector);
	if (!conn_state)
		return NULL;

	if (conn_state->best_encoder)
		return conn_state->best_encoder;

	drm_connector_list_iter_begin(connector->dev, &conn_iter);
	drm_for_each_connector_iter(iter, &conn_iter) {
		struct drm_connector_state *peer_state;
		int stream_id;

		if (iter == connector ||
		    iter->funcs != &msm_dp_drm_mst_connector_funcs ||
		    to_dp_mst_connector(iter)->dp_mst != mst)
			continue;

		peer_state = drm_atomic_get_new_connector_state(state, iter) ?: iter->state;
		if (!peer_state || !peer_state->crtc || !peer_state->best_encoder)
			continue;

		stream_id = msm_dp_mst_encoder_stream_id(mst, peer_state->best_encoder);
		if (stream_id >= 0 && stream_id < mst->max_streams)
			stream_mask |= BIT(stream_id);
	}
	drm_connector_list_iter_end(&conn_iter);

	for (i = 0; i < mst->max_streams; i++) {
		if (!(stream_mask & BIT(i))) {
			conn_state->best_encoder = mst->mst_encoders[i].enc;
			return mst->mst_encoders[i].enc;
		}
	}

	return NULL;
}

/* DRM MST callbacks */
static const struct drm_connector_helper_funcs msm_dp_drm_mst_connector_helper_funcs = {
	.get_modes =    msm_dp_mst_connector_get_modes,
	.detect_ctx =   msm_dp_mst_connector_detect,
	.mode_valid =   msm_dp_mst_connector_mode_valid,
	.atomic_best_encoder = msm_dp_mst_atomic_best_encoder,
	.atomic_check = msm_dp_mst_connector_atomic_check,
};

static struct drm_connector *
msm_dp_mst_add_connector(struct drm_dp_mst_topology_mgr *mgr,
			 struct drm_dp_mst_port *port, const char *pathprop)
{
	struct msm_dp_mst *mst = container_of(mgr, struct msm_dp_mst, mst_mgr);
	struct drm_device *dev = mst->msm_dp->drm_dev;
	struct msm_dp_mst_connector *mst_conn;
	struct drm_connector *connector;
	int rc, i;

	mst_conn = kzalloc_obj(*mst_conn);
	if (!mst_conn)
		return NULL;

	connector = &mst_conn->connector;
	rc = drm_connector_dynamic_init(dev, connector,
					&msm_dp_drm_mst_connector_funcs,
					DRM_MODE_CONNECTOR_DisplayPort, NULL);
	if (rc)
		goto err_free;

	mst_conn->dp_mst = mst;

	drm_connector_helper_add(connector, &msm_dp_drm_mst_connector_helper_funcs);
	connector->funcs->reset(connector);

	/* add all encoders as possible encoders */
	for (i = 0; i < mst->max_streams; i++) {
		rc = drm_connector_attach_encoder(connector, mst->mst_encoders[i].enc);
		if (rc) {
			drm_err(dev, "[MST] failed to attach encoder:%u to conn:%d rc:%d\n",
				mst->mst_encoders[i].enc->base.id,
				connector->base.id, rc);
			goto err_connector;
		}
	}

	mst_conn->mst_port = port;
	drm_dp_mst_get_port_malloc(port);

	drm_object_attach_property(&connector->base,
				   dev->mode_config.path_property, 0);
	drm_object_attach_property(&connector->base,
				   dev->mode_config.tile_property, 0);
	drm_connector_set_path_property(connector, pathprop);

	drm_dbg_kms(dev, "[MST] add_connector done conn:%d max_streams:%u\n",
		    connector->base.id, mst->max_streams);

	return connector;

err_connector:
	drm_connector_cleanup(connector);
err_free:
	kfree(mst_conn);
	return NULL;
}

static const struct drm_dp_mst_topology_cbs msm_dp_mst_drm_cbs = {
	.add_connector = msm_dp_mst_add_connector,
};

int msm_dp_mst_mgr_init(struct msm_dp *dp_display, u32 max_streams, struct drm_dp_aux *drm_aux)
{
	struct drm_device *dev = dp_display->drm_dev;
	struct msm_dp_mst *mst;
	int ret;

	mst = kzalloc_obj(*mst);
	if (!mst)
		return -ENOMEM;

	mst->mst_mgr.cbs = &msm_dp_mst_drm_cbs;
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
