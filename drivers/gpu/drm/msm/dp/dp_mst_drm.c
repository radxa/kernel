// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <drm/drm_edid.h>
#include "dp_mst_drm.h"

static struct drm_private_state *msm_dp_mst_duplicate_bridge_state(struct drm_private_obj *obj)
{
	struct msm_dp_mst_bridge_state *state;

	state = kmemdup(obj->state, sizeof(*state), GFP_KERNEL);
	if (!state)
		return NULL;

	__drm_atomic_helper_private_obj_duplicate_state(obj, &state->base);

	return &state->base;
}

static void msm_dp_mst_destroy_bridge_state(struct drm_private_obj *obj,
					    struct drm_private_state *state)
{
	struct msm_dp_mst_bridge_state *priv_state =
		to_msm_dp_mst_bridge_priv_state(state);

	kfree(priv_state);
}

static const struct drm_private_state_funcs msm_dp_mst_bridge_state_funcs = {
	.atomic_duplicate_state = msm_dp_mst_duplicate_bridge_state,
	.atomic_destroy_state = msm_dp_mst_destroy_bridge_state,
};

/**
 * dp_mst_find_vcpi_slots() - Find VCPI slots for this PBN value
 * @mgr: manager to use
 * @pbn: payload bandwidth to convert into slots.
 *
 * Calculate the number of VCPI slots that will be required for the given PBN
 * value.
 *
 * RETURNS:
 * The total slots required for this port, or error.
 */
static int msm_dp_mst_find_vcpi_slots(struct drm_dp_mst_topology_mgr *mgr, int pbn)
{
	int num_slots;
	struct drm_dp_mst_topology_state *state;

	state = to_drm_dp_mst_topology_state(mgr->base.state);
	num_slots = DIV_ROUND_UP(dfixed_const(pbn), state->pbn_div.full);

	/* max. time slots - one slot for MTP header */
	if (num_slots > 63)
		return -ENOSPC;
	return num_slots;
}

static int msm_dp_mst_calc_pbn_mode(struct drm_display_mode *mode, u32 bpp)
{
	int pbn;

	// note : we can even drop dp_mst_calc_pbn_mode and just use this
	// directly
	pbn = drm_dp_calc_pbn_mode(mode->clock, bpp << 4);

	return pbn;
}

static int _msm_dp_mst_compute_config(struct drm_atomic_state *state,
				      struct msm_dp_mst *mst, struct drm_connector *connector,
				      struct drm_display_mode *mode)
{
	int slots = 0, pbn;
	struct msm_dp_mst_connector *mst_conn = to_msm_dp_mst_connector(connector);
	struct msm_dp_link_info *link_info = &mst_conn->dp_panel->link_info;
	int rc = 0;
	struct drm_dp_mst_topology_state *mst_state;

	struct msm_dp *dp_display = mst->msm_dp;
	u32 bpp;

	bpp = connector->display_info.bpc * 3;
	//default to 24
	if (!bpp)
		bpp = 24;

	pbn = msm_dp_mst_calc_pbn_mode(mode, bpp);

	mst_state = to_drm_dp_mst_topology_state(mst->mst_mgr.base.state);

	if (!mst_state->pbn_div.full)
		mst_state->pbn_div = drm_dp_get_vc_payload_bw(&mst->mst_mgr,
							      link_info->rate,
							      link_info->num_lanes);

	rc = drm_dp_atomic_find_time_slots(state, &mst->mst_mgr, mst_conn->mst_port, pbn);
	if (rc < 0) {
		DRM_ERROR("conn:%d failed to find vcpi slots. pbn:%d, rc:%d\n",
			  connector->base.id, pbn, rc);
		goto end;
	}

	slots = rc;

	rc = drm_dp_mst_atomic_check(state);
	if (rc) {
		DRM_ERROR("conn:%d mst atomic check failed: rc=%d\n", connector->base.id, rc);
		slots = 0;
		goto end;
	}

	drm_dbg_dp(dp_display->drm_dev, "conn:%d pbn:%d slots:%d rc:%d\n",
		   connector->base.id, pbn, slots, rc);

end:
	return (rc < 0 ? rc : slots);
}

static void _msm_dp_mst_update_timeslots(struct msm_dp_mst *mst,
					 struct msm_dp_mst_bridge *mst_bridge,
					 struct drm_dp_mst_port *port)
{
	int i;
	struct msm_dp_mst_bridge *msm_dp_bridge;
	struct drm_dp_mst_topology_state *mst_state;
	struct drm_dp_mst_atomic_payload *payload;
	int prev_start = 0;
	int prev_slots = 0;

	mst_state = to_drm_dp_mst_topology_state(mst->mst_mgr.base.state);
	payload = drm_atomic_get_mst_payload_state(mst_state, port);

	if (!payload) {
		DRM_ERROR("mst bridge [%d] update_timeslots failed, null payload\n",
			  mst_bridge->id);
		return;
	}

	for (i = 0; i < mst->max_streams; i++) {
		msm_dp_bridge = &mst->mst_bridge[i];
		if (mst_bridge == msm_dp_bridge) {
			/*
			 * When a payload was removed make sure to move any payloads after it
			 * to the left so all payloads are aligned to the left.
			 */
			if (payload->vc_start_slot < 0) {
				// cache the payload
				prev_start = msm_dp_bridge->start_slot;
				prev_slots = msm_dp_bridge->num_slots;
				msm_dp_bridge->pbn = 0;
				msm_dp_bridge->start_slot = 1;
				msm_dp_bridge->num_slots = 0;
				msm_dp_bridge->vcpi = 0;
			} else { //add payload
				msm_dp_bridge->pbn = payload->pbn;
				msm_dp_bridge->start_slot = payload->vc_start_slot;
				msm_dp_bridge->num_slots = payload->time_slots;
				msm_dp_bridge->vcpi = payload->vcpi;
			}
		}
	}

	// Now commit all the updated payloads
	for (i = 0; i < mst->max_streams; i++) {
		msm_dp_bridge = &mst->mst_bridge[i];

		//Shift payloads to the left if there was a removed payload.
		if (payload->vc_start_slot < 0 && msm_dp_bridge->start_slot > prev_start)
			msm_dp_bridge->start_slot -= prev_slots;

		msm_dp_display_set_stream_info(mst->msm_dp, msm_dp_bridge->msm_dp_panel,
					       msm_dp_bridge->id, msm_dp_bridge->start_slot,
					       msm_dp_bridge->num_slots,
					       msm_dp_bridge->pbn, msm_dp_bridge->vcpi);
		drm_dbg_dp(mst->msm_dp->drm_dev,
			   "conn:%d vcpi:%d start_slot:%d num_slots:%d, pbn:%d\n",
			   DP_MST_CONN_ID(msm_dp_bridge), msm_dp_bridge->vcpi,
			   msm_dp_bridge->start_slot,
			   msm_dp_bridge->num_slots, msm_dp_bridge->pbn);
	}
}

static int _msm_dp_mst_bridge_pre_enable_part1(struct msm_dp_mst_bridge *dp_bridge)
{
	struct msm_dp *msm_dp = dp_bridge->display;
	struct msm_dp_mst *mst = msm_dp->msm_dp_mst;
	struct msm_dp_mst_connector *mst_conn = to_msm_dp_mst_connector(dp_bridge->connector);
	struct drm_dp_mst_port *port = mst_conn->mst_port;
	struct drm_dp_mst_topology_state *mst_state;
	struct drm_dp_mst_atomic_payload *payload;
	struct msm_dp_panel *dp_panel = mst_conn->dp_panel;
	int pbn, slots;
	int rc = 0;

	pbn = msm_dp_mst_calc_pbn_mode(&dp_panel->msm_dp_mode.drm_mode,
				       msm_dp->connector->display_info.bpc * 3);

	slots = msm_dp_mst_find_vcpi_slots(&mst->mst_mgr, pbn);

	drm_dbg_dp(msm_dp->drm_dev, "conn:%d pbn:%d, slots:%d\n", DP_MST_CONN_ID(dp_bridge),
		   pbn, slots);

	mst_state = to_drm_dp_mst_topology_state(mst->mst_mgr.base.state);
	payload = drm_atomic_get_mst_payload_state(mst_state, port);
	if (!payload || payload->time_slots <= 0) {
		DRM_ERROR("time slots not allocated for conn:%d\n", DP_MST_CONN_ID(dp_bridge));
		rc = -EINVAL;
		return rc;
	}

	drm_dp_mst_update_slots(mst_state, DP_CAP_ANSI_8B10B);

	rc = drm_dp_add_payload_part1(&mst->mst_mgr, mst_state, payload);
	if (rc) {
		DRM_ERROR("payload allocation failure for conn:%d\n", DP_MST_CONN_ID(dp_bridge));
		return rc;
	}

	_msm_dp_mst_update_timeslots(mst, dp_bridge, port);

	return rc;
}

static void _msm_dp_mst_bridge_pre_enable_part2(struct msm_dp_mst_bridge *dp_bridge)
{
	struct msm_dp *msm_dp = dp_bridge->display;
	struct msm_dp_mst *mst = msm_dp->msm_dp_mst;
	struct msm_dp_mst_connector *mst_conn = to_msm_dp_mst_connector(dp_bridge->connector);
	struct drm_dp_mst_port *port = mst_conn->mst_port;
	struct drm_dp_mst_topology_state *mst_state;
	struct drm_dp_mst_atomic_payload *payload;

	drm_dp_check_act_status(&mst->mst_mgr);

	mst_state = to_drm_dp_mst_topology_state(mst->mst_mgr.base.state);
	payload = drm_atomic_get_mst_payload_state(mst_state, port);

	if (!payload) {
		DRM_ERROR("mst bridge [%d] null payload\n", dp_bridge->id);
		return;
	}

	if (!payload->port) {
		DRM_ERROR("mst bridge [%d] null port\n", dp_bridge->id);
		return;
	}

	if (!payload->port->connector) {
		DRM_ERROR("mst bridge [%d] part-2 failed, null connector\n",
			  dp_bridge->id);
		return;
	}

	if (payload->vc_start_slot == -1) {
		DRM_ERROR("mst bridge [%d] part-2 failed, payload alloc part 1 failed\n",
			  dp_bridge->id);
		return;
	}

	drm_dp_add_payload_part2(&mst->mst_mgr, payload);
	drm_dbg_dp(msm_dp->drm_dev, "mst bridge [%d] _pre enable part-2 complete\n",
		   dp_bridge->id);
}

static void _msm_dp_mst_bridge_pre_disable_part1(struct msm_dp_mst_bridge *dp_bridge)
{
	struct msm_dp *msm_dp = dp_bridge->display;
	struct msm_dp_mst *mst = msm_dp->msm_dp_mst;
	struct msm_dp_mst_connector *mst_conn = to_msm_dp_mst_connector(dp_bridge->connector);
	struct drm_dp_mst_port *port = mst_conn->mst_port;
	struct drm_dp_mst_topology_state *mst_state;
	struct drm_dp_mst_atomic_payload *payload;

	mst_state = to_drm_dp_mst_topology_state(mst->mst_mgr.base.state);
	payload = drm_atomic_get_mst_payload_state(mst_state, port);

	if (!payload) {
		DRM_ERROR("mst bridge [%d] _pre disable part-1 failed, null payload\n",
			  dp_bridge->id);
		return;
	}

	drm_dp_remove_payload_part1(&mst->mst_mgr, mst_state, payload);
	_msm_dp_mst_update_timeslots(mst, dp_bridge, port);

	drm_dbg_dp(msm_dp->drm_dev, "mst bridge [%d] _pre disable part-1 complete\n",
		   dp_bridge->id);
}

static void _msm_dp_mst_bridge_pre_disable_part2(struct msm_dp_mst_bridge *dp_bridge)
{
	struct msm_dp *msm_dp = dp_bridge->display;
	struct msm_dp_mst *mst = msm_dp->msm_dp_mst;

	drm_dp_check_act_status(&mst->mst_mgr);

	drm_dbg_dp(msm_dp->drm_dev, "mst bridge [%d] _pre disable part-2 complete\n",
		   dp_bridge->id);
}

static void msm_dp_mst_bridge_atomic_pre_enable(struct drm_bridge *drm_bridge,
						struct drm_bridge_state *old_bridge_state)
{
	int rc = 0;
	struct msm_dp_mst_bridge *bridge;
	struct msm_dp *dp;
	struct msm_dp_mst_bridge_state *msm_dp_bridge_state;
	struct msm_dp_mst *dp_mst;

	if (!drm_bridge) {
		DRM_ERROR("Invalid params\n");
		return;
	}

	bridge = to_msm_dp_mst_bridge(drm_bridge);
	msm_dp_bridge_state = to_msm_dp_mst_bridge_state(bridge);
	dp = bridge->display;
	dp_mst = dp->msm_dp_mst;

	/* to cover cases of bridge_disable/bridge_enable without modeset */
	bridge->connector = msm_dp_bridge_state->connector;
	bridge->msm_dp_panel = msm_dp_bridge_state->msm_dp_panel;

	if (!bridge->connector) {
		DRM_ERROR("Invalid connector\n");
		return;
	}

	mutex_lock(&dp_mst->mst_lock);
	msm_dp_display_atomic_prepare(dp);

	rc = _msm_dp_mst_bridge_pre_enable_part1(bridge);
	if (rc) {
		DRM_ERROR("[%d] DP display pre-enable failed, rc=%d\n", bridge->id, rc);
		msm_dp_display_unprepare(dp);
		mutex_unlock(&dp_mst->mst_lock);
		return;
	}

	msm_dp_display_enable_helper(dp, bridge->msm_dp_panel);

	_msm_dp_mst_bridge_pre_enable_part2(bridge);

	drm_dbg_dp(dp->drm_dev, "conn:%d mode:%s fps:%d vcpi:%d slots:%d to %d\n",
		   DP_MST_CONN_ID(bridge), bridge->drm_mode.name,
		   drm_mode_vrefresh(&bridge->drm_mode),
		   bridge->vcpi, bridge->start_slot,
		   bridge->start_slot + bridge->num_slots);

	mutex_unlock(&dp_mst->mst_lock);
}

static void msm_dp_mst_bridge_atomic_disable(struct drm_bridge *drm_bridge,
					     struct drm_bridge_state *old_bridge_state)
{
	struct msm_dp_mst_bridge *bridge;
	struct msm_dp *dp;
	struct msm_dp_mst *mst;

	if (!drm_bridge) {
		DRM_ERROR("Invalid params\n");
		return;
	}

	bridge = to_msm_dp_mst_bridge(drm_bridge);
	if (!bridge->connector) {
		DRM_ERROR("Invalid connector\n");
		return;
	}

	dp = bridge->display;
	mst = dp->msm_dp_mst;

	mutex_lock(&mst->mst_lock);

	_msm_dp_mst_bridge_pre_disable_part1(bridge);

	msm_dp_display_disable_helper(dp, bridge->msm_dp_panel);

	_msm_dp_mst_bridge_pre_disable_part2(bridge);

	drm_dbg_dp(dp->drm_dev, "mst bridge:%d conn:%d disable complete\n", bridge->id,
		   DP_MST_CONN_ID(bridge));

	mutex_unlock(&mst->mst_lock);
}

static void msm_dp_mst_bridge_atomic_post_disable(struct drm_bridge *drm_bridge,
						  struct drm_bridge_state *old_bridge_state)
{
	int conn = 0;
	struct msm_dp_mst_bridge *bridge;
	struct msm_dp *dp;
	struct msm_dp_mst *mst;

	if (!drm_bridge) {
		DRM_ERROR("Invalid params\n");
		return;
	}

	bridge = to_msm_dp_mst_bridge(drm_bridge);
	if (!bridge->connector) {
		DRM_ERROR("Invalid connector\n");
		return;
	}

	conn = DP_MST_CONN_ID(bridge);

	dp = bridge->display;
	mst = dp->msm_dp_mst;

	mutex_lock(&mst->mst_lock);

	msm_dp_display_atomic_post_disable_helper(dp, bridge->msm_dp_panel);

	if (!dp->mst_active)
		msm_dp_display_unprepare(dp);

	bridge->connector = NULL;
	bridge->msm_dp_panel =  NULL;

	drm_dbg_dp(dp->drm_dev, "mst bridge:%d conn:%d post disable complete\n",
		   bridge->id, conn);

	mutex_unlock(&mst->mst_lock);
}

static void msm_dp_mst_bridge_mode_set(struct drm_bridge *drm_bridge,
				       const struct drm_display_mode *mode,
				       const struct drm_display_mode *adjusted_mode)
{
	struct msm_dp_mst_bridge *bridge;
	struct msm_dp_mst_bridge_state *dp_bridge_state;
	struct msm_dp *dp;
	struct msm_dp_panel *msm_dp_panel;

	if (!drm_bridge || !mode || !adjusted_mode) {
		DRM_ERROR("Invalid params\n");
		return;
	}

	bridge = to_msm_dp_mst_bridge(drm_bridge);

	dp_bridge_state = to_msm_dp_mst_bridge_state(bridge);
	bridge->connector = dp_bridge_state->connector;
	bridge->msm_dp_panel = dp_bridge_state->msm_dp_panel;

	msm_dp_panel = bridge->msm_dp_panel;

	dp = bridge->display;

	memset(&bridge->msm_dp_mode, 0x0, sizeof(struct msm_dp_display_mode));
	memcpy(&bridge->drm_mode, adjusted_mode, sizeof(bridge->drm_mode));

	msm_dp_display_mode_set_helper(dp, mode, adjusted_mode, bridge->msm_dp_panel);
	msm_dp_panel->mst_caps.pbn = msm_dp_mst_calc_pbn_mode(&msm_dp_panel->msm_dp_mode.drm_mode,
							      msm_dp_panel->msm_dp_mode.bpp);
	memcpy(&bridge->msm_dp_mode, &bridge->msm_dp_panel->msm_dp_mode,
	       sizeof(struct msm_dp_display_mode));

	drm_dbg_dp(dp->drm_dev, "mst bridge:%d conn:%d mode set complete %s\n", bridge->id,
		   DP_MST_CONN_ID(bridge), mode->name);
}

/* DP MST Bridge APIs */
static const struct drm_bridge_funcs msm_dp_mst_bridge_ops = {
	.atomic_duplicate_state = drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state   = drm_atomic_helper_bridge_destroy_state,
	.atomic_reset           = drm_atomic_helper_bridge_reset,
	.atomic_pre_enable   = msm_dp_mst_bridge_atomic_pre_enable,
	.atomic_disable      = msm_dp_mst_bridge_atomic_disable,
	.atomic_post_disable = msm_dp_mst_bridge_atomic_post_disable,
	.mode_set     = msm_dp_mst_bridge_mode_set,
};

int msm_dp_mst_drm_bridge_init(struct msm_dp *dp, struct drm_encoder *encoder)
{
	int rc = 0;
	struct msm_dp_mst_bridge *bridge = NULL;
	struct msm_dp_mst_bridge_state *state;
	struct drm_device *dev;
	struct msm_dp_mst *mst = dp->msm_dp_mst;
	int i;

	for (i = 0; i < mst->max_streams; i++) {
		if (!mst->mst_bridge[i].in_use) {
			bridge = &mst->mst_bridge[i];
			bridge->encoder = encoder;
			bridge->in_use = true;
			bridge->id = i;
			break;
		}
	}

	if (i == mst->max_streams) {
		DRM_ERROR("mst supports only %d bridges\n", i);
		rc = -EACCES;
		goto end;
	}

	dev = dp->drm_dev;
	bridge->display = dp;
	bridge->base.funcs = &msm_dp_mst_bridge_ops;
	bridge->base.encoder = encoder;
	bridge->base.type = dp->connector_type;
	bridge->base.ops = DRM_BRIDGE_OP_MODES;
	drm_bridge_add(&bridge->base);

	rc = drm_bridge_attach(encoder, &bridge->base, NULL, 0);
	if (rc) {
		DRM_ERROR("failed to attach bridge, rc=%d\n", rc);
		goto end;
	}

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state) {
		rc = -ENOMEM;
		goto end;
	}

	drm_atomic_private_obj_init(dev, &bridge->obj,
				    &state->base,
				    &msm_dp_mst_bridge_state_funcs);

	drm_dbg_dp(dp->drm_dev, "mst drm bridge init. bridge id:%d\n", i);

	return 0;

end:
	return rc;
}

static struct msm_dp_mst_bridge_state *msm_dp_mst_br_priv_state(struct drm_atomic_state *st,
								struct msm_dp_mst_bridge *bridge)
{
	struct drm_device *dev = bridge->base.dev;

	WARN_ON(!drm_modeset_is_locked(&dev->mode_config.connection_mutex));

	return to_msm_dp_mst_bridge_priv_state(drm_atomic_get_private_obj_state(st,
										&bridge->obj));
}

static void msm_dp_mst_clear_panel_edid(struct msm_dp *dp_display)
{
	struct msm_dp_mst *mst = dp_display->msm_dp_mst;
	struct msm_dp_mst_connector *mst_conn;
	struct msm_dp_panel *dp_panel;
	struct msm_dp_mst_bridge *dp_bridge;
	int i;

	if (!dp_display) {
		DRM_ERROR("invalid input\n");
		return;
	}

	for (i = 0; i < mst->max_streams; i++) {
		dp_bridge = &mst->mst_bridge[i];
		mst_conn = to_msm_dp_mst_connector(dp_bridge->connector);
		dp_panel = dp_bridge->msm_dp_panel;

		if (!dp_panel || !mst_conn || !mst_conn->mst_port)
			continue;

		if (dp_panel->drm_edid) {
			drm_edid_free(dp_panel->drm_edid);
			dp_panel->drm_edid = NULL;
		}
	}
}

/* DP MST HPD IRQ callback */
void msm_dp_mst_display_hpd_irq(struct msm_dp *dp_display)
{
	int rc;
	struct msm_dp_mst *mst = dp_display->msm_dp_mst;
	u8 ack[8] = {};
	u8 esi[14];
	unsigned int esi_res = DP_SINK_COUNT_ESI + 1;
	bool handled;

	rc = drm_dp_dpcd_read(mst->dp_aux, DP_SINK_COUNT_ESI, esi, 14);
	if (rc != 14) {
		DRM_ERROR("dpcd sink status read failed, rlen=%d\n", rc);
		return;
	}

	drm_dbg_dp(dp_display->drm_dev, "mst irq: esi1[0x%x] esi2[0x%x] esi3[0x%x]\n",
		   esi[1], esi[2], esi[3]);

	rc = drm_dp_mst_hpd_irq_handle_event(&mst->mst_mgr, esi, ack, &handled);

	/* ack the request */
	if (handled) {
		rc = drm_dp_dpcd_writeb(mst->dp_aux, esi_res, ack[1]);

		if (ack[1] & DP_UP_REQ_MSG_RDY)
			msm_dp_mst_clear_panel_edid(dp_display);

		if (rc != 1)
			DRM_ERROR("dpcd esi_res failed. rc=%d\n", rc);

		drm_dp_mst_hpd_irq_send_new_request(&mst->mst_mgr);
	}
	drm_dbg_dp(dp_display->drm_dev, "mst display hpd_irq handled:%d rc:%d\n", handled, rc);
}

/* DP MST Connector OPs */
static int
msm_dp_mst_connector_detect(struct drm_connector *connector,
			    struct drm_modeset_acquire_ctx *ctx,
			    bool force)
{
	struct msm_dp_mst_connector *mst_conn = to_msm_dp_mst_connector(connector);
	struct msm_dp *dp_display = mst_conn->msm_dp;
	struct msm_dp_mst *mst = dp_display->msm_dp_mst;
	enum drm_connector_status status = connector_status_disconnected;

	if (dp_display->link_ready && dp_display->mst_active)
		status = drm_dp_mst_detect_port(connector,
						ctx, &mst->mst_mgr, mst_conn->mst_port);

	drm_dbg_dp(dp_display->drm_dev, "conn:%d status:%d\n", connector->base.id, status);

	return (int)status;
}

static int msm_dp_mst_connector_get_modes(struct drm_connector *connector)
{
	struct msm_dp_mst_connector *mst_conn = to_msm_dp_mst_connector(connector);
	struct msm_dp *dp_display = mst_conn->msm_dp;
	struct msm_dp_mst *mst = dp_display->msm_dp_mst;
	struct msm_dp_panel *dp_panel = mst_conn->dp_panel;

	if (dp_panel->drm_edid)
		goto duplicate_edid;

	drm_edid_free(dp_panel->drm_edid);

	dp_panel->drm_edid = drm_dp_mst_edid_read(connector, &mst->mst_mgr, mst_conn->mst_port);
	if (!dp_panel->drm_edid) {
		DRM_ERROR("get edid failed. id: %d\n", connector->base.id);
		return -EINVAL;
	}

duplicate_edid:
	drm_edid_connector_update(connector, dp_panel->drm_edid);

	return drm_edid_connector_add_modes(connector);
}

static enum drm_mode_status msm_dp_mst_connector_mode_valid(struct drm_connector *connector,
							    struct drm_display_mode *mode)
{
	struct msm_dp_mst_connector *mst_conn = to_msm_dp_mst_connector(connector);
	struct msm_dp *dp_display = mst_conn->msm_dp;
	struct drm_dp_mst_port *mst_port;
	struct msm_dp_panel *dp_panel;
	struct msm_dp_mst *mst;
	u16 full_pbn, required_pbn;
	int available_slots, required_slots;
	struct msm_dp_mst_bridge_state *dp_bridge_state;
	int i, slots_in_use = 0, active_enc_cnt = 0;
	const u32 tot_slots = 63;

	if (!connector || !mode || !dp_display) {
		DRM_ERROR("invalid input\n");
		return 0;
	}

	mst = dp_display->msm_dp_mst;
	mst_conn = to_msm_dp_mst_connector(connector);
	mst_port = mst_conn->mst_port;
	dp_panel = mst_conn->dp_panel;

	if (!dp_panel || !mst_port)
		return MODE_ERROR;

	/* dp bridge state is protected by drm_mode_config.connection_mutex */
	for (i = 0; i < mst->max_streams; i++) {
		dp_bridge_state = to_msm_dp_mst_bridge_state(&mst->mst_bridge[i]);
		if (dp_bridge_state->connector &&
		    dp_bridge_state->connector != connector) {
			active_enc_cnt++;
			slots_in_use += dp_bridge_state->num_slots;
		}
	}

	if (active_enc_cnt < DP_STREAM_MAX) {
		full_pbn = mst_port->full_pbn;
		available_slots = tot_slots - slots_in_use;
	} else {
		DRM_ERROR("all mst streams are active\n");
		return MODE_BAD;
	}

	required_pbn = msm_dp_mst_calc_pbn_mode(mode, connector->display_info.bpc * 3);

	required_slots = msm_dp_mst_find_vcpi_slots(&mst->mst_mgr, required_pbn);

	if (required_pbn > full_pbn || required_slots > available_slots) {
		drm_dbg_dp(dp_display->drm_dev,
			   "mode:%s not supported. pbn %d vs %d slots %d vs %d\n",
			   mode->name, required_pbn, full_pbn,
			   required_slots, available_slots);
		return MODE_BAD;
	}

	return msm_dp_display_mode_valid(dp_display, &dp_display->connector->display_info, mode);
}

static struct drm_encoder *
msm_dp_mst_atomic_best_encoder(struct drm_connector *connector, struct drm_atomic_state *state)
{
	struct msm_dp_mst_connector *mst_conn = to_msm_dp_mst_connector(connector);
	struct msm_dp *dp_display = mst_conn->msm_dp;
	struct msm_dp_mst *mst = dp_display->msm_dp_mst;
	struct drm_encoder *enc = NULL;
	struct msm_dp_mst_bridge_state *bridge_state;
	u32 i;
	struct drm_connector_state *conn_state = drm_atomic_get_new_connector_state(state,
										    connector);

	if (conn_state && conn_state->best_encoder)
		return conn_state->best_encoder;

	for (i = 0; i < mst->max_streams; i++) {
		bridge_state = msm_dp_mst_br_priv_state(state, &mst->mst_bridge[i]);
		if (IS_ERR(bridge_state))
			goto end;

		if (bridge_state->connector == connector) {
			enc = mst->mst_bridge[i].encoder;
			goto end;
		}
	}

	for (i = 0; i < mst->max_streams; i++) {
		bridge_state = msm_dp_mst_br_priv_state(state, &mst->mst_bridge[i]);

		if (!bridge_state->connector) {
			bridge_state->connector = connector;
			bridge_state->msm_dp_panel = mst_conn->dp_panel;
			enc = mst->mst_bridge[i].encoder;
			break;
		}
	}

end:
	if (enc)
		drm_dbg_dp(dp_display->drm_dev, "mst connector:%d atomic best encoder:%d\n",
			   connector->base.id, i);
	else
		drm_dbg_dp(dp_display->drm_dev, "mst connector:%d atomic best encoder failed\n",
			   connector->base.id);

	return enc;
}

static int msm_dp_mst_connector_atomic_check(struct drm_connector *connector,
					     struct drm_atomic_state *state)
{
	int rc = 0, slots, i;
	bool vcpi_released = false;
	struct drm_connector_state *old_conn_state;
	struct drm_connector_state *new_conn_state;
	struct drm_crtc *old_crtc;
	struct drm_crtc_state *crtc_state;
	struct msm_dp_mst_bridge *bridge;
	struct msm_dp_mst_bridge_state *bridge_state;
	struct drm_bridge *drm_bridge;
	struct msm_dp_mst_connector *mst_conn = to_msm_dp_mst_connector(connector);
	struct msm_dp *dp_display = mst_conn->msm_dp;
	struct msm_dp_mst *mst = dp_display->msm_dp_mst;

	if (!state)
		return rc;

	new_conn_state = drm_atomic_get_new_connector_state(state, connector);
	if (!new_conn_state)
		return rc;

	old_conn_state = drm_atomic_get_old_connector_state(state, connector);
	if (!old_conn_state)
		goto mode_set;

	old_crtc = old_conn_state->crtc;
	if (!old_crtc)
		goto mode_set;

	crtc_state = drm_atomic_get_new_crtc_state(state, old_crtc);

	for (i = 0; i < mst->max_streams; i++) {
		bridge = &mst->mst_bridge[i];
		drm_dbg_dp(dp_display->drm_dev, "bridge id:%d, vcpi:%d, pbn:%d, slots:%d\n",
			   bridge->id, bridge->vcpi, bridge->pbn,
			   bridge->num_slots);
	}

	/*attempt to release vcpi slots on a modeset change for crtc state*/
	if (drm_atomic_crtc_needs_modeset(crtc_state)) {
		if (WARN_ON(!old_conn_state->best_encoder)) {
			rc = -EINVAL;
			goto end;
		}

		drm_bridge = drm_bridge_chain_get_first_bridge(old_conn_state->best_encoder);
		if (WARN_ON(!drm_bridge)) {
			rc = -EINVAL;
			goto end;
		}
		bridge = to_msm_dp_mst_bridge(drm_bridge);

		bridge_state = msm_dp_mst_br_priv_state(state, bridge);
		if (IS_ERR(bridge_state)) {
			rc = PTR_ERR(bridge_state);
			goto end;
		}

		if (WARN_ON(bridge_state->connector != connector)) {
			rc = -EINVAL;
			goto end;
		}

		slots = bridge_state->num_slots;
		if (slots > 0) {
			rc = drm_dp_atomic_release_time_slots(state,
							      &mst->mst_mgr,
							      mst_conn->mst_port);
			if (rc) {
				DRM_ERROR("failed releasing %d vcpi slots %d\n", slots, rc);
				goto end;
			}
			vcpi_released = true;
		}

		if (!new_conn_state->crtc) {
			/* for cases where crtc is not disabled the slots are not
			 * freed by drm_dp_atomic_release_time_slots. this results
			 * in subsequent atomic_check failing since internal slots
			 * were freed but not the dp mst mgr's
			 */
			bridge_state->num_slots = 0;
			bridge_state->connector = NULL;
			bridge_state->msm_dp_panel = NULL;

			drm_dbg_dp(dp_display->drm_dev, "clear best encoder: %d\n", bridge->id);
		}
	}

mode_set:
	if (!new_conn_state->crtc)
		goto end;

	crtc_state = drm_atomic_get_new_crtc_state(state, new_conn_state->crtc);

	if (drm_atomic_crtc_needs_modeset(crtc_state) && crtc_state->active) {
		if (WARN_ON(!new_conn_state->best_encoder)) {
			rc = -EINVAL;
			goto end;
		}

		drm_bridge = drm_bridge_chain_get_first_bridge(new_conn_state->best_encoder);
		if (WARN_ON(!drm_bridge)) {
			rc = -EINVAL;
			goto end;
		}
		bridge = to_msm_dp_mst_bridge(drm_bridge);

		bridge_state = msm_dp_mst_br_priv_state(state, bridge);
		if (IS_ERR(bridge_state)) {
			rc = PTR_ERR(bridge_state);
			goto end;
		}

		if (WARN_ON(bridge_state->connector != connector)) {
			rc = -EINVAL;
			goto end;
		}

		/*
		 * check if vcpi slots are trying to get allocated in same phase
		 * as deallocation. If so, go to end to avoid allocation.
		 */
		if (vcpi_released) {
			drm_dbg_dp(dp_display->drm_dev,
				   "skipping allocation since vcpi was released in the same state\n");
			goto end;
		}

		if (WARN_ON(bridge_state->num_slots)) {
			rc = -EINVAL;
			goto end;
		}

		slots = _msm_dp_mst_compute_config(state, mst, connector, &crtc_state->mode);
		if (slots < 0) {
			rc = slots;
			goto end;
		}

		bridge_state->num_slots = slots;
	}

end:
	drm_dbg_dp(dp_display->drm_dev, "mst connector:%d atomic check ret %d\n",
		   connector->base.id, rc);
	return rc;
}

static void dp_mst_connector_destroy(struct drm_connector *connector)
{
	struct msm_dp_mst_connector *mst_conn = to_msm_dp_mst_connector(connector);
	struct msm_dp_panel *dp_panel = mst_conn->dp_panel;

	drm_connector_cleanup(connector);
	drm_edid_free(dp_panel->drm_edid);
	dp_panel->drm_edid = NULL;
	drm_dp_mst_put_port_malloc(mst_conn->mst_port);
	msm_dp_panel_put(mst_conn->dp_panel);
}

/* DRM MST callbacks */
static const struct drm_connector_helper_funcs dp_drm_mst_connector_helper_funcs = {
	.get_modes =    msm_dp_mst_connector_get_modes,
	.detect_ctx =   msm_dp_mst_connector_detect,
	.mode_valid =   msm_dp_mst_connector_mode_valid,
	.atomic_best_encoder = msm_dp_mst_atomic_best_encoder,
	.atomic_check = msm_dp_mst_connector_atomic_check,
};

static const struct drm_connector_funcs dp_drm_mst_connector_funcs = {
	.reset = drm_atomic_helper_connector_reset,
	.destroy = dp_mst_connector_destroy,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static struct drm_connector *
msm_dp_mst_add_connector(struct drm_dp_mst_topology_mgr *mgr,
			 struct drm_dp_mst_port *port, const char *pathprop)
{
	struct msm_dp_mst *dp_mst;
	struct drm_device *dev;
	struct msm_dp *dp_display;
	struct msm_dp_mst_connector *mst_connector;
	struct drm_connector *connector;
	int rc, i;

	dp_mst = container_of(mgr, struct msm_dp_mst, mst_mgr);

	dp_display = dp_mst->msm_dp;
	dev = dp_display->drm_dev;

	mst_connector = devm_kzalloc(dev->dev, sizeof(*mst_connector), GFP_KERNEL);

	/* make sure connector is not accessed before reset */
	drm_modeset_lock_all(dev);

	rc = drm_connector_init(dev, &mst_connector->connector, &dp_drm_mst_connector_funcs,
				DRM_MODE_CONNECTOR_DisplayPort);
	if (rc) {
		drm_modeset_unlock_all(dev);
		return NULL;
	}

	mst_connector->dp_panel = msm_dp_display_get_panel(dp_display);
	if (!mst_connector->dp_panel) {
		DRM_ERROR("failed to get dp_panel for connector\n");
		drm_modeset_unlock_all(dev);
		return NULL;
	}

	mst_connector->dp_panel->connector = &mst_connector->connector;
	mst_connector->msm_dp = dp_display;
	connector = &mst_connector->connector;
	drm_connector_helper_add(&mst_connector->connector, &dp_drm_mst_connector_helper_funcs);

	if (connector->funcs->reset)
		connector->funcs->reset(connector);

	/* add all encoders as possible encoders */
	for (i = 0; i < dp_mst->max_streams; i++) {
		rc = drm_connector_attach_encoder(&mst_connector->connector,
						  dp_mst->mst_bridge[i].encoder);
		if (rc) {
			DRM_ERROR("failed to attach encoder to connector, %d\n", rc);
			drm_modeset_unlock_all(dev);
			return NULL;
		}
	}

	mst_connector->mst_port = port;
	drm_dp_mst_get_port_malloc(mst_connector->mst_port);

	/* check if we need to call connector_reset here */

	drm_object_attach_property(&mst_connector->connector.base,
				   dev->mode_config.path_property, 0);
	drm_object_attach_property(&mst_connector->connector.base,
				   dev->mode_config.tile_property, 0);

	/* unlock connector and make it accessible */
	drm_modeset_unlock_all(dev);

	drm_dbg_dp(dp_display->drm_dev, "add mst connector id:%d\n",
		   mst_connector->connector.base.id);

	return &mst_connector->connector;
}

int msm_dp_mst_display_set_mgr_state(struct msm_dp *dp_display, bool state)
{
	int rc;
	struct msm_dp_mst *mst = dp_display->msm_dp_mst;

	/*
	 * on hpd high, set_mgr_state is called before hotplug event is sent
	 * to usermode and mst_session_state should be updated here.
	 * on hpd_low, set_mgr_state is called after hotplug event is sent and
	 * the session_state was already updated prior to that.
	 */
	if (state)
		mst->mst_session_hpd_state = state;

	rc = drm_dp_mst_topology_mgr_set_mst(&mst->mst_mgr, state);
	if (rc < 0) {
		DRM_ERROR("failed to set topology mgr state to %d. rc %d\n",
			  state, rc);
	}

	drm_dbg_dp(dp_display->drm_dev, "dp_mst_display_set_mgr_state state:%d\n", state);
	return rc;
}

static const struct drm_dp_mst_topology_cbs msm_dp_mst_drm_cbs = {
	.add_connector = msm_dp_mst_add_connector,
};

int msm_dp_mst_init(struct msm_dp *dp_display, u32 max_streams, u32 max_dpcd_transaction_bytes,
		    struct drm_dp_aux *drm_aux)
{
	struct drm_device *dev;
	int conn_base_id = 0;
	int ret;
	struct msm_dp_mst *msm_dp_mst;

	if (!dp_display) {
		DRM_ERROR("invalid params\n");
		return 0;
	}

	dev = dp_display->drm_dev;

	msm_dp_mst = devm_kzalloc(dev->dev, sizeof(*msm_dp_mst), GFP_KERNEL);
	if (!msm_dp_mst)
		return -ENOMEM;

	// will expost the callbacks as direct APIs for hotplug
	// lets drop install_info for now
	memset(&msm_dp_mst->mst_mgr, 0, sizeof(msm_dp_mst->mst_mgr));
	msm_dp_mst->mst_mgr.cbs = &msm_dp_mst_drm_cbs;
	conn_base_id = dp_display->connector->base.id;
	msm_dp_mst->msm_dp = dp_display;
	msm_dp_mst->dp_aux = drm_aux;
	msm_dp_mst->max_streams = max_streams;

	ret = drm_dp_mst_topology_mgr_init(&msm_dp_mst->mst_mgr, dev,
					   drm_aux,
					   max_dpcd_transaction_bytes,
					   max_streams,
					   conn_base_id);
	if (ret) {
		DRM_ERROR("dp drm mst topology manager init failed\n");
		return ret;
	}

	dp_display->msm_dp_mst = msm_dp_mst;

	mutex_init(&msm_dp_mst->mst_lock);

	msm_dp_mst->mst_initialized = true;

	drm_dbg_dp(dp_display->drm_dev, "dp drm mst topology manager init completed\n");

	return ret;
}
