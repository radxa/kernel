/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024, The Linux Foundation. All rights reserved.
 */

#include <linux/types.h>
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
	u8 num_byte = (data & 0xFF00) > 0 ? 8 : 2;

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
			    unsigned int lanes, bool yuv420)
{
	unsigned int bpp;

	bpc = min3(bpc ? bpc : 8, max_bpc, 10U);
	bpc = round_down(bpc, 2);
	if (bpc < 6)
		return -EINVAL;

	for (bpp = bpc * 3; bpp >= 18; bpp -= 6) {
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
	default:
		return -EINVAL;
	}

	ret = drm_dp_vsc_sdp_pack(&vsc, sdp);
	return ret < 0 ? ret : 0;
}

bool msm_dp_utils_psr_needs_modeset(const struct drm_crtc_state *state)
{
	/* active_changed alone also describes a normal self-refresh exit. */
	return state->mode_changed || state->connectors_changed;
}
