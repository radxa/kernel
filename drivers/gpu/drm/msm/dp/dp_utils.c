/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024, The Linux Foundation. All rights reserved.
 */

#include <linux/types.h>
#include <drm/display/drm_hdmi_helper.h>
#include <drm/drm_property.h>
#include <drm/drm_crtc.h>

#include "dp_utils.h"

#define DP_SDP_HEADER_SIZE		8

u8 msm_dp_utils_get_g0_value(u8 data)
{
	u8 c[4];
	u8 g[4];
	u8 ret_data = 0;
	u8 i;

	for (i = 0; i < 4; i++)
		c[i] = (data >> i) & 0x01;

	g[0] = c[3];
	g[1] = c[0] ^ c[3];
	g[2] = c[1];
	g[3] = c[2];

	for (i = 0; i < 4; i++)
		ret_data = ((g[i] & 0x01) << i) | ret_data;

	return ret_data;
}

u8 msm_dp_utils_get_g1_value(u8 data)
{
	u8 c[4];
	u8 g[4];
	u8 ret_data = 0;
	u8 i;

	for (i = 0; i < 4; i++)
		c[i] = (data >> i) & 0x01;

	g[0] = c[0] ^ c[3];
	g[1] = c[0] ^ c[1] ^ c[3];
	g[2] = c[1] ^ c[2];
	g[3] = c[2] ^ c[3];

	for (i = 0; i < 4; i++)
		ret_data = ((g[i] & 0x01) << i) | ret_data;

	return ret_data;
}

u8 msm_dp_utils_calculate_parity(u32 data)
{
	u8 x0 = 0;
	u8 x1 = 0;
	u8 ci = 0;
	u8 iData = 0;
	u8 i = 0;
	u8 parity_byte;
	u8 num_byte = data > 0xFF ? 8 : 2;

	for (i = 0; i < num_byte; i++) {
		iData = (data >> i * 4) & 0xF;

		ci = iData ^ x1;
		x1 = x0 ^ msm_dp_utils_get_g1_value(ci);
		x0 = msm_dp_utils_get_g0_value(ci);
	}

	parity_byte = x1 | (x0 << 4);

	return parity_byte;
}

void msm_dp_utils_pack_sdp_header(const struct dp_sdp_header *sdp_header,
				  u32 header_buff[2])
{
	header_buff[0] = FIELD_PREP(HEADER_0_MASK, sdp_header->HB0) |
		FIELD_PREP(PARITY_0_MASK, msm_dp_utils_calculate_parity(sdp_header->HB0)) |
		FIELD_PREP(HEADER_1_MASK, sdp_header->HB1) |
		FIELD_PREP(PARITY_1_MASK, msm_dp_utils_calculate_parity(sdp_header->HB1));

	header_buff[1] = FIELD_PREP(HEADER_2_MASK, sdp_header->HB2) |
		FIELD_PREP(PARITY_2_MASK, msm_dp_utils_calculate_parity(sdp_header->HB2)) |
		FIELD_PREP(HEADER_3_MASK, sdp_header->HB3) |
		FIELD_PREP(PARITY_3_MASK, msm_dp_utils_calculate_parity(sdp_header->HB3));
}

bool msm_dp_utils_link_fits(unsigned int clock, unsigned int bpp,
			    unsigned int rate, unsigned int lanes, bool yuv420)
{
	u64 available = (u64)rate * lanes * 8;

	if (!clock || !bpp || !rate || !lanes)
		return false;

	if (yuv420)
		available *= 2;

	return (u64)clock * bpp <= available;
}

int msm_dp_utils_select_bpp(unsigned int bpc, unsigned int max_bpc,
			    unsigned int clock, unsigned int rate,
			    unsigned int lanes, bool yuv420, bool hdr)
{
	unsigned int bpp;

	if (hdr && bpc < 10)
		return -EINVAL;

	bpc = min3(bpc ? bpc : 8, max_bpc, 10U);
	bpc = round_down(bpc, 2);
	if (bpc < (hdr ? 10 : 6))
		return -EINVAL;

	for (bpp = bpc * 3; bpp >= (hdr ? 30 : 18); bpp -= 6) {
		if (msm_dp_utils_link_fits(clock, bpp, rate, lanes, yuv420))
			return bpp;
	}

	return -EINVAL;
}

int msm_dp_utils_pack_vsc_sdp(struct dp_sdp *sdp, enum drm_colorspace colorspace,
			      unsigned int bpc, bool yuv420)
{
	struct drm_dp_vsc_sdp vsc = {
		.sdp_type = DP_SDP_VSC,
		.revision = 0x05,
		.length = 0x13,
		.pixelformat = yuv420 ? DP_PIXELFORMAT_YUV420 : DP_PIXELFORMAT_RGB,
		.bpc = bpc,
		.dynamic_range = yuv420 ? DP_DYNAMIC_RANGE_CTA : DP_DYNAMIC_RANGE_VESA,
		.content_type = DP_CONTENT_TYPE_GRAPHICS,
	};
	ssize_t ret;

	switch (colorspace) {
	case DRM_MODE_COLORIMETRY_DEFAULT:
		vsc.colorimetry = DP_COLORIMETRY_DEFAULT;
		break;
	case DRM_MODE_COLORIMETRY_BT2020_RGB:
		if (yuv420)
			return -EINVAL;
		vsc.colorimetry = DP_COLORIMETRY_BT2020_RGB;
		break;
	case DRM_MODE_COLORIMETRY_DCI_P3_RGB_D65:
		if (yuv420)
			return -EINVAL;
		vsc.colorimetry = DP_COLORIMETRY_DCI_P3_RGB;
		break;
	default:
		return -EINVAL;
	}

	ret = drm_dp_vsc_sdp_pack(&vsc, sdp);
	return ret < 0 ? ret : 0;
}

int msm_dp_utils_pack_hdr_sdp(const struct drm_connector_state *conn_state,
			      struct dp_sdp *sdp)
{
	const struct hdr_output_metadata *metadata;
	const struct hdr_static_metadata *sink;
	struct hdmi_drm_infoframe frame;
	u8 buffer[HDMI_INFOFRAME_HEADER_SIZE + HDMI_DRM_INFOFRAME_SIZE];
	int ret;

	memset(sdp, 0, sizeof(*sdp));
	if (!conn_state->hdr_output_metadata)
		return 0;

	if (conn_state->hdr_output_metadata->length != sizeof(*metadata))
		return -EINVAL;

	metadata = conn_state->hdr_output_metadata->data;
	sink = &conn_state->connector->display_info.hdr_sink_metadata.hdmi_type1;
	if (metadata->metadata_type != HDMI_STATIC_METADATA_TYPE1 ||
	    metadata->hdmi_metadata_type1.metadata_type != HDMI_STATIC_METADATA_TYPE1 ||
	    metadata->hdmi_metadata_type1.eotf > HDMI_EOTF_BT_2100_HLG)
		return -EINVAL;

	if (!(sink->metadata_type & BIT(HDMI_STATIC_METADATA_TYPE1)) ||
	    !(sink->eotf & BIT(metadata->hdmi_metadata_type1.eotf)))
		return -EINVAL;

	ret = drm_hdmi_infoframe_set_hdr_metadata(&frame, conn_state);
	if (ret)
		return ret;

	ret = hdmi_drm_infoframe_pack_only(&frame, buffer, sizeof(buffer));
	if (ret < 0)
		return ret;

	sdp->sdp_header.HB1 = HDMI_INFOFRAME_TYPE_DRM;
	sdp->sdp_header.HB2 = 0x1d;
	sdp->sdp_header.HB3 = 0x13 << 2;
	sdp->db[0] = frame.version;
	sdp->db[1] = frame.length;
	/* DP carries the version and length, but no HDMI checksum. */
	memcpy(&sdp->db[2], &buffer[HDMI_INFOFRAME_HEADER_SIZE],
	       HDMI_DRM_INFOFRAME_SIZE);

	return 0;
}

bool msm_dp_utils_needs_modeset(struct drm_connector_state *old_state,
				struct drm_connector_state *new_state)
{
	return old_state->max_requested_bpc != new_state->max_requested_bpc ||
	       old_state->colorspace != new_state->colorspace ||
	       !drm_connector_atomic_hdr_metadata_equal(old_state, new_state);
}

bool msm_dp_utils_psr_needs_modeset(const struct drm_crtc_state *state)
{
	/* active_changed alone also describes a normal self-refresh exit. */
	return state->mode_changed || state->connectors_changed;
}
