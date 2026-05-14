// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2012-2020, The Linux Foundation. All rights reserved.
 */

#include "dp_panel.h"
#include "dp_reg.h"
#include "dp_utils.h"
#include "msm_dsc_helper.h"

#include <drm/display/drm_dp_helper.h>
#include <drm/display/drm_dsc_helper.h>
#include <drm/drm_connector.h>
#include <drm/drm_edid.h>
#include <drm/drm_of.h>
#include <drm/drm_print.h>

#include <linux/align.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <asm/byteorder.h>

#define DP_INTF_CONFIG_DATABUS_WIDEN     BIT(4)
#define MSM_DP_DSC_TARGET_BPP		10
/* sc8280xp DP DSC is currently validated up to 10bpc. */
#define MSM_DP_DSC_MAX_BPC		10
#define MSM_DP_DSC_PPS_SIZE		88
#define MSM_DP_DSC_BE_IN_LANE		10

struct msm_dp_panel_private {
	struct device *dev;
	struct drm_device *drm_dev;
	struct msm_dp_panel msm_dp_panel;
	struct drm_dp_aux *aux;
	struct msm_dp_link *link;
	void __iomem *link_base;
	void __iomem *p0_base;
	bool panel_on;
};

static inline u32 msm_dp_read_link(struct msm_dp_panel_private *panel, u32 offset)
{
	return readl_relaxed(panel->link_base + offset);
}

static inline void msm_dp_write_link(struct msm_dp_panel_private *panel,
			       u32 offset, u32 data)
{
	/*
	 * To make sure link reg writes happens before any other operation,
	 * this function uses writel() instread of writel_relaxed()
	 */
	writel(data, panel->link_base + offset);
}

static inline void msm_dp_write_p0(struct msm_dp_panel_private *panel,
			       u32 offset, u32 data)
{
	/*
	 * To make sure interface reg writes happens before any other operation,
	 * this function uses writel() instread of writel_relaxed()
	 */
	writel(data, panel->p0_base + offset);
}

static inline u32 msm_dp_read_p0(struct msm_dp_panel_private *panel,
			       u32 offset)
{
	/*
	 * To make sure interface reg writes happens before any other operation,
	 * this function uses writel() instread of writel_relaxed()
	 */
	return readl_relaxed(panel->p0_base + offset);
}

static void msm_dp_panel_read_psr_cap(struct msm_dp_panel_private *panel)
{
	ssize_t rlen;
	struct msm_dp_panel *msm_dp_panel;

	msm_dp_panel = &panel->msm_dp_panel;

	/* edp sink */
	if (msm_dp_panel->dpcd[DP_EDP_CONFIGURATION_CAP]) {
		rlen = drm_dp_dpcd_read(panel->aux, DP_PSR_SUPPORT,
				&msm_dp_panel->psr_cap, sizeof(msm_dp_panel->psr_cap));
		if (rlen == sizeof(msm_dp_panel->psr_cap)) {
			drm_dbg_dp(panel->drm_dev,
				"psr version: 0x%x, psr_cap: 0x%x\n",
				msm_dp_panel->psr_cap.version,
				msm_dp_panel->psr_cap.capabilities);
		} else
			DRM_ERROR("failed to read psr info, rlen=%zd\n", rlen);
	}
}

static void msm_dp_panel_reset_dsc_caps(struct msm_dp_panel *msm_dp_panel)
{
	msm_dp_panel->dsc_capable = false;
	msm_dp_panel->fec_capable = false;
	msm_dp_panel->fec_cap = 0;
	memset(msm_dp_panel->dsc_dpcd, 0, sizeof(msm_dp_panel->dsc_dpcd));
}

static void msm_dp_panel_reset_dsc_state(struct msm_dp_panel *msm_dp_panel)
{
	msm_dp_panel->dsc_en = false;
	msm_dp_panel->fec_en = false;
	memset(&msm_dp_panel->dsc, 0, sizeof(msm_dp_panel->dsc));
}

static void msm_dp_panel_reset_dsc(struct msm_dp_panel *msm_dp_panel)
{
	msm_dp_panel_reset_dsc_caps(msm_dp_panel);
	msm_dp_panel_reset_dsc_state(msm_dp_panel);
}

static void msm_dp_panel_read_dsc_caps(struct msm_dp_panel_private *panel)
{
	struct msm_dp_panel *msm_dp_panel = &panel->msm_dp_panel;
	ssize_t rlen;

	msm_dp_panel_reset_dsc_caps(msm_dp_panel);

	if (msm_dp_panel->dpcd[DP_DPCD_REV] < DP_DPCD_REV_14 ||
	    msm_dp_panel->dpcd[DP_EDP_CONFIGURATION_CAP])
		return;

	rlen = drm_dp_dpcd_readb(panel->aux, DP_FEC_CAPABILITY,
				 &msm_dp_panel->fec_cap);
	if (rlen != 1) {
		drm_dbg_dp(panel->drm_dev, "failed to read FEC capability: %zd\n", rlen);
		return;
	}

	msm_dp_panel->fec_capable = drm_dp_sink_supports_fec(msm_dp_panel->fec_cap);
	if (!msm_dp_panel->fec_capable)
		return;

	rlen = drm_dp_dpcd_read(panel->aux, DP_DSC_SUPPORT,
				msm_dp_panel->dsc_dpcd,
				sizeof(msm_dp_panel->dsc_dpcd));
	if (rlen != sizeof(msm_dp_panel->dsc_dpcd)) {
		drm_dbg_dp(panel->drm_dev, "failed to read DSC capability: %zd\n", rlen);
		return;
	}

	msm_dp_panel->dsc_capable =
		drm_dp_sink_supports_dsc(msm_dp_panel->dsc_dpcd) &&
		drm_dp_dsc_sink_supports_format(msm_dp_panel->dsc_dpcd, DP_DSC_RGB);

	drm_dbg_dp(panel->drm_dev, "fec=%d dsc=%d\n",
		   msm_dp_panel->fec_capable, msm_dp_panel->dsc_capable);
}

static int msm_dp_panel_read_dpcd(struct msm_dp_panel *msm_dp_panel)
{
	int rc, max_lttpr_lanes, max_lttpr_rate;
	struct msm_dp_panel_private *panel;
	struct msm_dp_link_info *link_info;
	struct msm_dp_link *link;
	u8 *dpcd, major, minor;

	panel = container_of(msm_dp_panel, struct msm_dp_panel_private, msm_dp_panel);
	dpcd = msm_dp_panel->dpcd;
	rc = drm_dp_read_dpcd_caps(panel->aux, dpcd);
	if (rc)
		return rc;

	msm_dp_panel->vsc_sdp_supported = drm_dp_vsc_sdp_supported(panel->aux, dpcd);
	link_info = &msm_dp_panel->link_info;
	link_info->revision = dpcd[DP_DPCD_REV];
	major = (link_info->revision >> 4) & 0x0f;
	minor = link_info->revision & 0x0f;

	link = panel->link;
	drm_dbg_dp(panel->drm_dev, "max_lanes=%d max_link_rate=%d\n",
		   link->max_dp_lanes, link->max_dp_link_rate);

	max_lttpr_lanes = drm_dp_lttpr_max_lane_count(link->lttpr_common_caps);
	max_lttpr_rate = drm_dp_lttpr_max_link_rate(link->lttpr_common_caps);

	/* eDP sink */
	if (msm_dp_panel->dpcd[DP_EDP_CONFIGURATION_CAP]) {
		u8 edp_rev;

		rc = drm_dp_dpcd_read_byte(panel->aux, DP_EDP_DPCD_REV, &edp_rev);
		if (rc)
			return rc;

		drm_dbg_dp(panel->drm_dev, "edp_rev=0x%x\n", edp_rev);

		/* For eDP v1.4+, parse the SUPPORTED_LINK_RATES table */
		if (edp_rev >= DP_EDP_14) {
			__le16 rates[DP_MAX_SUPPORTED_RATES];
			u8 bw_set;
			int i;

			rc = drm_dp_dpcd_read_data(panel->aux, DP_SUPPORTED_LINK_RATES,
						   rates, sizeof(rates));
			if (rc)
				return rc;

			rc = drm_dp_dpcd_read_byte(panel->aux, DP_LINK_BW_SET, &bw_set);
			if (rc)
				return rc;

			/* Find index of max supported link rate that does not exceed dtsi limits */
			for (i = 0; i < ARRAY_SIZE(rates); i++) {
				/*
				 * The value from the DPCD multiplied by 200 gives
				 * the link rate in kHz. Divide by 10 to convert to
				 * symbol rate, accounting for 8b/10b encoding.
				 */
				u32 rate = (le16_to_cpu(rates[i]) * 200) / 10;

				if (!rate)
					break;

				drm_dbg_dp(panel->drm_dev,
					   "SUPPORTED_LINK_RATES[%d]: %d\n", i, rate);

				/*
				 * Limit link rate from link-frequencies of endpoint
				 * property of dtsi
				 */
				if (rate > link->max_dp_link_rate)
					break;

				/* Limit link rate from LTTPR capabilities, if any */
				if (max_lttpr_rate && rate > max_lttpr_rate)
					break;

				link_info->rate = rate;
				link_info->supported_rates[i] = rate;
				link_info->rate_set = i;
			}

			/* Only use LINK_RATE_SET if LINK_BW_SET hasn't already been written to */
			if (!bw_set && link_info->rate)
				link_info->use_rate_set = true;
		}
	}

	/* Fall back on MAX_LINK_RATE/LINK_BW_SET (DP, eDP <= v1.3) */
	if (!link_info->rate) {
		link_info->rate = drm_dp_max_link_rate(dpcd);

		/* Limit link rate from link-frequencies of endpoint property of dtsi */
		if (link_info->rate > link->max_dp_link_rate)
			link_info->rate = link->max_dp_link_rate;

		/* Limit link rate from LTTPR capabilities, if any */
		if (max_lttpr_rate && max_lttpr_rate < link_info->rate)
			link_info->rate = max_lttpr_rate;
	}

	link_info->num_lanes = drm_dp_max_lane_count(dpcd);

	/* Limit data lanes from data-lanes of endpoint property of dtsi */
	if (link_info->num_lanes > link->max_dp_lanes)
		link_info->num_lanes = link->max_dp_lanes;

	/* Limit data lanes from LTTPR capabilities, if any */
	if (max_lttpr_lanes && max_lttpr_lanes < link_info->num_lanes)
		link_info->num_lanes = max_lttpr_lanes;

	drm_dbg_dp(panel->drm_dev, "version: %d.%d\n", major, minor);
	drm_dbg_dp(panel->drm_dev, "link_rate=%d\n", link_info->rate);
	drm_dbg_dp(panel->drm_dev, "link_rate_set=%d\n", link_info->rate_set);
	drm_dbg_dp(panel->drm_dev, "use_rate_set=%d\n", link_info->use_rate_set);
	drm_dbg_dp(panel->drm_dev, "lane_count=%d\n", link_info->num_lanes);

	if (drm_dp_enhanced_frame_cap(dpcd))
		link_info->capabilities |= DP_LINK_CAP_ENHANCED_FRAMING;

	msm_dp_panel_read_psr_cap(panel);
	msm_dp_panel_read_dsc_caps(panel);

	return rc;
}

static u32 msm_dp_panel_get_supported_bpp(struct msm_dp_panel *msm_dp_panel,
		u32 mode_edid_bpp, u32 mode_pclk_khz)
{
	const struct msm_dp_link_info *link_info;
	const u32 max_supported_bpp = 30, min_supported_bpp = 18;
	u32 bpp, data_rate_khz;

	bpp = min(mode_edid_bpp, max_supported_bpp);

	link_info = &msm_dp_panel->link_info;
	data_rate_khz = link_info->num_lanes * link_info->rate * 8;

	do {
		if (mode_pclk_khz * bpp <= data_rate_khz)
			return bpp;
		bpp -= 6;
	} while (bpp > min_supported_bpp);

	return min_supported_bpp;
}

static void msm_dp_panel_clear_sink_caps(struct msm_dp_panel *msm_dp_panel,
					 struct drm_connector *connector,
					 struct drm_dp_aux *aux)
{
	drm_edid_connector_update(connector, NULL);
	drm_edid_free(msm_dp_panel->drm_edid);
	msm_dp_panel->drm_edid = NULL;
	drm_dp_cec_unset_edid(aux);
	msm_dp_panel_reset_dsc(msm_dp_panel);
}

int msm_dp_panel_read_sink_caps(struct msm_dp_panel *msm_dp_panel,
	struct drm_connector *connector)
{
	int rc, bw_code;
	int count;
	struct msm_dp_panel_private *panel;

	if (!msm_dp_panel || !connector) {
		DRM_ERROR("invalid input\n");
		return -EINVAL;
	}

	panel = container_of(msm_dp_panel, struct msm_dp_panel_private, msm_dp_panel);

	rc = msm_dp_panel_read_dpcd(msm_dp_panel);
	if (rc) {
		DRM_ERROR("read dpcd failed %d\n", rc);
		return rc;
	}

	bw_code = drm_dp_link_rate_to_bw_code(msm_dp_panel->link_info.rate);
	if (!is_link_rate_valid(bw_code) ||
			!is_lane_count_valid(msm_dp_panel->link_info.num_lanes) ||
			(bw_code > msm_dp_panel->max_bw_code)) {
		DRM_ERROR("Illegal link rate=%d lane=%d\n", msm_dp_panel->link_info.rate,
				msm_dp_panel->link_info.num_lanes);
		return -EINVAL;
	}

	if (drm_dp_is_branch(msm_dp_panel->dpcd)) {
		count = drm_dp_read_sink_count(panel->aux);
		if (count < 0)
			return count;

		panel->link->sink_count = count;
		if (!count) {
			msm_dp_panel_clear_sink_caps(msm_dp_panel, connector, panel->aux);
			return -ENOTCONN;
		}
	}

	rc = drm_dp_read_downstream_info(panel->aux, msm_dp_panel->dpcd,
					 msm_dp_panel->downstream_ports);
	if (rc) {
		msm_dp_panel_clear_sink_caps(msm_dp_panel, connector, panel->aux);
		return rc;
	}

	drm_edid_free(msm_dp_panel->drm_edid);

	msm_dp_panel->drm_edid = drm_edid_read_ddc(connector, &panel->aux->ddc);

	drm_edid_connector_update(connector, msm_dp_panel->drm_edid);

	if (msm_dp_panel->drm_edid) {
		drm_dp_cec_attach(panel->aux,
					connector->display_info.source_physical_address);
	} else {
		DRM_ERROR("panel edid read failed\n");
		msm_dp_panel_clear_sink_caps(msm_dp_panel, connector, panel->aux);
		/* check edid read fail is due to unplug */
		if (!msm_dp_aux_is_link_connected(panel->aux)) {
			rc = -ETIMEDOUT;
			goto end;
		}
	}

end:
	return rc;
}

void msm_dp_panel_unplugged(struct msm_dp_panel *msm_dp_panel,
			    struct drm_connector *connector)
{
	drm_edid_connector_update(connector, NULL);
	drm_edid_free(msm_dp_panel->drm_edid);
	msm_dp_panel->drm_edid = NULL;
	msm_dp_panel_reset_dsc(msm_dp_panel);
}

u32 msm_dp_panel_get_mode_bpp(struct msm_dp_panel *msm_dp_panel,
		u32 mode_edid_bpp, u32 mode_pclk_khz)
{
	struct msm_dp_panel_private *panel;
	u32 bpp;

	if (!msm_dp_panel || !mode_edid_bpp || !mode_pclk_khz) {
		DRM_ERROR("invalid input\n");
		return 0;
	}

	panel = container_of(msm_dp_panel, struct msm_dp_panel_private, msm_dp_panel);

	if (msm_dp_panel->video_test)
		bpp = msm_dp_link_bit_depth_to_bpp(
				panel->link->test_video.test_bit_depth);
	else
		bpp = msm_dp_panel_get_supported_bpp(msm_dp_panel, mode_edid_bpp,
				mode_pclk_khz);

	return bpp;
}

int msm_dp_panel_get_modes(struct msm_dp_panel *msm_dp_panel,
	struct drm_connector *connector)
{
	if (!msm_dp_panel) {
		DRM_ERROR("invalid input\n");
		return -EINVAL;
	}

	if (msm_dp_panel->drm_edid)
		return drm_edid_connector_add_modes(connector);

	return 0;
}

bool msm_dp_panel_dsc_available(const struct msm_dp_panel *msm_dp_panel)
{
	return msm_dp_panel && msm_dp_panel->fec_capable &&
	       msm_dp_panel->dsc_capable;
}

bool msm_dp_panel_dsc_enabled(const struct msm_dp_panel *msm_dp_panel)
{
	return msm_dp_panel && msm_dp_panel->dsc_en;
}

bool msm_dp_panel_fec_enabled(const struct msm_dp_panel *msm_dp_panel)
{
	return msm_dp_panel && msm_dp_panel->fec_en;
}

struct drm_dsc_config *msm_dp_panel_get_dsc_config(struct msm_dp_panel *msm_dp_panel)
{
	if (!msm_dp_panel_dsc_enabled(msm_dp_panel))
		return NULL;

	return &msm_dp_panel->dsc;
}

static bool msm_dp_panel_mode_fits_link(struct msm_dp_panel *msm_dp_panel,
					u32 mode_pclk_khz, u32 mode_bpp)
{
	const struct msm_dp_link_info *link_info = &msm_dp_panel->link_info;
	u64 mode_rate_khz = (u64)mode_pclk_khz * mode_bpp;
	u64 supported_rate_khz = (u64)link_info->num_lanes * link_info->rate * 8;

	return mode_rate_khz <= supported_rate_khz;
}

static bool msm_dp_panel_dsc_link_fits(struct msm_dp_panel *msm_dp_panel,
				       u32 mode_pclk_khz)
{
	return msm_dp_panel_mode_fits_link(msm_dp_panel, mode_pclk_khz,
					   MSM_DP_DSC_TARGET_BPP);
}

static u8 msm_dp_panel_pick_dsc_bpc(struct msm_dp_panel *msm_dp_panel, u32 mode_bpp)
{
	u8 supported_bpcs[3];
	u8 line_buf_depth;
	u8 max_bpc;
	int count;
	int i;

	max_bpc = mode_bpp / 3;
	count = drm_dp_dsc_sink_supported_input_bpcs(msm_dp_panel->dsc_dpcd,
						     supported_bpcs);
	line_buf_depth = drm_dp_dsc_sink_line_buf_depth(msm_dp_panel->dsc_dpcd);

	for (i = 0; i < count; i++) {
		if (supported_bpcs[i] > max_bpc)
			continue;
		if (supported_bpcs[i] > MSM_DP_DSC_MAX_BPC)
			continue;
		if (line_buf_depth && supported_bpcs[i] > line_buf_depth)
			continue;

		switch (supported_bpcs[i]) {
		case 8:
		case 10:
		case 12:
			return supported_bpcs[i];
		default:
			break;
		}
	}

	return 0;
}

static u8 msm_dp_panel_pick_slice_height(u16 pic_height)
{
	if (pic_height % 108 == 0)
		return 108;
	if (pic_height % 16 == 0)
		return 16;
	if (pic_height % 12 == 0)
		return 12;

	return 15;
}

static int msm_dp_panel_pick_slice_count(struct msm_dp_panel *msm_dp_panel,
					 const struct drm_display_mode *mode)
{
	static const u8 slice_counts[] = { 2, 4, 6, 8, 10, 12, 16, 20, 24 };
	u32 slice_count_mask;
	u32 max_slice_width;
	int max_slice_throughput;
	int i;

	slice_count_mask = drm_dp_dsc_sink_slice_count_mask(msm_dp_panel->dsc_dpcd, false);
	max_slice_width = drm_dp_dsc_sink_max_slice_width(msm_dp_panel->dsc_dpcd);
	max_slice_throughput =
		drm_dp_dsc_sink_max_slice_throughput(msm_dp_panel->dsc_dpcd,
						     mode->clock, true);

	for (i = 0; i < ARRAY_SIZE(slice_counts); i++) {
		u8 slice_count = slice_counts[i];
		u32 slice_width;

		if (!(slice_count_mask & drm_dp_dsc_slice_count_to_mask(slice_count)))
			continue;

		if (mode->hdisplay % slice_count)
			continue;

		slice_width = mode->hdisplay / slice_count;
		if (max_slice_width && slice_width > max_slice_width)
			continue;

		if (max_slice_throughput &&
		    DIV_ROUND_UP(mode->clock, slice_count) > max_slice_throughput)
			continue;

		return slice_count;
	}

	return 0;
}

static int msm_dp_panel_populate_dsc_config(struct msm_dp_panel *msm_dp_panel,
					    const struct drm_display_mode *mode,
					    u32 mode_bpp,
					    struct drm_dsc_config *dsc)
{
	u8 dsc_rev;
	u8 line_buf_depth;
	u8 slice_count;
	u8 bpc;
	int ret;

	if (!msm_dp_panel_dsc_available(msm_dp_panel))
		return -EINVAL;

	bpc = msm_dp_panel_pick_dsc_bpc(msm_dp_panel, mode_bpp);
	if (!bpc)
		return -EINVAL;

	slice_count = msm_dp_panel_pick_slice_count(msm_dp_panel, mode);
	if (!slice_count)
		return -EINVAL;

	memset(dsc, 0, sizeof(*dsc));

	dsc_rev = msm_dp_panel->dsc_dpcd[DP_DSC_REV - DP_DSC_SUPPORT];
	dsc->dsc_version_major = FIELD_GET(DP_DSC_MAJOR_MASK, dsc_rev);
	dsc->dsc_version_minor = FIELD_GET(DP_DSC_MINOR_MASK, dsc_rev);
	if (dsc->dsc_version_major != 1 || dsc->dsc_version_minor < 1)
		return -EINVAL;
	if (dsc->dsc_version_minor > 2)
		dsc->dsc_version_minor = 2;

	dsc->bits_per_component = bpc;
	dsc->bits_per_pixel = MSM_DP_DSC_TARGET_BPP << 4;
	dsc->pic_width = mode->hdisplay;
	dsc->pic_height = mode->vdisplay;
	dsc->slice_count = slice_count;
	dsc->slice_width = mode->hdisplay / slice_count;
	dsc->slice_height = msm_dp_panel_pick_slice_height(mode->vdisplay);
	dsc->simple_422 = false;
	dsc->convert_rgb = true;
	dsc->vbr_enable = false;
	dsc->native_422 = false;
	dsc->native_420 = false;
	dsc->block_pred_enable =
		msm_dp_panel->dsc_dpcd[DP_DSC_BLK_PREDICTION_SUPPORT - DP_DSC_SUPPORT] &
		DP_DSC_BLK_PREDICTION_IS_SUPPORTED;

	drm_dsc_set_const_params(dsc);
	drm_dsc_set_rc_buf_thresh(dsc);

	ret = drm_dsc_setup_rc_params(dsc, dsc->dsc_version_minor >= 2 ?
				      DRM_DSC_1_2_444 : DRM_DSC_1_1_PRE_SCR);
	if (ret)
		return ret;

	dsc->initial_scale_value = drm_dsc_initial_scale_value(dsc);

	line_buf_depth = drm_dp_dsc_sink_line_buf_depth(msm_dp_panel->dsc_dpcd);
	if (!line_buf_depth)
		line_buf_depth = bpc + 1;
	dsc->line_buf_depth = min_t(u8, line_buf_depth, bpc + 1);

	return drm_dsc_compute_rc_parameters(dsc);
}

bool msm_dp_panel_dsc_mode_valid(struct msm_dp_panel *msm_dp_panel,
				 const struct drm_display_mode *mode,
				 u32 mode_bpp, u32 mode_pclk_khz)
{
	struct drm_dsc_config dsc;

	if (!msm_dp_panel_dsc_link_fits(msm_dp_panel, mode_pclk_khz))
		return false;

	return !msm_dp_panel_populate_dsc_config(msm_dp_panel, mode, mode_bpp, &dsc);
}

bool msm_dp_panel_dsc_needed(struct msm_dp_panel *msm_dp_panel,
			     const struct drm_display_mode *mode,
			     u32 mode_bpp, u32 mode_pclk_khz)
{
	if (!msm_dp_panel_dsc_available(msm_dp_panel))
		return false;

	if (msm_dp_panel_mode_fits_link(msm_dp_panel, mode_pclk_khz, mode_bpp))
		return false;

	return msm_dp_panel_dsc_mode_valid(msm_dp_panel, mode, mode_bpp,
					   mode_pclk_khz);
}

int msm_dp_panel_prepare_dsc(struct msm_dp_panel *msm_dp_panel,
			     const struct drm_display_mode *mode,
			     u32 mode_bpp, u32 mode_pclk_khz)
{
	struct msm_dp_panel_private *panel =
		container_of(msm_dp_panel, struct msm_dp_panel_private, msm_dp_panel);
	const struct drm_dsc_config *dsc = &msm_dp_panel->dsc;
	int ret;

	msm_dp_panel_reset_dsc_state(msm_dp_panel);

	if (!msm_dp_panel_dsc_needed(msm_dp_panel, mode, mode_bpp, mode_pclk_khz))
		return 0;

	ret = msm_dp_panel_populate_dsc_config(msm_dp_panel, mode, mode_bpp,
					       &msm_dp_panel->dsc);
	if (ret)
		return ret;

	msm_dp_panel->dsc_en = true;
	msm_dp_panel->fec_en = true;

	drm_dbg_dp(panel->drm_dev,
		   "DSC prepared: %ux%u@%d source_bpp=%u target_bpp=%u bpc=%u slices=%u slice_width=%u slice_height=%u bytes_per_line=%u\n",
		   mode->hdisplay, mode->vdisplay, drm_mode_vrefresh(mode),
		   mode_bpp, drm_dsc_get_bpp_int(dsc), dsc->bits_per_component,
		   dsc->slice_count, dsc->slice_width, dsc->slice_height,
		   msm_dsc_get_bytes_per_line(dsc));

	return 0;
}

static u8 msm_dp_panel_get_edid_checksum(const struct edid *edid)
{
	edid += edid->extensions;

	return edid->checksum;
}

void msm_dp_panel_handle_sink_request(struct msm_dp_panel *msm_dp_panel)
{
	struct msm_dp_panel_private *panel;

	if (!msm_dp_panel) {
		DRM_ERROR("invalid input\n");
		return;
	}

	panel = container_of(msm_dp_panel, struct msm_dp_panel_private, msm_dp_panel);

	if (panel->link->sink_request & DP_TEST_LINK_EDID_READ) {
		/* FIXME: get rid of drm_edid_raw() */
		const struct edid *edid = drm_edid_raw(msm_dp_panel->drm_edid);
		u8 checksum;

		if (edid)
			checksum = msm_dp_panel_get_edid_checksum(edid);
		else
			checksum = msm_dp_panel->connector->real_edid_checksum;

		msm_dp_link_send_edid_checksum(panel->link, checksum);
		msm_dp_link_send_test_response(panel->link);
	}
}

static void msm_dp_panel_tpg_enable(struct msm_dp_panel *msm_dp_panel,
				    struct drm_display_mode *drm_mode)
{
	struct msm_dp_panel_private *panel =
		container_of(msm_dp_panel, struct msm_dp_panel_private, msm_dp_panel);
	u32 hsync_period, vsync_period;
	u32 display_v_start, display_v_end;
	u32 hsync_start_x, hsync_end_x;
	u32 v_sync_width;
	u32 hsync_ctl;
	u32 display_hctl;

	/* TPG config parameters*/
	hsync_period = drm_mode->htotal;
	vsync_period = drm_mode->vtotal;

	display_v_start = ((drm_mode->vtotal - drm_mode->vsync_start) *
					hsync_period);
	display_v_end = ((vsync_period - (drm_mode->vsync_start -
					drm_mode->vdisplay))
					* hsync_period) - 1;

	display_v_start += drm_mode->htotal - drm_mode->hsync_start;
	display_v_end -= (drm_mode->hsync_start - drm_mode->hdisplay);

	hsync_start_x = drm_mode->htotal - drm_mode->hsync_start;
	hsync_end_x = hsync_period - (drm_mode->hsync_start -
					drm_mode->hdisplay) - 1;

	v_sync_width = drm_mode->vsync_end - drm_mode->vsync_start;

	hsync_ctl = (hsync_period << 16) |
			(drm_mode->hsync_end - drm_mode->hsync_start);
	display_hctl = (hsync_end_x << 16) | hsync_start_x;


	msm_dp_write_p0(panel, MMSS_DP_INTF_HSYNC_CTL, hsync_ctl);
	msm_dp_write_p0(panel, MMSS_DP_INTF_VSYNC_PERIOD_F0, vsync_period *
			hsync_period);
	msm_dp_write_p0(panel, MMSS_DP_INTF_VSYNC_PULSE_WIDTH_F0, v_sync_width *
			hsync_period);
	msm_dp_write_p0(panel, MMSS_DP_INTF_VSYNC_PERIOD_F1, 0);
	msm_dp_write_p0(panel, MMSS_DP_INTF_VSYNC_PULSE_WIDTH_F1, 0);
	msm_dp_write_p0(panel, MMSS_DP_INTF_DISPLAY_HCTL, display_hctl);
	msm_dp_write_p0(panel, MMSS_DP_INTF_ACTIVE_HCTL, 0);
	msm_dp_write_p0(panel, MMSS_INTF_DISPLAY_V_START_F0, display_v_start);
	msm_dp_write_p0(panel, MMSS_DP_INTF_DISPLAY_V_END_F0, display_v_end);
	msm_dp_write_p0(panel, MMSS_INTF_DISPLAY_V_START_F1, 0);
	msm_dp_write_p0(panel, MMSS_DP_INTF_DISPLAY_V_END_F1, 0);
	msm_dp_write_p0(panel, MMSS_DP_INTF_ACTIVE_V_START_F0, 0);
	msm_dp_write_p0(panel, MMSS_DP_INTF_ACTIVE_V_END_F0, 0);
	msm_dp_write_p0(panel, MMSS_DP_INTF_ACTIVE_V_START_F1, 0);
	msm_dp_write_p0(panel, MMSS_DP_INTF_ACTIVE_V_END_F1, 0);
	msm_dp_write_p0(panel, MMSS_DP_INTF_POLARITY_CTL, 0);

	msm_dp_write_p0(panel, MMSS_DP_TPG_MAIN_CONTROL,
				DP_TPG_CHECKERED_RECT_PATTERN);
	msm_dp_write_p0(panel, MMSS_DP_TPG_VIDEO_CONFIG,
				DP_TPG_VIDEO_CONFIG_BPP_8BIT |
				DP_TPG_VIDEO_CONFIG_RGB);
	msm_dp_write_p0(panel, MMSS_DP_BIST_ENABLE,
				DP_BIST_ENABLE_DPBIST_EN);
	msm_dp_write_p0(panel, MMSS_DP_TIMING_ENGINE_EN,
				DP_TIMING_ENGINE_EN_EN);
	drm_dbg_dp(panel->drm_dev, "%s: enabled tpg\n", __func__);
}

static void msm_dp_panel_tpg_disable(struct msm_dp_panel *msm_dp_panel)
{
	struct msm_dp_panel_private *panel =
		container_of(msm_dp_panel, struct msm_dp_panel_private, msm_dp_panel);

	msm_dp_write_p0(panel, MMSS_DP_TPG_MAIN_CONTROL, 0x0);
	msm_dp_write_p0(panel, MMSS_DP_BIST_ENABLE, 0x0);
	msm_dp_write_p0(panel, MMSS_DP_TIMING_ENGINE_EN, 0x0);
}

void msm_dp_panel_tpg_config(struct msm_dp_panel *msm_dp_panel, bool enable)
{
	struct msm_dp_panel_private *panel;

	if (!msm_dp_panel) {
		DRM_ERROR("invalid input\n");
		return;
	}

	panel = container_of(msm_dp_panel, struct msm_dp_panel_private, msm_dp_panel);

	if (!panel->panel_on) {
		drm_dbg_dp(panel->drm_dev,
				"DP panel not enabled, handle TPG on next on\n");
		return;
	}

	if (!enable) {
		msm_dp_panel_tpg_disable(msm_dp_panel);
		return;
	}

	drm_dbg_dp(panel->drm_dev, "calling panel's tpg_enable\n");
	msm_dp_panel_tpg_enable(msm_dp_panel, &panel->msm_dp_panel.msm_dp_mode.drm_mode);
}

void msm_dp_panel_clear_dsc_dto(struct msm_dp_panel *msm_dp_panel)
{
	struct msm_dp_panel_private *panel =
		container_of(msm_dp_panel, struct msm_dp_panel_private, msm_dp_panel);

	msm_dp_write_p0(panel, MMSS_DP_DSC_DTO_COUNT, 0x0);
	msm_dp_write_p0(panel, MMSS_DP_DSC_DTO, 0x0);
	msm_dp_write_link(panel, REG_DP_COMPRESSION_MODE_CTRL, 0x0);
}

static void msm_dp_panel_send_sdp(struct msm_dp_panel_private *panel,
				  const struct dp_sdp *sdp, u32 base)
{
	u32 header[2];
	u32 val;
	int i;

	msm_dp_utils_pack_sdp_header(&sdp->sdp_header, header);

	msm_dp_write_link(panel, base, header[0]);
	msm_dp_write_link(panel, base + 4, header[1]);

	for (i = 0; i < sizeof(sdp->db); i += 4) {
		val = sdp->db[i] | sdp->db[i + 1] << 8 | sdp->db[i + 2] << 16 |
		      sdp->db[i + 3] << 24;
		msm_dp_write_link(panel, base + 8 + i, val);
	}
}

static void msm_dp_panel_send_vsc_sdp(struct msm_dp_panel_private *panel,
				      const struct dp_sdp *vsc_sdp)
{
	msm_dp_panel_send_sdp(panel, vsc_sdp, MMSS_DP_GENERIC0_0);
}

static void msm_dp_panel_update_sdp(struct msm_dp_panel_private *panel)
{
	u32 hw_revision = panel->msm_dp_panel.hw_revision;

	if (hw_revision >= DP_HW_VERSION_1_0 &&
	    hw_revision < DP_HW_VERSION_1_2) {
		msm_dp_write_link(panel, MMSS_DP_SDP_CFG3, UPDATE_SDP);
		msm_dp_write_link(panel, MMSS_DP_SDP_CFG3, 0x0);
	}
}

static u32 msm_dp_panel_pack_bytes(const u8 *bytes)
{
	return bytes[0] | bytes[1] << 8 | bytes[2] << 16 | bytes[3] << 24;
}

static void msm_dp_panel_pack_dto_ratio(u32 target_bpp, u32 *dto_n, u32 *dto_d)
{
	*dto_n = target_bpp;
	*dto_d = 24;

	while (!(*dto_n & 1) && !(*dto_d & 1)) {
		*dto_n >>= 1;
		*dto_d >>= 1;
	}
}

static void msm_dp_panel_write_pps(struct msm_dp_panel_private *panel,
				   const struct drm_dsc_config *dsc)
{
	struct drm_dsc_picture_parameter_set pps;
	struct dp_sdp_header pps_header;
	u8 pps_parity[MSM_DP_DSC_PPS_SIZE / 4] = { 0 };
	u8 header[4];
	u8 header_parity[4];
	u8 *pps_payload = (u8 *)&pps;
	u32 word;
	int i;

	drm_dsc_pps_payload_pack(&pps, dsc);
	drm_dsc_dp_pps_header_init(&pps_header);

	header[0] = pps_header.HB0;
	header[1] = pps_header.HB1;
	header[2] = pps_header.HB2;
	header[3] = pps_header.HB3;

	for (i = 0; i < ARRAY_SIZE(header); i++)
		header_parity[i] = msm_dp_utils_calculate_parity(header[i]);

	msm_dp_write_link(panel, DP_PPS_HB_0_3, msm_dp_panel_pack_bytes(header));
	msm_dp_write_link(panel, DP_PPS_PB_0_3, msm_dp_panel_pack_bytes(header_parity));

	for (i = 0; i < ARRAY_SIZE(pps_parity); i++) {
		word = msm_dp_panel_pack_bytes(&pps_payload[i * 4]);
		pps_parity[i] = msm_dp_utils_calculate_parity(word);
		msm_dp_write_link(panel, DP_PPS_PPS_0_3 + i * 4, word);
	}

	for (i = 0; i < DIV_ROUND_UP(ARRAY_SIZE(pps_parity), 4); i++) {
		u8 parity[4] = { 0 };
		int j;

		for (j = 0; j < ARRAY_SIZE(parity); j++) {
			int index = i * 4 + j;

			if (index < ARRAY_SIZE(pps_parity))
				parity[j] = pps_parity[index];
		}

		msm_dp_write_link(panel, DP_PPS_PB_4_7 + i * 4,
				  msm_dp_panel_pack_bytes(parity));
	}
}

static void msm_dp_panel_flush_pps(struct msm_dp_panel_private *panel)
{
	u32 flush;

	flush = msm_dp_read_link(panel, MMSS_DP_FLUSH);
	flush &= ~BIT(2);
	flush |= BIT(0);
	msm_dp_write_link(panel, MMSS_DP_FLUSH, flush);

	msm_dp_panel_update_sdp(panel);
}

void msm_dp_panel_config_dsc(struct msm_dp_panel *msm_dp_panel, bool enable)
{
	struct msm_dp_panel_private *panel =
		container_of(msm_dp_panel, struct msm_dp_panel_private, msm_dp_panel);
	const struct drm_dsc_config *dsc = &msm_dp_panel->dsc;
	u32 dto_count, dto_n, dto_d, bytes_per_line, eol_byte_num;
	u32 compression = 0;
	u32 dto = 0;

	if (!enable || !msm_dp_panel_dsc_enabled(msm_dp_panel)) {
		msm_dp_panel_clear_dsc_dto(msm_dp_panel);
		msm_dp_write_p0(panel, MMSS_DP_DSC_DTO,
				MMSS_DP_DSC_DTO_ACK_OVERRIDE);
		drm_dbg_dp(panel->drm_dev, "DSC ctrl disabled\n");
		return;
	}

	bytes_per_line = msm_dsc_get_bytes_per_line(dsc);
	eol_byte_num = ALIGN(bytes_per_line, 3) - bytes_per_line;
	dto_count = max_t(u32, DIV_ROUND_UP(bytes_per_line, 6), 1) - 1;
	msm_dp_panel_pack_dto_ratio(drm_dsc_get_bpp_int(dsc), &dto_n, &dto_d);

	dto = MMSS_DP_DSC_DTO_EN |
	      MMSS_DP_DSC_DTO_OUT_EN |
	      FIELD_PREP(MMSS_DP_DSC_DTO_N, dto_n) |
	      FIELD_PREP(MMSS_DP_DSC_DTO_D, dto_d);

	compression = DP_COMPRESSION_MODE_DSC_EN |
		      FIELD_PREP(DP_COMPRESSION_MODE_EOL_BYTE_NUM, eol_byte_num) |
		      FIELD_PREP(DP_COMPRESSION_MODE_SLICE_PER_PKT, dsc->slice_count - 1) |
		      FIELD_PREP(DP_COMPRESSION_MODE_BE_IN_LANE, MSM_DP_DSC_BE_IN_LANE) |
		      FIELD_PREP(DP_COMPRESSION_MODE_BYTES_PER_PKT, dsc->slice_chunk_size);

	msm_dp_write_p0(panel, MMSS_DP_DSC_DTO_COUNT, dto_count);
	msm_dp_write_p0(panel, MMSS_DP_DSC_DTO, dto);
	msm_dp_write_link(panel, REG_DP_COMPRESSION_MODE_CTRL, compression);

	drm_dbg_dp(panel->drm_dev,
		   "DSC ctrl: bytes_per_line=%u chunk=%u eol=%u dto_count=%u dto=%u/%u compression=0x%x\n",
		   bytes_per_line, dsc->slice_chunk_size, eol_byte_num,
		   dto_count, dto_n, dto_d, compression);

	msm_dp_panel_write_pps(panel, dsc);
	msm_dp_panel_flush_pps(panel);
}

void msm_dp_panel_enable_vsc_sdp(struct msm_dp_panel *msm_dp_panel, struct dp_sdp *vsc_sdp)
{
	struct msm_dp_panel_private *panel =
		container_of(msm_dp_panel, struct msm_dp_panel_private, msm_dp_panel);
	u32 cfg, cfg2, misc;

	cfg = msm_dp_read_link(panel, MMSS_DP_SDP_CFG);
	cfg2 = msm_dp_read_link(panel, MMSS_DP_SDP_CFG2);
	misc = msm_dp_read_link(panel, REG_DP_MISC1_MISC0);

	cfg |= GEN0_SDP_EN;
	msm_dp_write_link(panel, MMSS_DP_SDP_CFG, cfg);

	cfg2 |= GENERIC0_SDPSIZE_VALID;
	msm_dp_write_link(panel, MMSS_DP_SDP_CFG2, cfg2);

	msm_dp_panel_send_vsc_sdp(panel, vsc_sdp);

	/* indicates presence of VSC (BIT(6) of MISC1) */
	misc |= DP_MISC1_VSC_SDP;

	drm_dbg_dp(panel->drm_dev, "vsc sdp enable=1\n");

	pr_debug("misc settings = 0x%x\n", misc);
	msm_dp_write_link(panel, REG_DP_MISC1_MISC0, misc);

	msm_dp_panel_update_sdp(panel);
}

void msm_dp_panel_disable_vsc_sdp(struct msm_dp_panel *msm_dp_panel)
{
	struct msm_dp_panel_private *panel =
		container_of(msm_dp_panel, struct msm_dp_panel_private, msm_dp_panel);
	u32 cfg, cfg2, misc;

	cfg = msm_dp_read_link(panel, MMSS_DP_SDP_CFG);
	cfg2 = msm_dp_read_link(panel, MMSS_DP_SDP_CFG2);
	misc = msm_dp_read_link(panel, REG_DP_MISC1_MISC0);

	cfg &= ~GEN0_SDP_EN;
	msm_dp_write_link(panel, MMSS_DP_SDP_CFG, cfg);

	cfg2 &= ~GENERIC0_SDPSIZE_VALID;
	msm_dp_write_link(panel, MMSS_DP_SDP_CFG2, cfg2);

	/* switch back to MSA */
	misc &= ~DP_MISC1_VSC_SDP;

	drm_dbg_dp(panel->drm_dev, "vsc sdp enable=0\n");

	pr_debug("misc settings = 0x%x\n", misc);
	msm_dp_write_link(panel, REG_DP_MISC1_MISC0, misc);

	msm_dp_panel_update_sdp(panel);
}

static int msm_dp_panel_setup_vsc_sdp_yuv_420(struct msm_dp_panel *msm_dp_panel)
{
	struct msm_dp_display_mode *msm_dp_mode;
	struct drm_dp_vsc_sdp vsc_sdp_data;
	struct dp_sdp vsc_sdp;
	ssize_t len;

	if (!msm_dp_panel) {
		DRM_ERROR("invalid input\n");
		return -EINVAL;
	}

	msm_dp_mode = &msm_dp_panel->msm_dp_mode;

	memset(&vsc_sdp_data, 0, sizeof(vsc_sdp_data));

	/* VSC SDP header as per table 2-118 of DP 1.4 specification */
	vsc_sdp_data.sdp_type = DP_SDP_VSC;
	vsc_sdp_data.revision = 0x05;
	vsc_sdp_data.length = 0x13;

	/* VSC SDP Payload for DB16 */
	vsc_sdp_data.pixelformat = DP_PIXELFORMAT_YUV420;
	vsc_sdp_data.colorimetry = DP_COLORIMETRY_DEFAULT;

	/* VSC SDP Payload for DB17 */
	vsc_sdp_data.bpc = msm_dp_mode->bpp / 3;
	vsc_sdp_data.dynamic_range = DP_DYNAMIC_RANGE_CTA;

	/* VSC SDP Payload for DB18 */
	vsc_sdp_data.content_type = DP_CONTENT_TYPE_GRAPHICS;

	len = drm_dp_vsc_sdp_pack(&vsc_sdp_data, &vsc_sdp);
	if (len < 0) {
		DRM_ERROR("unable to pack vsc sdp\n");
		return len;
	}

	msm_dp_panel_enable_vsc_sdp(msm_dp_panel, &vsc_sdp);

	return 0;
}

int msm_dp_panel_timing_cfg(struct msm_dp_panel *msm_dp_panel, bool wide_bus_en)
{
	u32 data, total_ver, total_hor;
	struct msm_dp_panel_private *panel;
	struct drm_display_mode *drm_mode;
	u32 width_blanking;
	u32 sync_start;
	u32 msm_dp_active;
	u32 total;
	u32 reg;

	panel = container_of(msm_dp_panel, struct msm_dp_panel_private, msm_dp_panel);
	drm_mode = &panel->msm_dp_panel.msm_dp_mode.drm_mode;

	drm_dbg_dp(panel->drm_dev, "width=%d hporch= %d %d %d\n",
		drm_mode->hdisplay, drm_mode->htotal - drm_mode->hsync_end,
		drm_mode->hsync_start - drm_mode->hdisplay,
		drm_mode->hsync_end - drm_mode->hsync_start);

	drm_dbg_dp(panel->drm_dev, "height=%d vporch= %d %d %d\n",
		drm_mode->vdisplay, drm_mode->vtotal - drm_mode->vsync_end,
		drm_mode->vsync_start - drm_mode->vdisplay,
		drm_mode->vsync_end - drm_mode->vsync_start);

	total_hor = drm_mode->htotal;

	total_ver = drm_mode->vtotal;

	data = total_ver;
	data <<= 16;
	data |= total_hor;

	total = data;

	data = (drm_mode->vtotal - drm_mode->vsync_start);
	data <<= 16;
	data |= (drm_mode->htotal - drm_mode->hsync_start);

	sync_start = data;

	data = drm_mode->vsync_end - drm_mode->vsync_start;
	data <<= 16;
	data |= (panel->msm_dp_panel.msm_dp_mode.v_active_low << 31);
	data |= drm_mode->hsync_end - drm_mode->hsync_start;
	data |= (panel->msm_dp_panel.msm_dp_mode.h_active_low << 15);

	width_blanking = data;

	data = drm_mode->vdisplay;
	data <<= 16;
	data |= drm_mode->hdisplay;

	msm_dp_active = data;

	msm_dp_write_link(panel, REG_DP_TOTAL_HOR_VER, total);
	msm_dp_write_link(panel, REG_DP_START_HOR_VER_FROM_SYNC, sync_start);
	msm_dp_write_link(panel, REG_DP_HSYNC_VSYNC_WIDTH_POLARITY, width_blanking);
	msm_dp_write_link(panel, REG_DP_ACTIVE_HOR_VER, msm_dp_active);

	reg = msm_dp_read_p0(panel, MMSS_DP_INTF_CONFIG);
	if (wide_bus_en)
		reg |= DP_INTF_CONFIG_DATABUS_WIDEN;
	else
		reg &= ~DP_INTF_CONFIG_DATABUS_WIDEN;

	drm_dbg_dp(panel->drm_dev, "wide_bus_en=%d reg=%#x\n", wide_bus_en, reg);

	msm_dp_write_p0(panel, MMSS_DP_INTF_CONFIG, reg);

	if (msm_dp_panel->msm_dp_mode.out_fmt_is_yuv_420)
		msm_dp_panel_setup_vsc_sdp_yuv_420(msm_dp_panel);

	panel->panel_on = true;

	return 0;
}

int msm_dp_panel_init_panel_info(struct msm_dp_panel *msm_dp_panel)
{
	struct drm_display_mode *drm_mode;
	struct msm_dp_panel_private *panel;

	drm_mode = &msm_dp_panel->msm_dp_mode.drm_mode;

	panel = container_of(msm_dp_panel, struct msm_dp_panel_private, msm_dp_panel);

	/*
	 * print resolution info as this is a result
	 * of user initiated action of cable connection
	 */
	drm_dbg_dp(panel->drm_dev, "SET NEW RESOLUTION:\n");
	drm_dbg_dp(panel->drm_dev, "%dx%d@%dfps\n",
		drm_mode->hdisplay, drm_mode->vdisplay, drm_mode_vrefresh(drm_mode));
	drm_dbg_dp(panel->drm_dev,
			"h_porches(back|front|width) = (%d|%d|%d)\n",
			drm_mode->htotal - drm_mode->hsync_end,
			drm_mode->hsync_start - drm_mode->hdisplay,
			drm_mode->hsync_end - drm_mode->hsync_start);
	drm_dbg_dp(panel->drm_dev,
			"v_porches(back|front|width) = (%d|%d|%d)\n",
			drm_mode->vtotal - drm_mode->vsync_end,
			drm_mode->vsync_start - drm_mode->vdisplay,
			drm_mode->vsync_end - drm_mode->vsync_start);
	drm_dbg_dp(panel->drm_dev, "pixel clock (KHz)=(%d)\n",
				drm_mode->clock);
	drm_dbg_dp(panel->drm_dev, "bpp = %d\n", msm_dp_panel->msm_dp_mode.bpp);

	if (msm_dp_panel->dsc_en)
		msm_dp_panel->msm_dp_mode.bpp = msm_dp_panel->dsc.bits_per_component * 3;
	else
		msm_dp_panel->msm_dp_mode.bpp =
			msm_dp_panel_get_mode_bpp(msm_dp_panel,
						  msm_dp_panel->msm_dp_mode.bpp,
						  msm_dp_panel->msm_dp_mode.drm_mode.clock);

	drm_dbg_dp(panel->drm_dev, "updated bpp = %d\n",
				msm_dp_panel->msm_dp_mode.bpp);

	return 0;
}

struct msm_dp_panel *msm_dp_panel_get(struct device *dev, struct drm_dp_aux *aux,
			      struct msm_dp_link *link,
			      void __iomem *link_base,
			      void __iomem *p0_base)
{
	struct msm_dp_panel_private *panel;
	struct msm_dp_panel *msm_dp_panel;

	if (!dev || !aux || !link) {
		DRM_ERROR("invalid input\n");
		return ERR_PTR(-EINVAL);
	}

	panel = devm_kzalloc(dev, sizeof(*panel), GFP_KERNEL);
	if (!panel)
		return ERR_PTR(-ENOMEM);

	panel->dev = dev;
	panel->aux = aux;
	panel->link = link;
	panel->link_base = link_base;
	panel->p0_base = p0_base;

	msm_dp_panel = &panel->msm_dp_panel;
	msm_dp_panel->max_bw_code = DP_LINK_BW_8_1;

	return msm_dp_panel;
}

void msm_dp_panel_put(struct msm_dp_panel *msm_dp_panel)
{
	if (!msm_dp_panel)
		return;

	drm_edid_free(msm_dp_panel->drm_edid);
}
