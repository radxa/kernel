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
#include "dp_drm.h"
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
	struct msm_dp_link *link;
	struct drm_dp_aux *dp_aux;
	u32 max_streams;
	/* Serializes concurrent stream link-state access across streams. */
	struct mutex mst_lock;
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

static struct msm_dp_panel *msm_dp_mst_panel_from_encoder(struct msm_dp_mst *mst,
							   struct drm_encoder *enc)
{
	int i;

	for (i = 0; i < mst->max_streams; i++) {
		if (mst->mst_encoders[i].enc == enc)
			return mst->mst_encoders[i].dp_panel;
	}
	return NULL;
}

static void msm_dp_mst_collect_slots(struct msm_dp_mst *mst,
				     struct drm_dp_mst_topology_state *mst_state,
				     struct msm_dp_mst_slot_info *slots)
{
	int i;

	for (i = 0; i < mst->max_streams; i++) {
		struct msm_dp_panel *dp_panel = mst->mst_encoders[i].dp_panel;
		struct drm_dp_mst_atomic_payload *payload;

		if (!dp_panel->connector)
			continue;

		payload = drm_atomic_get_mst_payload_state(mst_state,
				to_dp_mst_connector(dp_panel->connector)->mst_port);
		if (!payload || payload->vc_start_slot < 0)
			continue;

		slots[dp_panel->stream_id].start_slot = payload->vc_start_slot;
		slots[dp_panel->stream_id].tot_slots = payload->time_slots;
		slots[dp_panel->stream_id].pbn = payload->pbn;
	}
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

void msm_dp_mst_stream_enable(struct drm_encoder *encoder,
			       struct drm_atomic_commit *state)
{
	struct drm_connector *connector =
		drm_atomic_get_new_connector_for_encoder(state, encoder);
	struct msm_dp_mst_connector *mst_conn = to_dp_mst_connector(connector);
	struct msm_dp_mst *mst = mst_conn->dp_mst;
	struct msm_dp *dp_display = mst->msm_dp;
	struct msm_dp_panel *panel = msm_dp_mst_panel_from_encoder(mst, encoder);
	struct drm_dp_mst_port *port = mst_conn->mst_port;
	struct drm_dp_mst_topology_state *mst_state =
		drm_atomic_get_new_mst_topology_state(state, &mst->mst_mgr);
	struct drm_dp_mst_atomic_payload *payload =
		drm_atomic_get_mst_payload_state(mst_state, port);
	struct msm_dp_mst_slot_info slots[DP_STREAM_MAX] = {};
	struct msm_dp_bridge_state dp_state = { .bpp = 24 };
	int rc;

	guard(mutex)(&mst->mst_lock);

	drm_connector_get(connector);
	panel->connector = connector;

	rc = msm_dp_display_set_mode_helper(dp_display, state, encoder, panel, &dp_state);
	if (rc) {
		drm_err(dp_display->drm_dev,
			"[MST] stream:%u set_mode failed rc=%d\n", panel->stream_id, rc);
		return;
	}

	rc = msm_dp_display_prepare_link(dp_display, &dp_state);
	if (rc) {
		drm_err(dp_display->drm_dev,
			"[MST] stream:%u prepare_link failed rc=%d\n", panel->stream_id, rc);
		return;
	}

	drm_dp_mst_update_slots(mst_state, DP_CAP_ANSI_8B10B);

	rc = drm_dp_add_payload_part1(&mst->mst_mgr, mst_state, payload);
	if (rc)
		return;

	msm_dp_mst_collect_slots(mst, mst_state, slots);

	drm_dbg_kms(dp_display->drm_dev,
		    "[MST] stream:%u timeslots vc_start:%d slots:%d pbn:%d\n",
		    panel->stream_id, payload->vc_start_slot,
		    payload->time_slots, payload->pbn);

	rc = msm_dp_display_enable_helper(dp_display, panel, slots, &dp_state);
	if (rc)
		return;

	drm_dp_check_act_status(&mst->mst_mgr);

	drm_dp_add_payload_part2(&mst->mst_mgr, payload);
}

void msm_dp_mst_stream_disable(struct drm_encoder *encoder,
			       struct drm_atomic_commit *state)
{
	struct drm_connector *connector = drm_atomic_get_old_connector_for_encoder(state, encoder);
	struct msm_dp_mst_connector *mst_conn = to_dp_mst_connector(connector);
	struct msm_dp_mst *mst = mst_conn->dp_mst;
	struct msm_dp_panel *panel = msm_dp_mst_panel_from_encoder(mst, encoder);
	struct drm_dp_mst_topology_state *old_mst_state =
		drm_atomic_get_old_mst_topology_state(state, &mst->mst_mgr);
	struct drm_dp_mst_topology_state *new_mst_state =
		drm_atomic_get_new_mst_topology_state(state, &mst->mst_mgr);
	struct drm_dp_mst_atomic_payload *old_payload =
		drm_atomic_get_mst_payload_state(old_mst_state, mst_conn->mst_port);
	struct drm_dp_mst_atomic_payload *new_payload =
		drm_atomic_get_mst_payload_state(new_mst_state, mst_conn->mst_port);
	struct msm_dp_mst_slot_info slots[DP_STREAM_MAX] = {};

	guard(mutex)(&mst->mst_lock);

	drm_dp_remove_payload_part1(&mst->mst_mgr, new_mst_state, new_payload);

	drm_dp_remove_payload_part2(&mst->mst_mgr, new_mst_state, old_payload, new_payload);

	msm_dp_mst_collect_slots(mst, new_mst_state, slots);

	drm_dbg_kms(mst->msm_dp->drm_dev,
		    "[MST] stream:%u timeslots vc_start:%d slots:%d pbn:%d\n",
		    panel->stream_id, new_payload->vc_start_slot,
		    new_payload->time_slots, new_payload->pbn);

	if (!msm_dp_display_is_plugged(mst->msm_dp))
		return;

	msm_dp_display_disable_helper(mst->msm_dp, panel, slots);

	drm_dp_check_act_status(&mst->mst_mgr);
}

void msm_dp_mst_stream_post_disable(struct drm_encoder *encoder,
				    struct drm_atomic_commit *state)
{
	struct drm_connector *connector = drm_atomic_get_old_connector_for_encoder(state, encoder);
	struct msm_dp_mst_connector *mst_conn = to_dp_mst_connector(connector);
	struct msm_dp_mst *mst = mst_conn->dp_mst;
	struct msm_dp_panel *panel = msm_dp_mst_panel_from_encoder(mst, encoder);

	guard(mutex)(&mst->mst_lock);

	msm_dp_display_atomic_post_disable_helper(mst->msm_dp, panel);

	if (!mst->msm_dp->mst_active)
		msm_dp_display_unprepare(mst->msm_dp);

	panel->connector = NULL;

	drm_connector_put(connector);
}

int msm_dp_mst_stream_atomic_check(struct drm_encoder *enc,
				   struct drm_crtc_state *crtc_state,
				   struct drm_connector_state *conn_state)
{
	struct msm_dp_mst_connector *mst_conn = to_dp_mst_connector(conn_state->connector);
	struct msm_dp_mst *mst = mst_conn->dp_mst;
	struct drm_dp_mst_topology_state *mst_state;
	int bpp, pbn, slots;

	if (!conn_state->crtc)
		return 0;

	if (!drm_atomic_crtc_needs_modeset(crtc_state) || !crtc_state->active)
		return 0;

	bpp = (conn_state->connector->display_info.bpc * 3) ?: 24; /* fallback: assume 8bpc */
	pbn = drm_dp_calc_pbn_mode(crtc_state->mode.clock, bpp << 4);

	mst_state = drm_atomic_get_mst_topology_state(crtc_state->state, &mst->mst_mgr);
	if (IS_ERR(mst_state))
		return PTR_ERR(mst_state);

	if (!dfixed_trunc(mst_state->pbn_div)) {
		mst_state->pbn_div =
			drm_dp_get_vc_payload_bw(mst->link->link_caps.rate,
						 mst->link->link_caps.num_lanes);
	}

	slots = drm_dp_atomic_find_time_slots(crtc_state->state, &mst->mst_mgr,
					      mst_conn->mst_port, pbn);
	if (slots < 0)
		return slots;

	return 0;
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

int msm_dp_mst_mgr_init(struct msm_dp *dp_display, struct msm_dp_link *link,
			 u32 max_streams, struct drm_dp_aux *drm_aux)
{
	struct drm_device *dev = dp_display->drm_dev;
	struct msm_dp_mst *mst;
	int ret;

	mst = kzalloc_obj(*mst);
	if (!mst)
		return -ENOMEM;

	mst->mst_mgr.cbs = &msm_dp_mst_drm_cbs;
	mst->msm_dp = dp_display;
	mst->link = link;
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

	mutex_init(&mst->mst_lock);
	dp_display->msm_dp_mst = mst;
	return 0;
}

void msm_dp_mst_mgr_destroy(struct msm_dp *dp_display)
{
	struct msm_dp_mst *mst = dp_display->msm_dp_mst;

	if (!mst)
		return;

	drm_dp_mst_topology_mgr_destroy(&mst->mst_mgr);
	mutex_destroy(&mst->mst_lock);
	dp_display->msm_dp_mst = NULL;
	kfree(mst);
}
