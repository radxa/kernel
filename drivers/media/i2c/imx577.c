// SPDX-License-Identifier: GPL-2.0
/*
 * imx577 camera driver
 *
 * Copyright (C) 2022 Rockchip Electronics Co., Ltd.
 *
 * Modified by Radxa Computer (Shenzhen) Co., Ltd.
 * Copyright (C) 2026 Radxa Computer (Shenzhen) Co., Ltd.
 *
 * V0.0X01.0X00 first version.
 * V0.0X01.0X01 add full size 30fps.
 * V0.0X01.0X02 fix gain and exposure setting.
 * V0.0X01.0X03
 *  1.support 10bit HDR DOL2.
 *  2.4056*3040 @ 25fps
 * V0.0X01.0X04 add dgain ctrl
 * V0.0X01.0X05 fix 4056*3040 HDRx2 30fps
 * V0.0X01.0X06 add two-lane RAW10 modes
 *
 */

// #define DEBUG
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/sysfs.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/compat.h>
#include <linux/rk-camera-module.h>
#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-mediabus.h>
#include <linux/of_graph.h>
#include <linux/pinctrl/consumer.h>
#include <linux/rk-preisp.h>

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x06)

#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN		V4L2_CID_GAIN
#endif

#define IMX577_XVCLK_FREQ			24000000
#define IMX577_PIXEL_RATE_MAX		840000000LL

/* Four-lane CSI pixel rates and timing limits. */
#define IMX577_4LANE_LINK_FREQ_1050MHZ	1050000000U
#define IMX577_4LANE_LINK_FREQ_498MHZ	498000000U
#define IMX577_4LANE_PIXEL_RATE_1050M_10BIT \
	(IMX577_4LANE_LINK_FREQ_1050MHZ * 2LL * 4LL / 10LL)
#define IMX577_4LANE_PIXEL_RATE_1050M_12BIT \
	(IMX577_4LANE_LINK_FREQ_1050MHZ * 2LL * 4LL / 12LL)
#define IMX577_4LANE_VTS_MAX		0xffff
#define IMX577_4LANE_BRL_FULL		3077
#define IMX577_4LANE_TEST_PATTERN_ENABLE	0x02
#define IMX577_4LANE_TEST_PATTERN_DISABLE	0x0

/* Two-lane VT timing is independent of the CSI payload pixel rate. */
#define IMX577_2LANE_LINK_FREQ		450000000U
#define IMX577_2LANE_VT_PIXEL_RATE	840000000U
#define IMX577_2LANE_VTS_MAX		0xffdc
#define IMX577_2LANE_HTS(width)		\
	((width) * 10ULL * IMX577_2LANE_VT_PIXEL_RATE / \
	 (IMX577_2LANE_LINK_FREQ * 4ULL) + 500)

/* Shared register definitions and control limits. */
#define CHIP_ID				0x0577
#define IMX577_REG_CHIP_ID		0x0016

#define IMX577_REG_CTRL_MODE		0x0100
#define IMX577_MODE_SW_STANDBY		0x0
#define IMX577_MODE_STREAMING		BIT(0)

#define IMX577_REG_EXPOSURE_H		0x0202
#define IMX577_REG_EXPOSURE_L		0x0203
#define	IMX577_EXPOSURE_MIN		8
#define	IMX577_EXPOSURE_STEP		1

#define IMX577_REG_GAIN_H		0x0204
#define IMX577_REG_GAIN_L		0x0205
#define IMX577_GAIN_MIN			0x10
#define IMX577_GAIN_MAX			0x1600
#define IMX577_GAIN_STEP		0x1
#define IMX577_GAIN_DEFAULT		0x20

#define IMX577_REG_DGAIN		0x3ff9
#define IMX577_DGAIN_MODE		1
#define IMX577_REG_DGAINGR_H		0x020e
#define IMX577_REG_DGAINGR_L		0x020f
#define IMX577_REG_DGAINR_H		0x0210
#define IMX577_REG_DGAINR_L		0x0211
#define IMX577_REG_DGAINB_H		0x0212
#define IMX577_REG_DGAINB_L		0x0213
#define IMX577_REG_DGAINGB_H		0x0214
#define IMX577_REG_DGAINGB_L		0x0215

/* Four-lane HDR exposure and gain registers. */
#define IMX577_LF_GAIN_REG_H		0x00f0
#define IMX577_LF_GAIN_REG_L		0x00f1
#define IMX577_SEF1_GAIN_REG_H		0x00f2
#define IMX577_SEF1_GAIN_REG_L		0x00f3
#define IMX577_SEF2_GAIN_REG_H		0x00f4
#define IMX577_SEF2_GAIN_REG_L		0x00f5

#define IMX577_LF_DGAIN_REG_H		0x00f6
#define IMX577_LF_DGAIN_REG_L		0x00f7
#define IMX577_SEF1_DGAIN_REG_H		0x00f8
#define IMX577_SEF1_DGAIN_REG_L		0x00f9
#define IMX577_SEF2_DGAIN_REG_H		0x00fa
#define IMX577_SEF2_DGAIN_REG_L		0x00fb

#define IMX577_LF_EXPO_REG_H		0x00ea
#define IMX577_LF_EXPO_REG_L		0x00eb
#define IMX577_SEF1_EXPO_REG_H		0x00ec
#define IMX577_SEF1_EXPO_REG_L		0x00ed
#define IMX577_SEF2_EXPO_REG_H		0x00ee
#define IMX577_SEF2_EXPO_REG_L		0x00ef

#define IMX577_RHS1_REG_H		0x00e6
#define IMX577_RHS1_REG_L		0x00e7
#define IMX577_RHS2_REG_H		0x00e8
#define IMX577_RHS2_REG_L		0x00e9

#define IMX577_REG_TEST_PATTERN		0x0600

#define IMX577_REG_VTS			0x0340
#define IMX577_REG_HTS			0x0342
#define IMX577_REG_IOP_PLL_MPY		0x030e
#define IMX577_REG_LINK_BIT_RATE		0x0820
#define IMX577_REG_CSI_FORMAT		0x0112
#define IMX577_REG_IOP_PIX_DIV		0x0309
#define IMX577_REG_ADBIT_MODE		0x3f0d
#define IMX577_REG_LONG_EXP_SHIFT		0x3100

#define IMX577_FETCH_EXP_H(VAL)		(((VAL) >> 8) & 0xFF)
#define IMX577_FETCH_EXP_L(VAL)		((VAL) & 0xFF)

#define IMX577_FETCH_AGAIN_H(VAL)	(((VAL) >> 8) & 0x03)
#define IMX577_FETCH_AGAIN_L(VAL)	((VAL) & 0xFF)

#define IMX577_FETCH_DGAIN_H(VAL)	(((VAL) >> 8) & 0x0F)
#define IMX577_FETCH_DGAIN_L(VAL)	((VAL) & 0xFF)

#define IMX577_FETCH_GAIN_H(VAL)	(((VAL) >> 8) & 0xFF)
#define IMX577_FETCH_GAIN_L(VAL)	((VAL) & 0xFF)

#define IMX577_FETCH_RHS1_H(VAL)	(((VAL) >> 8) & 0xFF)
#define IMX577_FETCH_RHS1_L(VAL)	((VAL) & 0xFF)

#define REG_NULL			0xFFFF

#define IMX577_REG_VALUE_08BIT		1
#define IMX577_REG_VALUE_16BIT		2
#define IMX577_REG_VALUE_24BIT		3
#define IMX577_REG_VALUE_32BIT		4

#define IMX577_GROUP_HOLD_REG		0x0104
#define IMX577_GROUP_HOLD_START		0x01
#define IMX577_GROUP_HOLD_END		0x00

#define CIT_MARGIN			22

#define OF_CAMERA_PINCTRL_STATE_DEFAULT	"rockchip,camera_default"
#define OF_CAMERA_PINCTRL_STATE_SLEEP	"rockchip,camera_sleep"
#define OF_CAMERA_HDR_MODE		"rockchip,camera-hdr-mode"

#define IMX577_NAME			"imx577"

static const char * const imx577_supply_names[] = {
	"avdd",		/* Analog power */
	"dovdd",	/* Digital I/O power */
	"dvdd",		/* Digital core power */
};

#define IMX577_NUM_SUPPLIES ARRAY_SIZE(imx577_supply_names)

struct regval {
	u16 addr;
	u16 val;
};

struct imx577_mode {
	u32 bus_fmt;
	u32 width;
	u32 height;
	struct v4l2_fract max_fps;
	u32 hts_def;
	u32 vts_def;
	u32 exp_def;
	u32 link_freq_idx;
	u32 bpp;
	const struct regval *reg_list;
	u32 hdr_mode;
	u32 vc[PAD_MAX];
};

struct imx577 {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*power_gpio;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*pwdn_gpio;
	struct regulator_bulk_data supplies[IMX577_NUM_SUPPLIES];

	struct pinctrl		*pinctrl;
	struct pinctrl_state	*pins_default;
	struct pinctrl_state	*pins_sleep;

	struct v4l2_subdev	subdev;
	struct media_pad	pad;
	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl	*exposure;
	struct v4l2_ctrl	*anal_gain;
	struct v4l2_ctrl	*digi_gain;
	struct v4l2_ctrl	*hblank;
	struct v4l2_ctrl	*vblank;
	struct v4l2_ctrl	*pixel_rate;
	struct v4l2_ctrl	*link_freq;
	struct v4l2_ctrl	*test_pattern;
	struct mutex		mutex;
	bool			streaming;
	bool			power_on;
	const struct imx577_mode *supported_modes;
	unsigned int		cfg_num;
	const s64		*link_freq_menu;
	unsigned int		link_freq_menu_count;
	const struct imx577_mode *cur_mode;
	bool			has_init_exp;
	struct preisp_hdrae_exp_s init_hdrae_exp;
	u32			cur_pixel_rate;
	u32			cur_link_freq;
	u32			module_index;
	u32			cur_vts;
	const char		*module_facing;
	const char		*module_name;
	const char		*len_name;
	struct v4l2_fwnode_endpoint bus_cfg;
	struct rkmodule_awb_cfg	awb_cfg;
	struct rkmodule_lsc_cfg	lsc_cfg;
};

#define to_imx577(sd) container_of(sd, struct imx577, subdev)

static bool imx577_is_2lane(struct imx577 *imx577)
{
	return imx577->bus_cfg.bus.mipi_csi2.num_data_lanes == 2;
}

/* Four-lane register tables and modes from the Rockchip driver. */
static __maybe_unused const struct regval imx577_4lane_common_regs[] = {
	{0x0136, 0x18},
	{0x0137, 0x00},
	{0x3C7E, 0x01},
	{0x3C7F, 0x02},
	{0x38A8, 0x1F},
	{0x38A9, 0xFF},
	{0x38AA, 0x1F},
	{0x38AB, 0xFF},
	{0x55D4, 0x00},
	{0x55D5, 0x00},
	{0x55D6, 0x07},
	{0x55D7, 0xFF},
	{0x55E8, 0x07},
	{0x55E9, 0xFF},
	{0x55EA, 0x00},
	{0x55EB, 0x00},
	{0x575C, 0x07},
	{0x575D, 0xFF},
	{0x575E, 0x00},
	{0x575F, 0x00},
	{0x5764, 0x00},
	{0x5765, 0x00},
	{0x5766, 0x07},
	{0x5767, 0xFF},
	{0x5974, 0x04},
	{0x5975, 0x01},
	{0x5F10, 0x09},
	{0x5F11, 0x92},
	{0x5F12, 0x32},
	{0x5F13, 0x72},
	{0x5F14, 0x16},
	{0x5F15, 0xBA},
	{0x5F17, 0x13},
	{0x5F18, 0x24},
	{0x5F19, 0x60},
	{0x5F1A, 0xE3},
	{0x5F1B, 0xAD},
	{0x5F1C, 0x74},
	{0x5F2D, 0x25},
	{0x5F5C, 0xD0},
	{0x6A22, 0x00},
	{0x6A23, 0x1D},
	{0x7BA8, 0x00},
	{0x7BA9, 0x00},
	{0x886B, 0x00},
	{0x9002, 0x0A},
	{0x9004, 0x1A},
	{0x9214, 0x93},
	{0x9215, 0x69},
	{0x9216, 0x93},
	{0x9217, 0x6B},
	{0x9218, 0x93},
	{0x9219, 0x6D},
	{0x921A, 0x57},
	{0x921B, 0x58},
	{0x921C, 0x57},
	{0x921D, 0x59},
	{0x921E, 0x57},
	{0x921F, 0x5A},
	{0x9220, 0x57},
	{0x9221, 0x5B},
	{0x9222, 0x93},
	{0x9223, 0x02},
	{0x9224, 0x93},
	{0x9225, 0x03},
	{0x9226, 0x93},
	{0x9227, 0x04},
	{0x9228, 0x93},
	{0x9229, 0x05},
	{0x922A, 0x98},
	{0x922B, 0x21},
	{0x922C, 0xB2},
	{0x922D, 0xDB},
	{0x922E, 0xB2},
	{0x922F, 0xDC},
	{0x9230, 0xB2},
	{0x9231, 0xDD},
	{0x9232, 0xB2},
	{0x9233, 0xE1},
	{0x9234, 0xB2},
	{0x9235, 0xE2},
	{0x9236, 0xB2},
	{0x9237, 0xE3},
	{0x9238, 0xB7},
	{0x9239, 0xB9},
	{0x923A, 0xB7},
	{0x923B, 0xBB},
	{0x923C, 0xB7},
	{0x923D, 0xBC},
	{0x923E, 0xB7},
	{0x923F, 0xC5},
	{0x9240, 0xB7},
	{0x9241, 0xC7},
	{0x9242, 0xB7},
	{0x9243, 0xC9},
	{0x9244, 0x98},
	{0x9245, 0x56},
	{0x9246, 0x98},
	{0x9247, 0x55},
	{0x9380, 0x00},
	{0x9381, 0x62},
	{0x9382, 0x00},
	{0x9383, 0x56},
	{0x9384, 0x00},
	{0x9385, 0x52},
	{0x9388, 0x00},
	{0x9389, 0x55},
	{0x938A, 0x00},
	{0x938B, 0x55},
	{0x938C, 0x00},
	{0x938D, 0x41},
	{REG_NULL, 0x00},
};

static __maybe_unused const struct regval imx577_4lane_linear_10bit_4056x3040_60fps_regs[] = {
	{0x0112, 0x0A},
	{0x0113, 0x0A},
	{0x0114, 0x03},
	{0x0342, 0x11},
	{0x0343, 0xA0},
	{0x0340, 0x0C},
	{0x0341, 0x1E},
	{0x3210, 0x00},
	{0x0344, 0x00},
	{0x0345, 0x00},
	{0x0346, 0x00},
	{0x0347, 0x00},
	{0x0348, 0x0F},
	{0x0349, 0xD7},
	{0x034A, 0x0B},
	{0x034B, 0xDF},
	{0x00E3, 0x00},
	{0x00E4, 0x00},
	{0x00E5, 0x01},
	{0x00FC, 0x0A},
	{0x00FD, 0x0A},
	{0x00FE, 0x0A},
	{0x00FF, 0x0A},
	{0xE013, 0x00},
	{0x0220, 0x00},
	{0x0221, 0x11},
	{0x0381, 0x01},
	{0x0383, 0x01},
	{0x0385, 0x01},
	{0x0387, 0x01},
	{0x0900, 0x00},
	{0x0901, 0x11},
	{0x0902, 0x00},
	{0x3140, 0x02},
	{0x3241, 0x11},
	{0x3250, 0x03},
	{0x3E10, 0x00},
	{0x3E11, 0x00},
	{0x3F0D, 0x00},
	{0x3F42, 0x00},
	{0x3F43, 0x00},
	{0x0401, 0x00},
	{0x0404, 0x00},
	{0x0405, 0x10},
	{0x0408, 0x00},
	{0x0409, 0x00},
	{0x040A, 0x00},
	{0x040B, 0x00},
	{0x040C, 0x0F},
	{0x040D, 0xD8},
	{0x040E, 0x0B},
	{0x040F, 0xE0},
	{0x034C, 0x0F},
	{0x034D, 0xD8},
	{0x034E, 0x0B},
	{0x034F, 0xE0},
	{0x0301, 0x05},
	{0x0303, 0x02},
	{0x0305, 0x04},
	{0x0306, 0x01},
	{0x0307, 0x5E},
	{0x0309, 0x0A},
	{0x030B, 0x01},
	{0x030D, 0x02},
	{0x030E, 0x01},
	{0x030F, 0x5E},
	{0x0310, 0x00},
	{0x0820, 0x20},
	{0x0821, 0xD0},
	{0x0822, 0x00},
	{0x0823, 0x00},
	{0x3E20, 0x01},
	{0x3E37, 0x00},
	{0x3F50, 0x00},
	{0x3F56, 0x00},
	{0x3F57, 0x82},
	{0x3C0A, 0x5A},
	{0x3C0B, 0x55},
	{0x3C0C, 0x28},
	{0x3C0D, 0x07},
	{0x3C0E, 0xFF},
	{0x3C0F, 0x00},
	{0x3C10, 0x00},
	{0x3C11, 0x02},
	{0x3C12, 0x00},
	{0x3C13, 0x03},
	{0x3C14, 0x00},
	{0x3C15, 0x00},
	{0x3C16, 0x0C},
	{0x3C17, 0x0C},
	{0x3C18, 0x0C},
	{0x3C19, 0x0A},
	{0x3C1A, 0x0A},
	{0x3C1B, 0x0A},
	{0x3C1C, 0x00},
	{0x3C1D, 0x00},
	{0x3C1E, 0x00},
	{0x3C1F, 0x00},
	{0x3C20, 0x00},
	{0x3C21, 0x00},
	{0x3C22, 0x3F},
	{0x3C23, 0x0A},
	{0x3E35, 0x01},
	{0x3F4A, 0x03},
	{0x3F4B, 0xBF},
	{0x3F26, 0x00},
	{0x0202, 0x0C},
	{0x0203, 0x08},
	{0x0204, 0x00},
	{0x0205, 0x00},
	{0x020E, 0x01},
	{0x020F, 0x00},
	{0x0210, 0x01},
	{0x0211, 0x00},
	{0x0212, 0x01},
	{0x0213, 0x00},
	{0x0214, 0x01},
	{0x0215, 0x00},
	{REG_NULL, 0x00},
};

static __maybe_unused const struct regval imx577_4lane_linear_10bit_4056x3040_30fps_regs[] = {
	{0x0112, 0x0A},
	{0x0113, 0x0A},
	{0x0114, 0x03},
	{0x0342, 0x23},
	{0x0343, 0x18},
	{0x0340, 0x0C},
	{0x0341, 0x2c},
	{0x3210, 0x00},
	{0x0344, 0x00},
	{0x0345, 0x00},
	{0x0346, 0x00},
	{0x0347, 0x00},
	{0x0348, 0x0F},
	{0x0349, 0xD7},
	{0x034A, 0x0B},
	{0x034B, 0xDF},
	{0x00E3, 0x00},
	{0x00E4, 0x00},
	{0x00E5, 0x01},
	{0x00FC, 0x0A},
	{0x00FD, 0x0A},
	{0x00FE, 0x0A},
	{0x00FF, 0x0A},
	{0x0220, 0x00},
	{0x0221, 0x11},
	{0x0381, 0x01},
	{0x0383, 0x01},
	{0x0385, 0x01},
	{0x0387, 0x01},
	{0x0900, 0x00},
	{0x0901, 0x11},
	{0x0902, 0x00},
	{0x3140, 0x02},
	{0x3241, 0x11},
	{0x3250, 0x03},
	{0x3E10, 0x00},
	{0x3E11, 0x00},
	{0x3F0D, 0x00},
	{0x3F42, 0x00},
	{0x3F43, 0x00},
	{0x0401, 0x00},
	{0x0404, 0x00},
	{0x0405, 0x10},
	{0x0408, 0x00},
	{0x0409, 0x00},
	{0x040A, 0x00},
	{0x040B, 0x00},
	{0x040C, 0x0F},
	{0x040D, 0xD8},
	{0x040E, 0x0B},
	{0x040F, 0xE0},
	{0x034C, 0x0F},
	{0x034D, 0xD8},
	{0x034E, 0x0B},
	{0x034F, 0xE0},
	{0x0301, 0x05},
	{0x0303, 0x02},
	{0x0305, 0x04},
	{0x0306, 0x01},
	{0x0307, 0x5E},
	{0x0309, 0x0A},
	{0x030B, 0x02},
	{0x030D, 0x02},
	{0x030E, 0x00},
	{0x030F, 0xA6},
	{0x0310, 0x01},
	{0x0820, 0x0F},
	{0x0821, 0x90},
	{0x0822, 0x00},
	{0x0823, 0x00},
	{0x3E20, 0x01},
	{0x3E37, 0x00},
	{0x3F50, 0x00},
	{0x3F56, 0x00},
	{0x3F57, 0x41},
	{0x3C0A, 0x5A},
	{0x3C0B, 0x55},
	{0x3C0C, 0x28},
	{0x3C0D, 0x07},
	{0x3C0E, 0xFF},
	{0x3C0F, 0x00},
	{0x3C10, 0x00},
	{0x3C11, 0x02},
	{0x3C12, 0x00},
	{0x3C13, 0x03},
	{0x3C14, 0x00},
	{0x3C15, 0x00},
	{0x3C16, 0x0C},
	{0x3C17, 0x0C},
	{0x3C18, 0x0C},
	{0x3C19, 0x0A},
	{0x3C1A, 0x0A},
	{0x3C1B, 0x0A},
	{0x3C1C, 0x00},
	{0x3C1D, 0x00},
	{0x3C1E, 0x00},
	{0x3C1F, 0x00},
	{0x3C20, 0x00},
	{0x3C21, 0x00},
	{0x3C22, 0x3F},
	{0x3C23, 0x0A},
	{0x3E35, 0x01},
	{0x3F4A, 0x03},
	{0x3F4B, 0xBF},
	{0x3F26, 0x00},
	{0x0202, 0x0C},
	{0x0203, 0x16},
	{0x0204, 0x00},
	{0x0205, 0x00},
	{0x020E, 0x01},
	{0x020F, 0x00},
	{0x0210, 0x01},
	{0x0211, 0x00},
	{0x0212, 0x01},
	{0x0213, 0x00},
	{0x0214, 0x01},
	{0x0215, 0x00},
	{REG_NULL, 0x00},
};

static __maybe_unused const struct regval imx577_4lane_hdr2_10bit_4056x3040_15fps_regs[] = {
	{0x0112, 0x0A},
	{0x0113, 0x0A},
	{0x0114, 0x03},
	{0x0342, 0x11},
	{0x0343, 0xA0},
	{0x0340, 0x18},
	{0x0341, 0x3D},
	{0x3210, 0x00},
	{0x0344, 0x00},
	{0x0345, 0x00},
	{0x0346, 0x00},
	{0x0347, 0x00},
	{0x0348, 0x0F},
	{0x0349, 0xD7},
	{0x034A, 0x0B},
	{0x034B, 0xDF},
	{0x00E3, 0x01},
	{0x00E4, 0x01},
	{0x00E5, 0x00},//vc:0 LI:1
	{0x00FC, 0x0A},
	{0x00FD, 0x0A},
	{0x00FE, 0x0A},
	{0x00FF, 0x0A},
	{0xE013, 0x01},//VC:1 LI:0
	{0x0220, 0x00},
	{0x0221, 0x11},
	{0x0381, 0x01},
	{0x0383, 0x01},
	{0x0385, 0x01},
	{0x0387, 0x01},
	{0x0900, 0x00},
	{0x0901, 0x11},
	{0x0902, 0x00},
	{0x3140, 0x02},
	{0x3241, 0x11},
	{0x3250, 0x03},
	{0x3E10, 0x01},//VC:1 LI:0
	{0x3E11, 0x02},//VC:2 LI:0
	{0x3F0D, 0x00},
	{0x3F42, 0x00},
	{0x3F43, 0x00},
	{0x0401, 0x00},
	{0x0404, 0x00},
	{0x0405, 0x10},
	{0x0408, 0x00},
	{0x0409, 0x00},
	{0x040A, 0x00},
	{0x040B, 0x00},
	{0x040C, 0x0F},
	{0x040D, 0xD8},
	{0x040E, 0x0B},
	{0x040F, 0xE0},
	{0x034C, 0x0F},
	{0x034D, 0xDC},
	{0x034E, 0x0B},
	{0x034F, 0xE0},
	{0x0301, 0x05},
	{0x0303, 0x02},
	{0x0305, 0x04},
	{0x0306, 0x01},
	{0x0307, 0x5E},
	{0x0309, 0x0A},
	{0x030B, 0x01},
	{0x030D, 0x02},
	{0x030E, 0x01},
	{0x030F, 0x5E},
	{0x0310, 0x00},
	{0x0820, 0x20},
	{0x0821, 0xD0},
	{0x0822, 0x00},
	{0x0823, 0x00},
	{0x3E20, 0x01},
	{0x3E37, 0x00},
	{0x3F50, 0x00},
	{0x3F56, 0x00},
	{0x3F57, 0x82},
	{0x3C0A, 0x5A},
	{0x3C0B, 0x55},
	{0x3C0C, 0x28},
	{0x3C0D, 0x07},
	{0x3C0E, 0xFF},
	{0x3C0F, 0x00},
	{0x3C10, 0x00},
	{0x3C11, 0x02},
	{0x3C12, 0x00},
	{0x3C13, 0x03},
	{0x3C14, 0x00},
	{0x3C15, 0x00},
	{0x3C16, 0x0C},
	{0x3C17, 0x0C},
	{0x3C18, 0x0C},
	{0x3C19, 0x0A},
	{0x3C1A, 0x0A},
	{0x3C1B, 0x0A},
	{0x3C1C, 0x00},
	{0x3C1D, 0x00},
	{0x3C1E, 0x00},
	{0x3C1F, 0x00},
	{0x3C20, 0x00},
	{0x3C21, 0x00},
	{0x3C22, 0x3F},
	{0x3C23, 0x0A},
	{0x3E35, 0x01},
	{0x3F4A, 0x03},
	{0x3F4B, 0xBF},
	{0x3F26, 0x00},
	{0x0202, 0x18},
	{0x0203, 0x27},
	{0x0204, 0x00},
	{0x0205, 0x00},
	{0x020E, 0x01},
	{0x020F, 0x00},
	{0x0210, 0x01},
	{0x0211, 0x00},
	{0x0212, 0x01},
	{0x0213, 0x00},
	{0x0214, 0x01},
	{0x0215, 0x00},
	{REG_NULL, 0x00},
};

static __maybe_unused const struct regval imx577_4lane_hdr2_10bit_4056x3040_30fps_regs[] = {
	{0x0112, 0x0A},
	{0x0113, 0x0A},
	{0x0114, 0x03},
	{0x0342, 0x11},
	{0x0343, 0xA0},
	{0x0340, 0x0C},
	{0x0341, 0x1E},
	{0x3210, 0x00},
	{0x0344, 0x00},
	{0x0345, 0x00},
	{0x0346, 0x00},
	{0x0347, 0x00},
	{0x0348, 0x0F},
	{0x0349, 0xD7},
	{0x034A, 0x0B},
	{0x034B, 0xDF},
	{0x00E3, 0x01},
	{0x00E4, 0x01},
	{0x00E5, 0x00},//vc:0
	{0x00FC, 0x0A},
	{0x00FD, 0x0A},
	{0x00FE, 0x0A},
	{0x00FF, 0x0A},
	{0xE013, 0x01},//vc:1
	{0x0220, 0x00},
	{0x0221, 0x11},
	{0x0381, 0x01},
	{0x0383, 0x01},
	{0x0385, 0x01},
	{0x0387, 0x01},
	{0x0900, 0x00},
	{0x0901, 0x11},
	{0x0902, 0x00},
	{0x3140, 0x02},
	{0x3241, 0x11},
	{0x3250, 0x03},
	{0x3E10, 0x01},//vc:1  li:0
	{0x3E11, 0x02},//vc:2  li:0
	{0x3F0D, 0x00},
	{0x3F42, 0x00},
	{0x3F43, 0x00},
	{0x0401, 0x00},
	{0x0404, 0x00},
	{0x0405, 0x10},
	{0x0408, 0x00},
	{0x0409, 0x00},
	{0x040A, 0x00},
	{0x040B, 0x00},
	{0x040C, 0x0F},
	{0x040D, 0xD8},
	{0x040E, 0x0B},
	{0x040F, 0xE0},
	{0x034C, 0x0F},
	{0x034D, 0xDC},
	{0x034E, 0x0B},
	{0x034F, 0xE0},
	{0x0301, 0x05},
	{0x0303, 0x02},
	{0x0305, 0x04},
	{0x0306, 0x01},
	{0x0307, 0x5E},
	{0x0309, 0x0A},
	{0x030B, 0x01},
	{0x030D, 0x02},
	{0x030E, 0x01},
	{0x030F, 0x5E},
	{0x0310, 0x00},
	{0x0820, 0x20},
	{0x0821, 0xD0},
	{0x0822, 0x00},
	{0x0823, 0x00},
	{0x3E20, 0x01},
	{0x3E37, 0x00},
	{0x3F50, 0x00},
	{0x3F56, 0x00},
	{0x3F57, 0x82},
	{0x3C0A, 0x5A},
	{0x3C0B, 0x55},
	{0x3C0C, 0x28},
	{0x3C0D, 0x07},
	{0x3C0E, 0xFF},
	{0x3C0F, 0x00},
	{0x3C10, 0x00},
	{0x3C11, 0x02},
	{0x3C12, 0x00},
	{0x3C13, 0x03},
	{0x3C14, 0x00},
	{0x3C15, 0x00},
	{0x3C16, 0x0C},
	{0x3C17, 0x0C},
	{0x3C18, 0x0C},
	{0x3C19, 0x0A},
	{0x3C1A, 0x0A},
	{0x3C1B, 0x0A},
	{0x3C1C, 0x00},
	{0x3C1D, 0x00},
	{0x3C1E, 0x00},
	{0x3C1F, 0x00},
	{0x3C20, 0x00},
	{0x3C21, 0x00},
	{0x3C22, 0x3F},
	{0x3C23, 0x0A},
	{0x3E35, 0x01},
	{0x3F4A, 0x03},
	{0x3F4B, 0xBF},
	{0x3F26, 0x00},
	{0x0202, 0x0C},
	{0x0203, 0x08},
	{0x0204, 0x00},
	{0x0205, 0x00},
	{0x020E, 0x01},
	{0x020F, 0x00},
	{0x0210, 0x01},
	{0x0211, 0x00},
	{0x0212, 0x01},
	{0x0213, 0x00},
	{0x0214, 0x01},
	{0x0215, 0x00},
	{REG_NULL, 0x00},
};

static __maybe_unused const struct regval imx577_4lane_linear_12bit_4056x3040_40fps_regs[] = {
	{0x0112, 0x0C},
	{0x0113, 0x0C},
	{0x0114, 0x03},
	{0x0342, 0x18},
	{0x0343, 0x50},
	{0x0340, 0x0D},
	{0x0341, 0x2E},
	{0x3210, 0x00},
	{0x0344, 0x00},
	{0x0345, 0x00},
	{0x0346, 0x00},
	{0x0347, 0x00},
	{0x0348, 0x0F},
	{0x0349, 0xD7},
	{0x034A, 0x0B},
	{0x034B, 0xDF},
	{0x00E3, 0x00},
	{0x00E4, 0x00},
	{0x00E5, 0x01},
	{0x00FC, 0x0A},
	{0x00FD, 0x0A},
	{0x00FE, 0x0A},
	{0x00FF, 0x0A},
	{0xE013, 0x00},
	{0x0220, 0x00},
	{0x0221, 0x11},
	{0x0381, 0x01},
	{0x0383, 0x01},
	{0x0385, 0x01},
	{0x0387, 0x01},
	{0x0900, 0x00},
	{0x0901, 0x11},
	{0x0902, 0x00},
	{0x3140, 0x02},
	{0x3241, 0x11},
	{0x3250, 0x03},
	{0x3E10, 0x00},
	{0x3E11, 0x00},
	{0x3F0D, 0x01},
	{0x3F42, 0x00},
	{0x3F43, 0x00},
	{0x0401, 0x00},
	{0x0404, 0x00},
	{0x0405, 0x10},
	{0x0408, 0x00},
	{0x0409, 0x00},
	{0x040A, 0x00},
	{0x040B, 0x00},
	{0x040C, 0x0F},
	{0x040D, 0xD8},
	{0x040E, 0x0B},
	{0x040F, 0xE0},
	{0x034C, 0x0F},
	{0x034D, 0xD8},
	{0x034E, 0x0B},
	{0x034F, 0xE0},
	{0x0301, 0x05},
	{0x0303, 0x02},
	{0x0305, 0x04},
	{0x0306, 0x01},
	{0x0307, 0x5E},
	{0x0309, 0x0C},
	{0x030B, 0x01},
	{0x030D, 0x02},
	{0x030E, 0x01},
	{0x030F, 0x5E},
	{0x0310, 0x00},
	{0x0820, 0x20},
	{0x0821, 0xD0},
	{0x0822, 0x00},
	{0x0823, 0x00},
	{0x3E20, 0x01},
	{0x3E37, 0x00},
	{0x3F50, 0x00},
	{0x3F56, 0x00},
	{0x3F57, 0xB2},
	{0x3C0A, 0x5A},
	{0x3C0B, 0x55},
	{0x3C0C, 0x28},
	{0x3C0D, 0x07},
	{0x3C0E, 0xFF},
	{0x3C0F, 0x00},
	{0x3C10, 0x00},
	{0x3C11, 0x02},
	{0x3C12, 0x00},
	{0x3C13, 0x03},
	{0x3C14, 0x00},
	{0x3C15, 0x00},
	{0x3C16, 0x0C},
	{0x3C17, 0x0C},
	{0x3C18, 0x0C},
	{0x3C19, 0x0A},
	{0x3C1A, 0x0A},
	{0x3C1B, 0x0A},
	{0x3C1C, 0x00},
	{0x3C1D, 0x00},
	{0x3C1E, 0x00},
	{0x3C1F, 0x00},
	{0x3C20, 0x00},
	{0x3C21, 0x00},
	{0x3C22, 0x3F},
	{0x3C23, 0x0A},
	{0x3E35, 0x01},
	{0x3F4A, 0x03},
	{0x3F4B, 0x85},
	{0x3F26, 0x00},
	{0x0202, 0x0D},
	{0x0203, 0x18},
	{0x0204, 0x00},
	{0x0205, 0x00},
	{0x020E, 0x01},
	{0x020F, 0x00},
	{0x0210, 0x01},
	{0x0211, 0x00},
	{0x0212, 0x01},
	{0x0213, 0x00},
	{0x0214, 0x01},
	{0x0215, 0x00},
	{REG_NULL, 0x00},
};

static const struct imx577_mode imx577_4lane_modes[] = {
	{
		.width = 4056,
		.height = 3040,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x0c10,
		.hts_def = 0x2318,
		.vts_def = 0x0c2c,
		.bpp = 10,
		.bus_fmt = MEDIA_BUS_FMT_SRGGB10_1X10,
		.reg_list = imx577_4lane_linear_10bit_4056x3040_30fps_regs,
		.hdr_mode = NO_HDR,
		.link_freq_idx = 1,
		.vc[PAD0] = 0,
	},
	{
		.width = 4056,
		.height = 3040,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x0c08,
		.hts_def = 0x11a0,
		.vts_def = 0x0c1e,
		.bpp = 10,
		.bus_fmt = MEDIA_BUS_FMT_SRGGB10_1X10,
		.reg_list = imx577_4lane_hdr2_10bit_4056x3040_30fps_regs,
		.link_freq_idx = 0,
		.hdr_mode = HDR_X2,
		.vc[PAD0] = 1,
		.vc[PAD1] = 0,//L->csi wr0
		.vc[PAD2] = 1,
		.vc[PAD3] = 1,//M->csi wr2
	},
	{
		.width = 4056,
		.height = 3040,
		.max_fps = {
			.numerator = 10000,
			.denominator = 600000,
		},
		.exp_def = 0x0c08,
		.hts_def = 0x11a0,
		.vts_def = 0x0c1e,
		.bpp = 10,
		.bus_fmt = MEDIA_BUS_FMT_SRGGB10_1X10,
		.reg_list = imx577_4lane_linear_10bit_4056x3040_60fps_regs,
		.hdr_mode = NO_HDR,
		.link_freq_idx = 0,
		.vc[PAD0] = 0,
	},
	{
		.width = 4056,
		.height = 3040,
		.max_fps = {
			.numerator = 10000,
			.denominator = 400000,
		},
		.exp_def = 0x0c10,
		.hts_def = 0x11a0,
		.vts_def = 0x0d2e,
		.bpp = 12,
		.bus_fmt = MEDIA_BUS_FMT_SRGGB12_1X12,
		.reg_list = imx577_4lane_linear_12bit_4056x3040_40fps_regs,
		.hdr_mode = NO_HDR,
		.link_freq_idx = 0,
		.vc[PAD0] = 0,
	},
};

static const s64 imx577_4lane_link_freq_items[] = {
	IMX577_4LANE_LINK_FREQ_1050MHZ,
	IMX577_4LANE_LINK_FREQ_498MHZ,
};

static const char * const imx577_4lane_test_pattern_menu[] = {
	"Disabled",
	"Vertical Color Bar Type 1",
	"Vertical Color Bar Type 2",
	"Vertical Color Bar Type 3"
};

/*
 * Two-lane tables from raspberrypi/linux, drivers/media/i2c/imx477.c
 * (commit 258d8575739954c0fd7c2289e845da25b3e288b5, GPL-2.0).
 * Copyright (C) 2020 Raspberry Pi (Trading) Ltd
 *
 * Matches INNO-MAKER/CAM-IMX577 tables at f080af7540e2.
 */
static const struct regval imx577_2lane_common_regs[] = {
	{0x0136, 0x18},
	{0x0137, 0x00},
	{0x0138, 0x01},
	{0xe000, 0x00},
	{0xe07a, 0x01},
	{0x0808, 0x02},
	{0x4ae9, 0x18},
	{0x4aea, 0x08},
	{0xf61c, 0x04},
	{0xf61e, 0x04},
	{0x4ae9, 0x21},
	{0x4aea, 0x80},
	{0x38a8, 0x1f},
	{0x38a9, 0xff},
	{0x38aa, 0x1f},
	{0x38ab, 0xff},
	{0x55d4, 0x00},
	{0x55d5, 0x00},
	{0x55d6, 0x07},
	{0x55d7, 0xff},
	{0x55e8, 0x07},
	{0x55e9, 0xff},
	{0x55ea, 0x00},
	{0x55eb, 0x00},
	{0x574c, 0x07},
	{0x574d, 0xff},
	{0x574e, 0x00},
	{0x574f, 0x00},
	{0x5754, 0x00},
	{0x5755, 0x00},
	{0x5756, 0x07},
	{0x5757, 0xff},
	{0x5973, 0x04},
	{0x5974, 0x01},
	{0x5d13, 0xc3},
	{0x5d14, 0x58},
	{0x5d15, 0xa3},
	{0x5d16, 0x1d},
	{0x5d17, 0x65},
	{0x5d18, 0x8c},
	{0x5d1a, 0x06},
	{0x5d1b, 0xa9},
	{0x5d1c, 0x45},
	{0x5d1d, 0x3a},
	{0x5d1e, 0xab},
	{0x5d1f, 0x15},
	{0x5d21, 0x0e},
	{0x5d22, 0x52},
	{0x5d23, 0xaa},
	{0x5d24, 0x7d},
	{0x5d25, 0x57},
	{0x5d26, 0xa8},
	{0x5d37, 0x5a},
	{0x5d38, 0x5a},
	{0x5d77, 0x7f},
	{0x7b75, 0x0e},
	{0x7b76, 0x0b},
	{0x7b77, 0x08},
	{0x7b78, 0x0a},
	{0x7b79, 0x47},
	{0x7b7c, 0x00},
	{0x7b7d, 0x00},
	{0x8d1f, 0x00},
	{0x8d27, 0x00},
	{0x9004, 0x03},
	{0x9200, 0x50},
	{0x9201, 0x6c},
	{0x9202, 0x71},
	{0x9203, 0x00},
	{0x9204, 0x71},
	{0x9205, 0x01},
	{0x9371, 0x6a},
	{0x9373, 0x6a},
	{0x9375, 0x64},
	{0x991a, 0x00},
	{0x996b, 0x8c},
	{0x996c, 0x64},
	{0x996d, 0x50},
	{0x9a4c, 0x0d},
	{0x9a4d, 0x0d},
	{0xa001, 0x0a},
	{0xa003, 0x0a},
	{0xa005, 0x0a},
	{0xa006, 0x01},
	{0xa007, 0xc0},
	{0xa009, 0xc0},
	{0x3d8a, 0x01},
	{0x4421, 0x04},
	{0x7b3b, 0x01},
	{0x7b4c, 0x00},
	{0x9905, 0x00},
	{0x9907, 0x00},
	{0x9909, 0x00},
	{0x990b, 0x00},
	{0x9944, 0x3c},
	{0x9947, 0x3c},
	{0x994a, 0x8c},
	{0x994b, 0x50},
	{0x994c, 0x1b},
	{0x994d, 0x8c},
	{0x994e, 0x50},
	{0x994f, 0x1b},
	{0x9950, 0x8c},
	{0x9951, 0x1b},
	{0x9952, 0x0a},
	{0x9953, 0x8c},
	{0x9954, 0x1b},
	{0x9955, 0x0a},
	{0x9a13, 0x04},
	{0x9a14, 0x04},
	{0x9a19, 0x00},
	{0x9a1c, 0x04},
	{0x9a1d, 0x04},
	{0x9a26, 0x05},
	{0x9a27, 0x05},
	{0x9a2c, 0x01},
	{0x9a2d, 0x03},
	{0x9a2f, 0x05},
	{0x9a30, 0x05},
	{0x9a41, 0x00},
	{0x9a46, 0x00},
	{0x9a47, 0x00},
	{0x9c17, 0x35},
	{0x9c1d, 0x31},
	{0x9c29, 0x50},
	{0x9c3b, 0x2f},
	{0x9c41, 0x6b},
	{0x9c47, 0x2d},
	{0x9c4d, 0x40},
	{0x9c6b, 0x00},
	{0x9c71, 0xc8},
	{0x9c73, 0x32},
	{0x9c75, 0x04},
	{0x9c7d, 0x2d},
	{0x9c83, 0x40},
	{0x9c94, 0x3f},
	{0x9c95, 0x3f},
	{0x9c96, 0x3f},
	{0x9c97, 0x00},
	{0x9c98, 0x00},
	{0x9c99, 0x00},
	{0x9c9a, 0x3f},
	{0x9c9b, 0x3f},
	{0x9c9c, 0x3f},
	{0x9ca0, 0x0f},
	{0x9ca1, 0x0f},
	{0x9ca2, 0x0f},
	{0x9ca3, 0x00},
	{0x9ca4, 0x00},
	{0x9ca5, 0x00},
	{0x9ca6, 0x1e},
	{0x9ca7, 0x1e},
	{0x9ca8, 0x1e},
	{0x9ca9, 0x00},
	{0x9caa, 0x00},
	{0x9cab, 0x00},
	{0x9cac, 0x09},
	{0x9cad, 0x09},
	{0x9cae, 0x09},
	{0x9cbd, 0x50},
	{0x9cbf, 0x50},
	{0x9cc1, 0x50},
	{0x9cc3, 0x40},
	{0x9cc5, 0x40},
	{0x9cc7, 0x40},
	{0x9cc9, 0x0a},
	{0x9ccb, 0x0a},
	{0x9ccd, 0x0a},
	{0x9d17, 0x35},
	{0x9d1d, 0x31},
	{0x9d29, 0x50},
	{0x9d3b, 0x2f},
	{0x9d41, 0x6b},
	{0x9d47, 0x42},
	{0x9d4d, 0x5a},
	{0x9d6b, 0x00},
	{0x9d71, 0xc8},
	{0x9d73, 0x32},
	{0x9d75, 0x04},
	{0x9d7d, 0x42},
	{0x9d83, 0x5a},
	{0x9d94, 0x3f},
	{0x9d95, 0x3f},
	{0x9d96, 0x3f},
	{0x9d97, 0x00},
	{0x9d98, 0x00},
	{0x9d99, 0x00},
	{0x9d9a, 0x3f},
	{0x9d9b, 0x3f},
	{0x9d9c, 0x3f},
	{0x9d9d, 0x1f},
	{0x9d9e, 0x1f},
	{0x9d9f, 0x1f},
	{0x9da0, 0x0f},
	{0x9da1, 0x0f},
	{0x9da2, 0x0f},
	{0x9da3, 0x00},
	{0x9da4, 0x00},
	{0x9da5, 0x00},
	{0x9da6, 0x1e},
	{0x9da7, 0x1e},
	{0x9da8, 0x1e},
	{0x9da9, 0x00},
	{0x9daa, 0x00},
	{0x9dab, 0x00},
	{0x9dac, 0x09},
	{0x9dad, 0x09},
	{0x9dae, 0x09},
	{0x9dc9, 0x0a},
	{0x9dcb, 0x0a},
	{0x9dcd, 0x0a},
	{0x9e17, 0x35},
	{0x9e1d, 0x31},
	{0x9e29, 0x50},
	{0x9e3b, 0x2f},
	{0x9e41, 0x6b},
	{0x9e47, 0x2d},
	{0x9e4d, 0x40},
	{0x9e6b, 0x00},
	{0x9e71, 0xc8},
	{0x9e73, 0x32},
	{0x9e75, 0x04},
	{0x9e94, 0x0f},
	{0x9e95, 0x0f},
	{0x9e96, 0x0f},
	{0x9e97, 0x00},
	{0x9e98, 0x00},
	{0x9e99, 0x00},
	{0x9ea0, 0x0f},
	{0x9ea1, 0x0f},
	{0x9ea2, 0x0f},
	{0x9ea3, 0x00},
	{0x9ea4, 0x00},
	{0x9ea5, 0x00},
	{0x9ea6, 0x3f},
	{0x9ea7, 0x3f},
	{0x9ea8, 0x3f},
	{0x9ea9, 0x00},
	{0x9eaa, 0x00},
	{0x9eab, 0x00},
	{0x9eac, 0x09},
	{0x9ead, 0x09},
	{0x9eae, 0x09},
	{0x9ec9, 0x0a},
	{0x9ecb, 0x0a},
	{0x9ecd, 0x0a},
	{0x9f17, 0x35},
	{0x9f1d, 0x31},
	{0x9f29, 0x50},
	{0x9f3b, 0x2f},
	{0x9f41, 0x6b},
	{0x9f47, 0x42},
	{0x9f4d, 0x5a},
	{0x9f6b, 0x00},
	{0x9f71, 0xc8},
	{0x9f73, 0x32},
	{0x9f75, 0x04},
	{0x9f94, 0x0f},
	{0x9f95, 0x0f},
	{0x9f96, 0x0f},
	{0x9f97, 0x00},
	{0x9f98, 0x00},
	{0x9f99, 0x00},
	{0x9f9a, 0x2f},
	{0x9f9b, 0x2f},
	{0x9f9c, 0x2f},
	{0x9f9d, 0x00},
	{0x9f9e, 0x00},
	{0x9f9f, 0x00},
	{0x9fa0, 0x0f},
	{0x9fa1, 0x0f},
	{0x9fa2, 0x0f},
	{0x9fa3, 0x00},
	{0x9fa4, 0x00},
	{0x9fa5, 0x00},
	{0x9fa6, 0x1e},
	{0x9fa7, 0x1e},
	{0x9fa8, 0x1e},
	{0x9fa9, 0x00},
	{0x9faa, 0x00},
	{0x9fab, 0x00},
	{0x9fac, 0x09},
	{0x9fad, 0x09},
	{0x9fae, 0x09},
	{0x9fc9, 0x0a},
	{0x9fcb, 0x0a},
	{0x9fcd, 0x0a},
	{0xa14b, 0xff},
	{0xa151, 0x0c},
	{0xa153, 0x50},
	{0xa155, 0x02},
	{0xa157, 0x00},
	{0xa1ad, 0xff},
	{0xa1b3, 0x0c},
	{0xa1b5, 0x50},
	{0xa1b9, 0x00},
	{0xa24b, 0xff},
	{0xa257, 0x00},
	{0xa2ad, 0xff},
	{0xa2b9, 0x00},
	{0xb21f, 0x04},
	{0xb35c, 0x00},
	{0xb35e, 0x08},
	{0x0114, 0x01},
	{0x0350, 0x00},
	{0xbcf1, 0x02},
	{0x3ff9, 0x01},
	{0x0220, 0x00},
	{0x0221, 0x11},
	{0x0381, 0x01},
	{0x0383, 0x01},
	{0x0385, 0x01},
	{0x0387, 0x01},
	{0x0902, 0x02},
	{0x3140, 0x02},
	{0x3c00, 0x00},
	{0x9e9a, 0x2f},
	{0x9e9b, 0x2f},
	{0x9e9c, 0x2f},
	{0x9e9d, 0x00},
	{0x9e9e, 0x00},
	{0x9e9f, 0x00},
	{0x0301, 0x05},
	{0x0303, 0x02},
	{0x030b, 0x02},
	{0x030d, 0x02},
	{0x0310, 0x01},
	{0x080a, 0x00},
	{0x080b, 0x7f},
	{0x080c, 0x00},
	{0x080d, 0x4f},
	{0x080e, 0x00},
	{0x080f, 0x77},
	{0x0810, 0x00},
	{0x0811, 0x5f},
	{0x0812, 0x00},
	{0x0813, 0x57},
	{0x0814, 0x00},
	{0x0815, 0x4f},
	{0x0816, 0x01},
	{0x0817, 0x27},
	{0x0818, 0x00},
	{0x0819, 0x3f},
	{0x3e20, 0x01},
	{0x3e37, 0x00},
	{0x3f50, 0x00},
	{REG_NULL, 0x00},
};

static const struct regval imx577_2lane_4056x3040_regs[] = {
	{0x0344, 0x00},
	{0x0345, 0x00},
	{0x0346, 0x00},
	{0x0347, 0x00},
	{0x0348, 0x0f},
	{0x0349, 0xd7},
	{0x034a, 0x0b},
	{0x034b, 0xdf},
	{0x00e3, 0x00},
	{0x00e4, 0x00},
	{0x00fc, 0x0a},
	{0x00fd, 0x0a},
	{0x00fe, 0x0a},
	{0x00ff, 0x0a},
	{0x0900, 0x00},
	{0x0901, 0x11},
	{0x3c01, 0x03},
	{0x3c02, 0xa2},
	{0x5748, 0x07},
	{0x5749, 0xff},
	{0x574a, 0x00},
	{0x574b, 0x00},
	{0x7b75, 0x0a},
	{0x7b76, 0x0c},
	{0x7b77, 0x07},
	{0x7b78, 0x06},
	{0x7b79, 0x3c},
	{0x7b53, 0x01},
	{0x9369, 0x5a},
	{0x936b, 0x55},
	{0x936d, 0x28},
	{0x9304, 0x00},
	{0x9305, 0x00},
	{0xa2a9, 0x60},
	{0xa2b7, 0x00},
	{0x0401, 0x00},
	{0x0404, 0x00},
	{0x0405, 0x10},
	{0x0408, 0x00},
	{0x0409, 0x00},
	{0x040a, 0x00},
	{0x040b, 0x00},
	{0x040c, 0x0f},
	{0x040d, 0xd8},
	{0x040e, 0x0b},
	{0x040f, 0xe0},
	{0x034c, 0x0f},
	{0x034d, 0xd8},
	{0x034e, 0x0b},
	{0x034f, 0xe0},
	{0x0305, 0x04},
	{0x0306, 0x01},
	{0x0307, 0x5e},
	{0xe04c, 0x00},
	{0xe04d, 0x7f},
	{0xe04e, 0x00},
	{0xe04f, 0x1f},
	{0x3f56, 0x02},
	{0x3f57, 0xae},
	{REG_NULL, 0x00},
};

static const struct regval imx577_2lane_4056x2160_regs[] = {
	{0x0344, 0x00},
	{0x0345, 0x00},
	{0x0346, 0x01},
	{0x0347, 0xb8},
	{0x0348, 0x0f},
	{0x0349, 0xd7},
	{0x034a, 0x0a},
	{0x034b, 0x27},
	{0x00e3, 0x00},
	{0x00e4, 0x00},
	{0x00fc, 0x0a},
	{0x00fd, 0x0a},
	{0x00fe, 0x0a},
	{0x00ff, 0x0a},
	{0x0900, 0x00},
	{0x0901, 0x11},
	{0x3c01, 0x03},
	{0x3c02, 0xa2},
	{0x5748, 0x07},
	{0x5749, 0xff},
	{0x574a, 0x00},
	{0x574b, 0x00},
	{0x7b75, 0x0a},
	{0x7b76, 0x0c},
	{0x7b77, 0x07},
	{0x7b78, 0x06},
	{0x7b79, 0x3c},
	{0x7b53, 0x01},
	{0x9369, 0x5a},
	{0x936b, 0x55},
	{0x936d, 0x28},
	{0x9304, 0x00},
	{0x9305, 0x00},
	{0xa2a9, 0x60},
	{0xa2b7, 0x00},
	{0x0401, 0x00},
	{0x0404, 0x00},
	{0x0405, 0x10},
	{0x0408, 0x00},
	{0x0409, 0x00},
	{0x040a, 0x00},
	{0x040b, 0x00},
	{0x040c, 0x0f},
	{0x040d, 0xd8},
	{0x040e, 0x08},
	{0x040f, 0x70},
	{0x034c, 0x0f},
	{0x034d, 0xd8},
	{0x034e, 0x08},
	{0x034f, 0x70},
	{0x0305, 0x04},
	{0x0306, 0x01},
	{0x0307, 0x5e},
	{0xe04c, 0x00},
	{0xe04d, 0x7f},
	{0xe04e, 0x00},
	{0xe04f, 0x1f},
	{0x3f56, 0x02},
	{0x3f57, 0xae},
	{REG_NULL, 0x00},
};

static const struct regval imx577_2lane_2028x1520_regs[] = {
	{0x0344, 0x00},
	{0x0345, 0x00},
	{0x0346, 0x00},
	{0x0347, 0x00},
	{0x0348, 0x0f},
	{0x0349, 0xd7},
	{0x034a, 0x0b},
	{0x034b, 0xdf},
	{0x0900, 0x01},
	{0x0901, 0x22},
	{0x3c01, 0x03},
	{0x3c02, 0xa2},
	{0x5748, 0x07},
	{0x5749, 0xff},
	{0x574a, 0x00},
	{0x574b, 0x00},
	{0x7b53, 0x01},
	{0x9369, 0x73},
	{0x936b, 0x64},
	{0x936d, 0x5f},
	{0x9304, 0x00},
	{0x9305, 0x00},
	{0xa2a9, 0x60},
	{0xa2b7, 0x00},
	{0x0401, 0x00},
	{0x0404, 0x00},
	{0x0405, 0x20},
	{0x0408, 0x00},
	{0x0409, 0x00},
	{0x040a, 0x00},
	{0x040b, 0x00},
	{0x040c, 0x0f},
	{0x040d, 0xd8},
	{0x040e, 0x0b},
	{0x040f, 0xe0},
	{0x034c, 0x07},
	{0x034d, 0xec},
	{0x034e, 0x05},
	{0x034f, 0xf0},
	{0x0305, 0x04},
	{0x0306, 0x01},
	{0x0307, 0x5e},
	{0xe04c, 0x00},
	{0xe04d, 0x7f},
	{0xe04e, 0x00},
	{0xe04f, 0x1f},
	{0x3f56, 0x01},
	{0x3f57, 0x6c},
	{REG_NULL, 0x00},
};

static const struct regval imx577_2lane_2028x1080_regs[] = {
	{0x0344, 0x00},
	{0x0345, 0x00},
	{0x0346, 0x01},
	{0x0347, 0xb8},
	{0x0348, 0x0f},
	{0x0349, 0xd7},
	{0x034a, 0x0a},
	{0x034b, 0x27},
	{0x0900, 0x01},
	{0x0901, 0x22},
	{0x3c01, 0x03},
	{0x3c02, 0xa2},
	{0x5748, 0x07},
	{0x5749, 0xff},
	{0x574a, 0x00},
	{0x574b, 0x00},
	{0x7b53, 0x01},
	{0x9369, 0x73},
	{0x936b, 0x64},
	{0x936d, 0x5f},
	{0x9304, 0x00},
	{0x9305, 0x00},
	{0xa2a9, 0x60},
	{0xa2b7, 0x00},
	{0x0401, 0x00},
	{0x0404, 0x00},
	{0x0405, 0x20},
	{0x0408, 0x00},
	{0x0409, 0x00},
	{0x040a, 0x00},
	{0x040b, 0x00},
	{0x040c, 0x0f},
	{0x040d, 0xd8},
	{0x040e, 0x04},
	{0x040f, 0x38},
	{0x034c, 0x07},
	{0x034d, 0xec},
	{0x034e, 0x04},
	{0x034f, 0x38},
	{0x0305, 0x04},
	{0x0306, 0x01},
	{0x0307, 0x5e},
	{0xe04c, 0x00},
	{0xe04d, 0x7f},
	{0xe04e, 0x00},
	{0xe04f, 0x1f},
	{0x3f56, 0x01},
	{0x3f57, 0x6c},
	{REG_NULL, 0x00},
};

static const struct regval imx577_2lane_1332x990_regs[] = {
	{0x420b, 0x01},
	{0x990c, 0x00},
	{0x990d, 0x08},
	{0x9956, 0x8c},
	{0x9957, 0x64},
	{0x9958, 0x50},
	{0x9a48, 0x06},
	{0x9a49, 0x06},
	{0x9a4a, 0x06},
	{0x9a4b, 0x06},
	{0x9a4c, 0x06},
	{0x9a4d, 0x06},
	{0x0114, 0x01},
	{0x0340, 0x04},
	{0x0341, 0x1a},
	{0x0344, 0x00},
	{0x0345, 0x00},
	{0x0346, 0x02},
	{0x0347, 0x10},
	{0x0348, 0x0f},
	{0x0349, 0xd7},
	{0x034a, 0x09},
	{0x034b, 0xcf},
	{0x00e3, 0x00},
	{0x00e4, 0x00},
	{0x00fc, 0x0a},
	{0x00fd, 0x0a},
	{0x00fe, 0x0a},
	{0x00ff, 0x0a},
	{0xe013, 0x00},
	{0x0220, 0x00},
	{0x0221, 0x11},
	{0x0381, 0x01},
	{0x0383, 0x01},
	{0x0385, 0x01},
	{0x0387, 0x01},
	{0x0900, 0x01},
	{0x0901, 0x22},
	{0x0902, 0x02},
	{0x3140, 0x02},
	{0x3c00, 0x00},
	{0x3c01, 0x01},
	{0x3c02, 0x9c},
	{0x5748, 0x00},
	{0x5749, 0x00},
	{0x574a, 0x00},
	{0x574b, 0xa4},
	{0x7b75, 0x0e},
	{0x7b76, 0x09},
	{0x7b77, 0x08},
	{0x7b78, 0x06},
	{0x7b79, 0x34},
	{0x7b53, 0x00},
	{0x9369, 0x73},
	{0x936b, 0x64},
	{0x936d, 0x5f},
	{0x9304, 0x03},
	{0x9305, 0x80},
	{0xa2a9, 0x27},
	{0xa2b7, 0x03},
	{0x0401, 0x00},
	{0x0404, 0x00},
	{0x0405, 0x10},
	{0x0408, 0x01},
	{0x0409, 0x5c},
	{0x040a, 0x00},
	{0x040b, 0x00},
	{0x040c, 0x05},
	{0x040d, 0x34},
	{0x040e, 0x03},
	{0x040f, 0xde},
	{0x034c, 0x05},
	{0x034d, 0x34},
	{0x034e, 0x03},
	{0x034f, 0xde},
	{0x0305, 0x02},
	{0x0306, 0x00},
	{0x0307, 0xaf},
	{0xe04c, 0x00},
	{0xe04d, 0x5f},
	{0xe04e, 0x00},
	{0xe04f, 0x1f},
	{0x3f56, 0x00},
	{0x3f57, 0xbf},
	{REG_NULL, 0x00},
};

/* Rockchip RAW10 overrides, applied after the selected two-lane mode table. */
static const struct regval imx577_2lane_raw10_regs[] = {
	{IMX577_REG_CSI_FORMAT, 0x0a},
	{IMX577_REG_CSI_FORMAT + 1, 0x0a},
	{IMX577_REG_IOP_PIX_DIV, 0x0a},
	{IMX577_REG_ADBIT_MODE, 0x00},
	{IMX577_REG_LONG_EXP_SHIFT, 0x00},
	{0x0b05, 0x01},
	{0x0b06, 0x01},
	{REG_NULL, 0x00},
};

/*
 * Two-lane mode timing uses the VT clock, not CSI payload throughput.
 * The 4056x2160 frame length includes 64 blanking lines for the RK receiver.
 */
static const struct imx577_mode imx577_2lane_modes[] = {
	{
		.bus_fmt = MEDIA_BUS_FMT_SRGGB10_1X10,
		.width = 2028,
		.height = 1520,
		.max_fps = {
			.numerator = IMX577_2LANE_HTS(2028) * 2197,
			.denominator = IMX577_2LANE_VT_PIXEL_RATE,
		},
		.hts_def = IMX577_2LANE_HTS(2028),
		.vts_def = 2197,
		.exp_def = 0x640,
		.bpp = 10,
		.reg_list = imx577_2lane_2028x1520_regs,
		.hdr_mode = NO_HDR,
		.link_freq_idx = 0,
		.vc[PAD0] = 0,
	},
	{
		.bus_fmt = MEDIA_BUS_FMT_SRGGB10_1X10,
		.width = 4056,
		.height = 3040,
		.max_fps = {
			.numerator = IMX577_2LANE_HTS(4056) * 3500,
			.denominator = IMX577_2LANE_VT_PIXEL_RATE,
		},
		.hts_def = IMX577_2LANE_HTS(4056),
		.vts_def = 3500,
		.exp_def = 0x640,
		.bpp = 10,
		.reg_list = imx577_2lane_4056x3040_regs,
		.hdr_mode = NO_HDR,
		.link_freq_idx = 0,
		.vc[PAD0] = 0,
	},
	{
		.bus_fmt = MEDIA_BUS_FMT_SRGGB10_1X10,
		.width = 4056,
		.height = 2160,
		.max_fps = {
			.numerator = IMX577_2LANE_HTS(4056) * 2224,
			.denominator = IMX577_2LANE_VT_PIXEL_RATE,
		},
		.hts_def = IMX577_2LANE_HTS(4056),
		.vts_def = 2224,
		.exp_def = 0x640,
		.bpp = 10,
		.reg_list = imx577_2lane_4056x2160_regs,
		.hdr_mode = NO_HDR,
		.link_freq_idx = 0,
		.vc[PAD0] = 0,
	},
	{
		.bus_fmt = MEDIA_BUS_FMT_SRGGB10_1X10,
		.width = 2028,
		.height = 1080,
		.max_fps = {
			.numerator = IMX577_2LANE_HTS(2028) * 2197,
			.denominator = IMX577_2LANE_VT_PIXEL_RATE,
		},
		.hts_def = IMX577_2LANE_HTS(2028),
		.vts_def = 2197,
		.exp_def = 0x640,
		.bpp = 10,
		.reg_list = imx577_2lane_2028x1080_regs,
		.hdr_mode = NO_HDR,
		.link_freq_idx = 0,
		.vc[PAD0] = 0,
	},
	{
		.bus_fmt = MEDIA_BUS_FMT_SRGGB10_1X10,
		.width = 1332,
		.height = 990,
		.max_fps = {
			.numerator = IMX577_2LANE_HTS(1332) * 1050,
			.denominator = IMX577_2LANE_VT_PIXEL_RATE,
		},
		.hts_def = IMX577_2LANE_HTS(1332),
		.vts_def = 1050,
		.exp_def = 0x400,
		.bpp = 10,
		.reg_list = imx577_2lane_1332x990_regs,
		.hdr_mode = NO_HDR,
		.link_freq_idx = 0,
		.vc[PAD0] = 0,
	},
};

static const s64 imx577_2lane_link_freq_items[] = {
	IMX577_2LANE_LINK_FREQ,
};

static const char * const imx577_2lane_test_pattern_menu[] = {
	"Disabled",
	"Solid Color",
	"Color Bars",
	"Grey Color",
	"PN9",
};

/* Shared register I/O and V4L2 operations. */
/* Write registers up to 4 at a time */
static int imx577_write_reg(struct i2c_client *client, u16 reg,
			     u32 len, u32 val)
{
	u32 buf_i, val_i;
	u8 buf[6];
	u8 *val_p;
	__be32 val_be;

	dev_dbg(&client->dev, "write reg(0x%x val:0x%x)!\n", reg, val);

	if (len > 4)
		return -EINVAL;

	buf[0] = reg >> 8;
	buf[1] = reg & 0xff;

	val_be = cpu_to_be32(val);
	val_p = (u8 *)&val_be;
	buf_i = 2;
	val_i = 4 - len;

	while (val_i < 4)
		buf[buf_i++] = val_p[val_i++];

	if (i2c_master_send(client, buf, len + 2) != len + 2)
		return -EIO;

	return 0;
}

static int imx577_write_array(struct i2c_client *client,
			       const struct regval *regs)
{
	u32 i;
	int ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++)
		ret = imx577_write_reg(client, regs[i].addr,
					IMX577_REG_VALUE_08BIT,
					regs[i].val);

	return ret;
}

/* Read registers up to 4 at a time */
static int imx577_read_reg(struct i2c_client *client, u16 reg,
			    unsigned int len, u32 *val)
{
	struct i2c_msg msgs[2];
	u8 *data_be_p;
	__be32 data_be = 0;
	__be16 reg_addr_be = cpu_to_be16(reg);
	int ret;

	if (len > 4 || !len)
		return -EINVAL;

	data_be_p = (u8 *)&data_be;
	/* Write register address */
	msgs[0].addr = client->addr;
	msgs[0].flags = 0;
	msgs[0].len = 2;
	msgs[0].buf = (u8 *)&reg_addr_be;

	/* Read data from register */
	msgs[1].addr = client->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = len;
	msgs[1].buf = &data_be_p[4 - len];

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret != ARRAY_SIZE(msgs))
		return -EIO;

	*val = be32_to_cpu(data_be);

	return 0;
}

static int imx577_get_reso_dist(const struct imx577_mode *mode,
				 struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
	       abs(mode->height - framefmt->height);
}

static const struct imx577_mode *
imx577_find_best_fit(struct imx577 *imx577, struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < imx577->cfg_num; i++) {
		dist = imx577_get_reso_dist(&imx577->supported_modes[i], framefmt);
		if (cur_best_fit_dist == -1 || dist < cur_best_fit_dist) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &imx577->supported_modes[cur_best_fit];
}

static int imx577_2lane_update_controls(struct imx577 *imx577)
{
	const struct imx577_mode *mode = imx577->cur_mode;
	u32 hblank = mode->hts_def - mode->width;
	u32 vblank = mode->vts_def - mode->height;
	int ret;

	ret = __v4l2_ctrl_modify_range(imx577->hblank, hblank, hblank, 1, hblank);
	if (ret)
		return ret;
	ret = __v4l2_ctrl_modify_range(imx577->vblank, vblank,
				       IMX577_2LANE_VTS_MAX - mode->height, 1, vblank);
	if (ret)
		return ret;
	ret = __v4l2_ctrl_modify_range(imx577->exposure, IMX577_EXPOSURE_MIN,
				       mode->vts_def - CIT_MARGIN,
				       IMX577_EXPOSURE_STEP, mode->exp_def);
	if (ret)
		return ret;

	imx577->cur_vts = mode->vts_def;
	ret = __v4l2_ctrl_s_ctrl(imx577->vblank, vblank);
	if (ret)
		return ret;
	ret = __v4l2_ctrl_s_ctrl(imx577->link_freq, mode->link_freq_idx);
	if (ret)
		return ret;
	return __v4l2_ctrl_s_ctrl_int64(imx577->pixel_rate, IMX577_2LANE_VT_PIXEL_RATE);
}

static int imx577_set_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_state *sd_state,
			  struct v4l2_subdev_format *fmt)
{
	struct imx577 *imx577 = to_imx577(sd);
	const struct imx577_mode *mode;
	s64 h_blank, vblank_def;
	u64 pixel_rate = 0;
	u32 lane_num = imx577->bus_cfg.bus.mipi_csi2.num_data_lanes;
	int ret = 0;

	if (imx577_is_2lane(imx577) && fmt->pad)
		return -EINVAL;

	mutex_lock(&imx577->mutex);

	if (imx577_is_2lane(imx577) && imx577->streaming &&
	    fmt->which == V4L2_SUBDEV_FORMAT_ACTIVE) {
		ret = -EBUSY;
		goto unlock;
	}

	mode = imx577_find_best_fit(imx577, fmt);
	fmt->format.code = mode->bus_fmt;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, sd_state, fmt->pad) = fmt->format;
#else
		mutex_unlock(&imx577->mutex);
		return -ENOTTY;
#endif
	} else {
		imx577->cur_mode = mode;
		if (imx577_is_2lane(imx577)) {
			ret = imx577_2lane_update_controls(imx577);
			goto unlock;
		}
		imx577->cur_vts = imx577->cur_mode->vts_def;
		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(imx577->hblank, h_blank,
					 h_blank, 1, h_blank);
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(imx577->vblank, vblank_def,
					 IMX577_4LANE_VTS_MAX - mode->height,
					 1, vblank_def);
		pixel_rate = (u32)imx577->link_freq_menu[mode->link_freq_idx] /
			     mode->bpp * 2 * lane_num;

		__v4l2_ctrl_s_ctrl_int64(imx577->pixel_rate,
					 pixel_rate);
		__v4l2_ctrl_s_ctrl(imx577->link_freq,
				   mode->link_freq_idx);
	}

unlock:
	mutex_unlock(&imx577->mutex);

	return ret;
}

static int imx577_get_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_state *sd_state,
			   struct v4l2_subdev_format *fmt)
{
	struct imx577 *imx577 = to_imx577(sd);
	const struct imx577_mode *mode;

	if (imx577_is_2lane(imx577) && fmt->pad)
		return -EINVAL;

	mutex_lock(&imx577->mutex);
	mode = imx577->cur_mode;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, sd_state, fmt->pad);
#else
		mutex_unlock(&imx577->mutex);
		return -ENOTTY;
#endif
	} else {
		fmt->format.width = mode->width;
		fmt->format.height = mode->height;
		fmt->format.code = mode->bus_fmt;
		fmt->format.field = V4L2_FIELD_NONE;
		if (fmt->pad < PAD_MAX && mode->hdr_mode != NO_HDR)
			fmt->reserved[0] = mode->vc[fmt->pad];
		else
			fmt->reserved[0] = mode->vc[PAD0];
	}
	mutex_unlock(&imx577->mutex);

	return 0;
}

static int imx577_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	struct imx577 *imx577 = to_imx577(sd);

	if (code->index != 0 || (imx577_is_2lane(imx577) && code->pad))
		return -EINVAL;
	code->code = imx577->cur_mode->bus_fmt;

	return 0;
}

static int imx577_enum_frame_sizes(struct v4l2_subdev *sd,
				    struct v4l2_subdev_state *sd_state,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	struct imx577 *imx577 = to_imx577(sd);
	const struct imx577_mode *mode;

	if (fse->index >= imx577->cfg_num ||
	    (imx577_is_2lane(imx577) && fse->pad))
		return -EINVAL;

	mode = &imx577->supported_modes[fse->index];
	if (fse->code != mode->bus_fmt)
		return -EINVAL;

	fse->min_width  = mode->width;
	fse->max_width  = mode->width;
	fse->max_height = mode->height;
	fse->min_height = mode->height;

	return 0;
}

static int imx577_enable_test_pattern(struct imx577 *imx577, u32 pattern)
{
	u32 val;

	if (imx577_is_2lane(imx577))
		return imx577_write_reg(imx577->client, IMX577_REG_TEST_PATTERN,
					IMX577_REG_VALUE_16BIT, pattern);

	if (pattern)
		val = (pattern - 1) | IMX577_4LANE_TEST_PATTERN_ENABLE;
	else
		val = IMX577_4LANE_TEST_PATTERN_DISABLE;

	return imx577_write_reg(imx577->client,
				 IMX577_REG_TEST_PATTERN,
				 IMX577_REG_VALUE_08BIT,
				 val);
}

static int imx577_g_frame_interval(struct v4l2_subdev *sd,
				    struct v4l2_subdev_frame_interval *fi)
{
	struct imx577 *imx577 = to_imx577(sd);

	if (imx577_is_2lane(imx577) && fi->pad)
		return -EINVAL;

	mutex_lock(&imx577->mutex);
	if (imx577_is_2lane(imx577)) {
		fi->interval.numerator = imx577->cur_mode->hts_def *
			(imx577->cur_mode->height + imx577->vblank->val);
		fi->interval.denominator = IMX577_2LANE_VT_PIXEL_RATE;
	} else {
		fi->interval = imx577->cur_mode->max_fps;
	}
	mutex_unlock(&imx577->mutex);

	return 0;
}

static void imx577_get_module_inf(struct imx577 *imx577,
				   struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strscpy(inf->base.sensor, IMX577_NAME, sizeof(inf->base.sensor));
	strscpy(inf->base.module, imx577->module_name,
		sizeof(inf->base.module));
	strscpy(inf->base.lens, imx577->len_name, sizeof(inf->base.lens));
}

static void imx577_set_awb_cfg(struct imx577 *imx577,
			       struct rkmodule_awb_cfg *cfg)
{
	mutex_lock(&imx577->mutex);
	memcpy(&imx577->awb_cfg, cfg, sizeof(*cfg));
	mutex_unlock(&imx577->mutex);
}

static void imx577_set_lsc_cfg(struct imx577 *imx577,
			       struct rkmodule_lsc_cfg *cfg)
{
	mutex_lock(&imx577->mutex);
	memcpy(&imx577->lsc_cfg, cfg, sizeof(*cfg));
	mutex_unlock(&imx577->mutex);
}

static int imx577_4lane_set_hdrae(struct imx577 *imx577,
				  struct preisp_hdrae_exp_s *ae)
{
	struct i2c_client *client = imx577->client;
	u32 l_exp_time, m_exp_time, s_exp_time;
	u32 l_a_gain, m_a_gain, s_a_gain;
	u32 l_d_gain, s_d_gain;
	int ret = 0;
	u32 fll, dol_cit1, dol_cit2, dol_off2;

	if (!imx577->has_init_exp && !imx577->streaming) {
		imx577->init_hdrae_exp = *ae;
		imx577->has_init_exp = true;
		dev_dbg(&imx577->client->dev, "imx577 is not streaming, save hdr ae!\n");
		return ret;
	}
	l_exp_time = ae->long_exp_reg;
	m_exp_time = ae->middle_exp_reg;
	s_exp_time = ae->short_exp_reg;
	l_a_gain = ae->long_gain_reg;
	m_a_gain = ae->middle_gain_reg;
	s_a_gain = ae->short_gain_reg;
	dev_dbg(&client->dev,
		"rev exp req: L_exp: 0x%x, 0x%x, M_exp: 0x%x, 0x%x S_exp: 0x%x, 0x%x\n",
		l_exp_time, m_exp_time, s_exp_time,
		l_a_gain, m_a_gain, s_a_gain);

	if (imx577->cur_mode->hdr_mode == HDR_X2) {
		l_a_gain = m_a_gain;
		l_exp_time = m_exp_time;
	}

	ret = imx577_write_reg(client, IMX577_GROUP_HOLD_REG,
		IMX577_REG_VALUE_08BIT, IMX577_GROUP_HOLD_START);
	/* gain effect n+1 */
	if (l_a_gain > 0x1600)
		l_a_gain = 0x1600;
	if (l_a_gain < 0x10)
		l_a_gain = 0x10;
	if (s_a_gain > 0x1600)
		s_a_gain = 0x1600;
	if (s_a_gain < 0x10)
		s_a_gain = 0x10;
	l_d_gain = l_a_gain > 0x160 ? (l_a_gain * 256 / 22 / 16) : 256;
	l_a_gain = l_a_gain > 0x160 ? 0x160 : l_a_gain;
	l_a_gain = 1024 - 1024 * 16 / l_a_gain;
	s_d_gain = s_a_gain > 0x160 ? (s_a_gain * 256 / 22 / 16) : 256;
	s_a_gain = s_a_gain > 0x160 ? 0x160 : s_a_gain;
	s_a_gain = 1024 - 1024 * 16 / s_a_gain;

	ret |= imx577_write_reg(client, IMX577_LF_GAIN_REG_H,
		IMX577_REG_VALUE_08BIT, IMX577_FETCH_GAIN_H(l_a_gain));
	ret |= imx577_write_reg(client, IMX577_LF_GAIN_REG_L,
		IMX577_REG_VALUE_08BIT, IMX577_FETCH_GAIN_L(l_a_gain));
	ret |= imx577_write_reg(client, IMX577_SEF1_GAIN_REG_H,
		IMX577_REG_VALUE_08BIT, IMX577_FETCH_GAIN_H(s_a_gain));
	ret |= imx577_write_reg(client, IMX577_SEF1_GAIN_REG_L,
		IMX577_REG_VALUE_08BIT, IMX577_FETCH_GAIN_L(s_a_gain));

	if (IMX577_DGAIN_MODE && l_d_gain > 0 && s_d_gain > 0) {
		ret |= imx577_write_reg(client, IMX577_LF_DGAIN_REG_H,
			IMX577_REG_VALUE_08BIT, IMX577_FETCH_DGAIN_H(l_d_gain));
		ret |= imx577_write_reg(client, IMX577_LF_DGAIN_REG_L,
			IMX577_REG_VALUE_08BIT, IMX577_FETCH_DGAIN_L(l_d_gain));
		ret |= imx577_write_reg(client, IMX577_SEF1_DGAIN_REG_H,
			IMX577_REG_VALUE_08BIT, IMX577_FETCH_DGAIN_H(s_d_gain));
		ret |= imx577_write_reg(client, IMX577_SEF1_DGAIN_REG_L,
			IMX577_REG_VALUE_08BIT, IMX577_FETCH_DGAIN_L(s_d_gain));
	}

	fll = imx577->cur_vts;
	dol_cit1 = l_exp_time >> 1;
	dol_cit2 = s_exp_time >> 1;

	/*dol_cit1 dol_cit2 dol_off2 should be even*/
	if (dol_cit1 < 2)
		dol_cit1 = 2;
	else if (dol_cit1 > fll - 2 * CIT_MARGIN - 2)
		dol_cit1 = fll - 2 * CIT_MARGIN - 2;
	dol_cit1 &= (~0x1);

	if (dol_cit2 < 2)
		dol_cit2 = 2;
	else if (dol_cit2 > fll - IMX577_4LANE_BRL_FULL - CIT_MARGIN)
		dol_cit2 = fll - IMX577_4LANE_BRL_FULL - CIT_MARGIN;
	dol_cit2 &= (~0x1);

	dol_off2 = (dol_cit2 + CIT_MARGIN) & (~0x1);
	if (dol_off2 < dol_cit2 + CIT_MARGIN)
		dol_off2 = (dol_cit2 + CIT_MARGIN) & (~0x1);
	else if (dol_off2 > fll - IMX577_4LANE_BRL_FULL)
		dol_off2 = (fll - IMX577_4LANE_BRL_FULL) & (~0x1);

	dev_dbg(&client->dev,
		"l_exp_time=%d,s_exp_time=%d,fll=%d,rhs1=%d,l_a_gain=%d,s_a_gain=%d\n",
		l_exp_time, s_exp_time, fll, dol_off2, l_a_gain, s_a_gain);

	ret |= imx577_write_reg(client,
		IMX577_RHS1_REG_L,
		IMX577_REG_VALUE_08BIT,
		IMX577_FETCH_RHS1_L(dol_off2));
	ret |= imx577_write_reg(client,
		IMX577_RHS1_REG_H,
		IMX577_REG_VALUE_08BIT,
		IMX577_FETCH_RHS1_H(dol_off2));

	ret |= imx577_write_reg(client,
		IMX577_SEF1_EXPO_REG_L,
		IMX577_REG_VALUE_08BIT,
		IMX577_FETCH_EXP_L(dol_cit2));
	ret |= imx577_write_reg(client,
		IMX577_SEF1_EXPO_REG_H,
		IMX577_REG_VALUE_08BIT,
		IMX577_FETCH_EXP_H(dol_cit2));
	ret |= imx577_write_reg(client,
		IMX577_LF_EXPO_REG_L,
		IMX577_REG_VALUE_08BIT,
		IMX577_FETCH_EXP_L(dol_cit1));
	ret |= imx577_write_reg(client,
		IMX577_LF_EXPO_REG_H,
		IMX577_REG_VALUE_08BIT,
		IMX577_FETCH_EXP_H(dol_cit1));

	ret |= imx577_write_reg(client, IMX577_GROUP_HOLD_REG,
		IMX577_REG_VALUE_08BIT, IMX577_GROUP_HOLD_END);

	return ret;
}

static int imx577_get_channel_info(struct imx577 *imx577, struct rkmodule_channel_info *ch_info)
{
	if (ch_info->index < PAD0 || ch_info->index >= PAD_MAX)
		return -EINVAL;
	ch_info->vc = imx577->cur_mode->vc[ch_info->index];
	ch_info->width = imx577->cur_mode->width;
	ch_info->height = imx577->cur_mode->height;
	ch_info->bus_fmt = imx577->cur_mode->bus_fmt;

	return 0;
}

static long imx577_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct imx577 *imx577 = to_imx577(sd);
	struct rkmodule_hdr_cfg *hdr;
	struct rkmodule_channel_info *ch_info;
	long ret = 0;
	u32 i, h, w;
	s64 dst_pixel_rate = 0;
	u32 stream = 0;
	const struct imx577_mode *mode;
	u32 lane_num = imx577->bus_cfg.bus.mipi_csi2.num_data_lanes;

	switch (cmd) {
	case PREISP_CMD_SET_HDRAE_EXP:
		if (imx577->cur_mode->hdr_mode == HDR_X2)
			ret = imx577_4lane_set_hdrae(imx577, arg);
		break;
	case RKMODULE_GET_MODULE_INFO:
		imx577_get_module_inf(imx577, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_GET_HDR_CFG:
		hdr = (struct rkmodule_hdr_cfg *)arg;
		hdr->esp.mode = HDR_NORMAL_VC;
		hdr->hdr_mode = imx577->cur_mode->hdr_mode;
		break;
	case RKMODULE_SET_HDR_CFG:
		hdr = (struct rkmodule_hdr_cfg *)arg;
		if (imx577_is_2lane(imx577)) {
			if (hdr->hdr_mode != NO_HDR)
				ret = -EINVAL;
			break;
		}
		w = imx577->cur_mode->width;
		h = imx577->cur_mode->height;
		for (i = 0; i < imx577->cfg_num; i++) {
			if (w == imx577->supported_modes[i].width &&
			    h == imx577->supported_modes[i].height &&
			    imx577->supported_modes[i].hdr_mode == hdr->hdr_mode) {
				imx577->cur_mode = &imx577->supported_modes[i];
				break;
			}
		}
		if (i == imx577->cfg_num) {
			dev_err(&imx577->client->dev,
				"not find hdr mode:%d %dx%d config\n",
				hdr->hdr_mode, w, h);
			ret = -EINVAL;
		} else {
			mode = imx577->cur_mode;
			imx577->cur_vts = mode->vts_def;
			w = mode->hts_def - mode->width;
			h = mode->vts_def - mode->height;
			mutex_lock(&imx577->mutex);
			__v4l2_ctrl_modify_range(imx577->hblank, w, w, 1, w);
			__v4l2_ctrl_modify_range(imx577->vblank, h,
				IMX577_4LANE_VTS_MAX - mode->height,
				1, h);
			__v4l2_ctrl_s_ctrl(imx577->link_freq, mode->link_freq_idx);
			dst_pixel_rate = (u32)imx577->link_freq_menu[mode->link_freq_idx] /
					mode->bpp * 2 * lane_num;
			__v4l2_ctrl_s_ctrl_int64(imx577->pixel_rate,
						 dst_pixel_rate);
			mutex_unlock(&imx577->mutex);
		}
		break;
	case RKMODULE_AWB_CFG:
		imx577_set_awb_cfg(imx577, (struct rkmodule_awb_cfg *)arg);
		break;
	case RKMODULE_LSC_CFG:
		imx577_set_lsc_cfg(imx577, (struct rkmodule_lsc_cfg *)arg);
		break;
	case RKMODULE_SET_QUICK_STREAM:

		stream = *((u32 *)arg);

		if (stream)
			ret = imx577_write_reg(imx577->client,
				 IMX577_REG_CTRL_MODE,
				 IMX577_REG_VALUE_08BIT,
				 IMX577_MODE_STREAMING);
		else
			ret = imx577_write_reg(imx577->client,
				 IMX577_REG_CTRL_MODE,
				 IMX577_REG_VALUE_08BIT,
				 IMX577_MODE_SW_STANDBY);
		break;
	case RKMODULE_GET_CHANNEL_INFO:
		ch_info = (struct rkmodule_channel_info *)arg;
		ret = imx577_get_channel_info(imx577, ch_info);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long imx577_compat_ioctl32(struct v4l2_subdev *sd,
				   unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkmodule_inf *inf;
	struct rkmodule_awb_cfg *cfg;
	struct rkmodule_hdr_cfg *hdr;
	struct preisp_hdrae_exp_s *hdrae;
	struct rkmodule_lsc_cfg *lsc_cfg;
	struct rkmodule_channel_info *ch_info;
	long ret = 0;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		inf = kzalloc(sizeof(*inf), GFP_KERNEL);
		if (!inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = imx577_ioctl(sd, cmd, inf);
		if (!ret) {
			ret = copy_to_user(up, inf, sizeof(*inf));
			if (ret)
				ret = -EFAULT;
		}
		kfree(inf);
		break;
	case RKMODULE_AWB_CFG:
		cfg = kzalloc(sizeof(*cfg), GFP_KERNEL);
		if (!cfg) {
			ret = -ENOMEM;
			return ret;
		}

		ret = copy_from_user(cfg, up, sizeof(*cfg));
		if (!ret)
			ret = imx577_ioctl(sd, cmd, cfg);
		else
			ret = -EFAULT;
		kfree(cfg);
		break;
	case RKMODULE_GET_HDR_CFG:
		hdr = kzalloc(sizeof(*hdr), GFP_KERNEL);
		if (!hdr) {
			ret = -ENOMEM;
			return ret;
		}

		ret = imx577_ioctl(sd, cmd, hdr);
		if (!ret) {
			if (copy_to_user(up, hdr, sizeof(*hdr))) {
				kfree(hdr);
				return -EFAULT;
			}
		}
		kfree(hdr);
		break;
	case RKMODULE_SET_HDR_CFG:
		hdr = kzalloc(sizeof(*hdr), GFP_KERNEL);
		if (!hdr) {
			ret = -ENOMEM;
			return ret;
		}

		if (copy_from_user(hdr, up, sizeof(*hdr))) {
			kfree(hdr);
			return -EFAULT;
		}
		ret = imx577_ioctl(sd, cmd, hdr);
		kfree(hdr);
		break;
	case PREISP_CMD_SET_HDRAE_EXP:
		hdrae = kzalloc(sizeof(*hdrae), GFP_KERNEL);
		if (!hdrae) {
			ret = -ENOMEM;
			return ret;
		}

		if (copy_from_user(hdrae, up, sizeof(*hdrae))) {
			kfree(hdrae);
			return -EFAULT;
		}
		ret = imx577_ioctl(sd, cmd, hdrae);
		kfree(hdrae);
		break;
	case RKMODULE_LSC_CFG:
		lsc_cfg = kzalloc(sizeof(*lsc_cfg), GFP_KERNEL);
		if (!lsc_cfg) {
			ret = -ENOMEM;
			return ret;
		}

		ret = copy_from_user(lsc_cfg, up, sizeof(*lsc_cfg));
		if (!ret)
			ret = imx577_ioctl(sd, cmd, lsc_cfg);
		else
			ret = -EFAULT;
		kfree(lsc_cfg);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		ret = copy_from_user(&stream, up, sizeof(u32));
		if (!ret)
			ret = imx577_ioctl(sd, cmd, &stream);
		else
			ret = -EFAULT;
		break;
	case RKMODULE_GET_CHANNEL_INFO:
		ch_info = kzalloc(sizeof(*ch_info), GFP_KERNEL);
		if (!ch_info) {
			ret = -ENOMEM;
			return ret;
		}

		ret = imx577_ioctl(sd, cmd, ch_info);
		if (!ret) {
			ret = copy_to_user(up, ch_info, sizeof(*ch_info));
			if (ret)
				ret = -EFAULT;
		}
		kfree(ch_info);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}
#endif

static int imx577_2lane_configure(struct imx577 *imx577)
{
	struct i2c_client *client = imx577->client;
	int ret;

	/* Restore the complete two-lane state after runtime power loss. */
	ret = imx577_write_array(client, imx577_2lane_common_regs);
	if (ret)
		return ret;
	/* 24 MHz / 2 * 150 / 2 = 900 Mbit/s per lane. */
	ret = imx577_write_reg(client, IMX577_REG_IOP_PLL_MPY,
			       IMX577_REG_VALUE_16BIT, 150);
	if (ret)
		return ret;
	ret = imx577_write_reg(client, IMX577_REG_LINK_BIT_RATE,
			       IMX577_REG_VALUE_32BIT,
			       (IMX577_2LANE_LINK_FREQ / 1000000 * 4) << 16);
	if (ret)
		return ret;
	ret = imx577_write_array(client, imx577->cur_mode->reg_list);
	if (ret)
		return ret;
	ret = imx577_write_array(client, imx577_2lane_raw10_regs);
	if (ret)
		return ret;
	return imx577_write_reg(client, IMX577_REG_HTS,
				IMX577_REG_VALUE_16BIT, imx577->cur_mode->hts_def);
}

static int __imx577_start_stream(struct imx577 *imx577)
{
	int ret;

	if (imx577_is_2lane(imx577))
		ret = imx577_2lane_configure(imx577);
	else
		ret = imx577_write_array(imx577->client, imx577->cur_mode->reg_list);
	if (ret)
		return ret;

	/* In case these controls are set before streaming */
	ret = __v4l2_ctrl_handler_setup(&imx577->ctrl_handler);
	if (ret)
		return ret;

	if (imx577->has_init_exp && imx577->cur_mode->hdr_mode != NO_HDR) {
		ret = imx577_ioctl(&imx577->subdev, PREISP_CMD_SET_HDRAE_EXP,
			&imx577->init_hdrae_exp);
		if (ret) {
			dev_err(&imx577->client->dev,
				"init exp fail in hdr mode\n");
			return ret;
		}
	}
	return imx577_write_reg(imx577->client,
				 IMX577_REG_CTRL_MODE,
				 IMX577_REG_VALUE_08BIT,
				 IMX577_MODE_STREAMING);
}

static int __imx577_stop_stream(struct imx577 *imx577)
{
	imx577->has_init_exp = false;
	return imx577_write_reg(imx577->client,
				 IMX577_REG_CTRL_MODE,
				 IMX577_REG_VALUE_08BIT,
				 IMX577_MODE_SW_STANDBY);
}

static int imx577_s_stream(struct v4l2_subdev *sd, int on)
{
	struct imx577 *imx577 = to_imx577(sd);
	struct i2c_client *client = imx577->client;
	int ret = 0;

	dev_info(&client->dev, "%s: on: %d, %dx%d@%d, hdr: %d, bpp: %d\n",
				__func__, on, imx577->cur_mode->width,
				imx577->cur_mode->height,
		DIV_ROUND_CLOSEST(imx577->cur_mode->max_fps.denominator,
				  imx577->cur_mode->max_fps.numerator),
				  imx577->cur_mode->hdr_mode, imx577->cur_mode->bpp);

	mutex_lock(&imx577->mutex);
	on = !!on;
	if (on == imx577->streaming)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = __imx577_start_stream(imx577);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		__imx577_stop_stream(imx577);
		pm_runtime_put(&client->dev);
	}

	imx577->streaming = on;

unlock_and_return:
	mutex_unlock(&imx577->mutex);

	return ret;
}

static int imx577_s_power(struct v4l2_subdev *sd, int on)
{
	struct imx577 *imx577 = to_imx577(sd);
	struct i2c_client *client = imx577->client;
	int ret = 0;

	mutex_lock(&imx577->mutex);

	/* If the power state is not modified - no work to do. */
	if (imx577->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = imx577_write_array(imx577->client,
					 imx577_is_2lane(imx577) ? imx577_2lane_common_regs :
					 imx577_4lane_common_regs);
		if (ret) {
			v4l2_err(sd, "could not set init registers\n");
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		imx577->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		imx577->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&imx577->mutex);

	return ret;
}

/* Calculate the delay in us by clock rate and clock cycles */
static inline u32 imx577_cal_delay(u32 cycles)
{
	return DIV_ROUND_UP(cycles, IMX577_XVCLK_FREQ / 1000 / 1000);
}

static int __imx577_power_on(struct imx577 *imx577)
{
	int ret;
	u32 delay_us;
	struct device *dev = &imx577->client->dev;

	if (!IS_ERR(imx577->power_gpio))
		gpiod_set_value_cansleep(imx577->power_gpio, 1);

	usleep_range(1000, 2000);

	if (!IS_ERR_OR_NULL(imx577->pins_default)) {
		ret = pinctrl_select_state(imx577->pinctrl,
					   imx577->pins_default);
		if (ret < 0)
			dev_err(dev, "could not set pins\n");
	}
	if (imx577->xvclk) {
		ret = clk_set_rate(imx577->xvclk, IMX577_XVCLK_FREQ);
		if (imx577_is_2lane(imx577) &&
		    (ret < 0 || clk_get_rate(imx577->xvclk) != IMX577_XVCLK_FREQ)) {
			dev_err(dev,
				"xvclk setup failed: expected %lu Hz, got %lu Hz (ret=%d)\n",
				(unsigned long)IMX577_XVCLK_FREQ,
				clk_get_rate(imx577->xvclk), ret);
			if (!ret)
				ret = -EINVAL;
			goto disable_power;
		}
		if (ret < 0)
			dev_warn(dev, "Failed to set xvclk rate to %lu Hz: %d\n",
				 (unsigned long)IMX577_XVCLK_FREQ, ret);
		if (clk_get_rate(imx577->xvclk) != IMX577_XVCLK_FREQ)
			dev_warn(dev, "xvclk mismatch: expected %lu Hz, got %lu Hz\n",
				 (unsigned long)IMX577_XVCLK_FREQ,
				 clk_get_rate(imx577->xvclk));
		ret = clk_prepare_enable(imx577->xvclk);
		if (ret < 0) {
			dev_err(dev, "Failed to enable xvclk\n");
			goto disable_power;
		}
	}
	if (!IS_ERR(imx577->reset_gpio))
		gpiod_set_value_cansleep(imx577->reset_gpio, 0);

	ret = regulator_bulk_enable(IMX577_NUM_SUPPLIES, imx577->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}

	if (!IS_ERR(imx577->reset_gpio))
		gpiod_set_value_cansleep(imx577->reset_gpio, 1);

	usleep_range(500, 1000);
	if (!IS_ERR(imx577->pwdn_gpio))
		gpiod_set_value_cansleep(imx577->pwdn_gpio, 1);

	/* 8192 cycles prior to first SCCB transaction */
	delay_us = imx577_cal_delay(8192);
	usleep_range(delay_us, delay_us * 2);

	return 0;

disable_clk:
	if (imx577->xvclk)
		clk_disable_unprepare(imx577->xvclk);
disable_power:
	if (!IS_ERR(imx577->power_gpio))
		gpiod_set_value_cansleep(imx577->power_gpio, 0);

	return ret;
}

static void __imx577_power_off(struct imx577 *imx577)
{
	int ret;
	struct device *dev = &imx577->client->dev;

	if (!IS_ERR(imx577->pwdn_gpio))
		gpiod_set_value_cansleep(imx577->pwdn_gpio, 0);
	if (imx577->xvclk)
		clk_disable_unprepare(imx577->xvclk);
	if (!IS_ERR(imx577->reset_gpio))
		gpiod_set_value_cansleep(imx577->reset_gpio, 0);

	if (!IS_ERR_OR_NULL(imx577->pins_sleep)) {
		ret = pinctrl_select_state(imx577->pinctrl,
					   imx577->pins_sleep);
		if (ret < 0)
			dev_dbg(dev, "could not set pins\n");
	}
	if (!IS_ERR(imx577->power_gpio))
		gpiod_set_value_cansleep(imx577->power_gpio, 0);

	regulator_bulk_disable(IMX577_NUM_SUPPLIES, imx577->supplies);
}

static int imx577_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx577 *imx577 = to_imx577(sd);

	return __imx577_power_on(imx577);
}

static int imx577_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx577 *imx577 = to_imx577(sd);

	__imx577_power_off(imx577);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int imx577_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct imx577 *imx577 = to_imx577(sd);
	struct v4l2_mbus_framefmt *try_fmt =
				v4l2_subdev_get_try_format(sd, fh->state, 0);
	const struct imx577_mode *def_mode = &imx577->supported_modes[0];

	mutex_lock(&imx577->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = def_mode->bus_fmt;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&imx577->mutex);
	/* No crop or compose */

	return 0;
}
#endif

static int imx577_enum_frame_interval(struct v4l2_subdev *sd,
				       struct v4l2_subdev_state *sd_state,
				       struct v4l2_subdev_frame_interval_enum *fie)
{
	struct imx577 *imx577 = to_imx577(sd);
	const struct imx577_mode *mode;
	unsigned int i;

	if (imx577_is_2lane(imx577) && (fie->code || fie->width || fie->height)) {
		if (fie->pad || fie->index || fie->code != MEDIA_BUS_FMT_SRGGB10_1X10)
			return -EINVAL;
		for (i = 0; i < imx577->cfg_num; i++) {
			mode = &imx577->supported_modes[i];
			if (mode->width == fie->width && mode->height == fie->height)
				break;
		}
		if (i == imx577->cfg_num)
			return -EINVAL;
	} else {
		if (fie->index >= imx577->cfg_num ||
		    (imx577_is_2lane(imx577) && fie->pad))
			return -EINVAL;
		mode = &imx577->supported_modes[fie->index];
	}

	fie->code = mode->bus_fmt;
	fie->width = mode->width;
	fie->height = mode->height;
	fie->interval = mode->max_fps;
	fie->reserved[0] = mode->hdr_mode;

	return 0;
}

static int imx577_g_mbus_config(struct v4l2_subdev *sd, unsigned int pad,
				struct v4l2_mbus_config *config)
{
	struct imx577 *imx577 = to_imx577(sd);

	if (imx577_is_2lane(imx577) && pad)
		return -EINVAL;

	config->type = V4L2_MBUS_CSI2_DPHY;
	config->bus.mipi_csi2 = imx577->bus_cfg.bus.mipi_csi2;

	return 0;
}

#define CROP_START(SRC, DST) (((SRC) - (DST)) / 2 / 4 * 4)
#define DST_WIDTH_4048 4048

static int imx577_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *sd_state,
				struct v4l2_subdev_selection *sel)
{
	struct imx577 *imx577 = to_imx577(sd);

	if (sel->target == V4L2_SEL_TGT_CROP_BOUNDS) {
		if (imx577_is_2lane(imx577)) {
			if (sel->pad)
				return -EINVAL;
			mutex_lock(&imx577->mutex);
			sel->r.left = 0;
			sel->r.top = 0;
			sel->r.width = imx577->cur_mode->width;
			sel->r.height = imx577->cur_mode->height;
			mutex_unlock(&imx577->mutex);
			return 0;
		}
		if (imx577->cur_mode->width == 4056) {
			sel->r.left = CROP_START(imx577->cur_mode->width, DST_WIDTH_4048);
			sel->r.width = DST_WIDTH_4048;
			sel->r.top = CROP_START(imx577->cur_mode->height, imx577->cur_mode->height);
			sel->r.height = imx577->cur_mode->height;
		} else {
			sel->r.left = CROP_START(imx577->cur_mode->width,
							imx577->cur_mode->width);
			sel->r.width = imx577->cur_mode->width;
			sel->r.top = CROP_START(imx577->cur_mode->height,
							imx577->cur_mode->height);
			sel->r.height = imx577->cur_mode->height;
		}
		return 0;
	}

	return -EINVAL;
}

static const struct dev_pm_ops imx577_pm_ops = {
	SET_RUNTIME_PM_OPS(imx577_runtime_suspend,
			   imx577_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops imx577_internal_ops = {
	.open = imx577_open,
};
#endif

static const struct v4l2_subdev_core_ops imx577_core_ops = {
	.s_power = imx577_s_power,
	.ioctl = imx577_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = imx577_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops imx577_video_ops = {
	.s_stream = imx577_s_stream,
	.g_frame_interval = imx577_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops imx577_pad_ops = {
	.enum_mbus_code = imx577_enum_mbus_code,
	.enum_frame_size = imx577_enum_frame_sizes,
	.enum_frame_interval = imx577_enum_frame_interval,
	.get_fmt = imx577_get_fmt,
	.set_fmt = imx577_set_fmt,
	.get_selection = imx577_get_selection,
	.get_mbus_config = imx577_g_mbus_config,
};

static const struct v4l2_subdev_ops imx577_subdev_ops = {
	.core	= &imx577_core_ops,
	.video	= &imx577_video_ops,
	.pad	= &imx577_pad_ops,
};

static int imx577_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct imx577 *imx577 = container_of(ctrl->handler,
					     struct imx577, ctrl_handler);
	struct i2c_client *client = imx577->client;
	s64 max;
	int ret = 0;
	u32 again = 0;
	u32 dgain = 0;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		if (imx577->cur_mode->hdr_mode == NO_HDR) {
			/* Update max exposure while meeting expected vblanking */
			max = imx577->cur_mode->height + ctrl->val - 22;
			if (imx577_is_2lane(imx577)) {
				ret = __v4l2_ctrl_modify_range(imx577->exposure,
							       IMX577_EXPOSURE_MIN, max,
							       IMX577_EXPOSURE_STEP,
							       min_t(s64,
								     imx577->cur_mode->exp_def,
								     max));
				if (ret)
					return ret;
				imx577->cur_vts = imx577->cur_mode->height + ctrl->val;
			} else {
				__v4l2_ctrl_modify_range(imx577->exposure,
							 imx577->exposure->minimum, max,
							 imx577->exposure->step,
							 imx577->exposure->default_value);
			}
		}
		break;
	}

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		if (imx577_is_2lane(imx577)) {
			ret = imx577_write_reg(client, IMX577_REG_EXPOSURE_H,
					       IMX577_REG_VALUE_16BIT, ctrl->val);
			break;
		}
		/* 4 least significant bits of expsoure are fractional part */
		if (imx577->cur_mode->hdr_mode != NO_HDR)
			goto ctrl_end;
		ret = imx577_write_reg(imx577->client,
				       IMX577_REG_EXPOSURE_H,
				       IMX577_REG_VALUE_08BIT,
				       IMX577_FETCH_EXP_H(ctrl->val));
		ret |= imx577_write_reg(imx577->client,
					IMX577_REG_EXPOSURE_L,
					IMX577_REG_VALUE_08BIT,
					IMX577_FETCH_EXP_L(ctrl->val));
		dev_dbg(&client->dev, "set exposure 0x%x\n",
			ctrl->val);
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		/* gain_reg = 1024 - 1024 / gain_ana
		 * manual multiple 16 to add accuracy:
		 * then formula change to:
		 * gain_reg = 1024 - 1024 * 16 / (gain_ana * 16)
		 */
		if (imx577->cur_mode->hdr_mode != NO_HDR)
			goto ctrl_end;
		if (ctrl->val > 0x1600)
			ctrl->val = 0x1600;
		if (ctrl->val < 0x10)
			ctrl->val = 0x10;

		dgain = ctrl->val > 0x160 ? (ctrl->val * 256 / 22 / 16) : 256;
		again = ctrl->val > 0x160 ? 0x160 : ctrl->val;
		again = 1024 - 1024 * 16 / again;
		ret = imx577_write_reg(imx577->client, IMX577_REG_GAIN_H,
				       IMX577_REG_VALUE_08BIT,
				       IMX577_FETCH_AGAIN_H(again));
		ret |= imx577_write_reg(imx577->client, IMX577_REG_GAIN_L,
					IMX577_REG_VALUE_08BIT,
					IMX577_FETCH_AGAIN_L(again));
		ret |= imx577_write_reg(imx577->client, IMX577_REG_DGAIN,
					IMX577_REG_VALUE_08BIT,
					IMX577_DGAIN_MODE);
		if (IMX577_DGAIN_MODE && dgain > 0) {
			ret |= imx577_write_reg(imx577->client,
						IMX577_REG_DGAINGR_H,
						IMX577_REG_VALUE_08BIT,
						IMX577_FETCH_DGAIN_H(dgain));
			ret |= imx577_write_reg(imx577->client,
						IMX577_REG_DGAINGR_L,
						IMX577_REG_VALUE_08BIT,
						IMX577_FETCH_DGAIN_L(dgain));
		} else if (dgain > 0) {
			ret |= imx577_write_reg(imx577->client,
						IMX577_REG_DGAINR_H,
						IMX577_REG_VALUE_08BIT,
						IMX577_FETCH_DGAIN_H(dgain));
			ret |= imx577_write_reg(imx577->client,
						IMX577_REG_DGAINR_L,
						IMX577_REG_VALUE_08BIT,
						IMX577_FETCH_DGAIN_L(dgain));
			ret |= imx577_write_reg(imx577->client,
						IMX577_REG_DGAINB_H,
						IMX577_REG_VALUE_08BIT,
						IMX577_FETCH_DGAIN_H(dgain));
			ret |= imx577_write_reg(imx577->client,
						IMX577_REG_DGAINB_L,
						IMX577_REG_VALUE_08BIT,
						IMX577_FETCH_DGAIN_L(dgain));
			ret |= imx577_write_reg(imx577->client,
						IMX577_REG_DGAINGB_H,
						IMX577_REG_VALUE_08BIT,
						IMX577_FETCH_DGAIN_H(dgain));
			ret |= imx577_write_reg(imx577->client,
						IMX577_REG_DGAINGB_L,
						IMX577_REG_VALUE_08BIT,
						IMX577_FETCH_DGAIN_L(dgain));
		}
		dev_dbg(&client->dev, "set analog gain 0x%x\n",
			ctrl->val);
		break;
	case V4L2_CID_VBLANK:
		ret = imx577_write_reg(imx577->client,
					IMX577_REG_VTS,
					IMX577_REG_VALUE_16BIT,
					ctrl->val + imx577->cur_mode->height);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = imx577_enable_test_pattern(imx577, ctrl->val);
		break;
	default:
		dev_warn(&client->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			 __func__, ctrl->id, ctrl->val);
		break;
	}

ctrl_end:
	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops imx577_ctrl_ops = {
	.s_ctrl = imx577_set_ctrl,
};

static int imx577_initialize_controls(struct imx577 *imx577)
{
	const struct imx577_mode *mode;
	struct v4l2_ctrl_handler *handler;
	const char * const *test_pattern_menu;
	unsigned int test_pattern_count;
	s64 exposure_max, vblank_def;
	u32 h_blank;
	int ret;

	handler = &imx577->ctrl_handler;
	mode = imx577->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 8);
	if (ret)
		return ret;
	handler->lock = &imx577->mutex;

	imx577->link_freq = v4l2_ctrl_new_int_menu(handler, NULL,
				V4L2_CID_LINK_FREQ,
				imx577->link_freq_menu_count - 1, 0,
				imx577->link_freq_menu);
	if (imx577->link_freq && imx577_is_2lane(imx577))
		imx577->link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	if (imx577_is_2lane(imx577)) {
		imx577->cur_link_freq = 0;
		imx577->cur_pixel_rate = IMX577_2LANE_VT_PIXEL_RATE;
	} else if (imx577->cur_mode->bus_fmt == MEDIA_BUS_FMT_SRGGB10_1X10) {
		imx577->cur_link_freq = 0;
		imx577->cur_pixel_rate = IMX577_4LANE_PIXEL_RATE_1050M_10BIT;
	} else if (imx577->cur_mode->bus_fmt == MEDIA_BUS_FMT_SRGGB12_1X12) {
		imx577->cur_link_freq = 0;
		imx577->cur_pixel_rate = IMX577_4LANE_PIXEL_RATE_1050M_12BIT;
	}

	imx577->pixel_rate = v4l2_ctrl_new_std(handler, NULL,
					       V4L2_CID_PIXEL_RATE,
					       0, IMX577_PIXEL_RATE_MAX,
					       1, imx577->cur_pixel_rate);

	__v4l2_ctrl_s_ctrl(imx577->link_freq,
			   mode->link_freq_idx);

	h_blank = mode->hts_def - mode->width;
	imx577->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
				h_blank, h_blank, 1, h_blank);
	if (imx577->hblank)
		imx577->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vblank_def = mode->vts_def - mode->height;
	imx577->vblank = v4l2_ctrl_new_std(handler, &imx577_ctrl_ops,
				V4L2_CID_VBLANK, vblank_def,
				(imx577_is_2lane(imx577) ? IMX577_2LANE_VTS_MAX :
				 IMX577_4LANE_VTS_MAX) - mode->height,
				1, vblank_def);
	imx577->cur_vts = mode->vts_def;
	exposure_max = mode->vts_def - 22;
	imx577->exposure = v4l2_ctrl_new_std(handler, &imx577_ctrl_ops,
				V4L2_CID_EXPOSURE, IMX577_EXPOSURE_MIN,
				exposure_max, IMX577_EXPOSURE_STEP,
				mode->exp_def);

	imx577->anal_gain = v4l2_ctrl_new_std(handler, &imx577_ctrl_ops,
				V4L2_CID_ANALOGUE_GAIN, IMX577_GAIN_MIN,
				IMX577_GAIN_MAX, IMX577_GAIN_STEP,
				IMX577_GAIN_DEFAULT);

	if (imx577_is_2lane(imx577)) {
		test_pattern_menu = imx577_2lane_test_pattern_menu;
		test_pattern_count = ARRAY_SIZE(imx577_2lane_test_pattern_menu);
	} else {
		test_pattern_menu = imx577_4lane_test_pattern_menu;
		test_pattern_count = ARRAY_SIZE(imx577_4lane_test_pattern_menu);
	}
	imx577->test_pattern = v4l2_ctrl_new_std_menu_items(handler,
				&imx577_ctrl_ops, V4L2_CID_TEST_PATTERN,
				test_pattern_count - 1, 0, 0, test_pattern_menu);

	if (handler->error) {
		ret = handler->error;
		dev_err(&imx577->client->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	imx577->subdev.ctrl_handler = handler;
	imx577->has_init_exp = false;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int imx577_check_sensor_id(struct imx577 *imx577,
				   struct i2c_client *client)
{
	struct device *dev = &imx577->client->dev;
	u32 id = 0;
	int ret;

	ret = imx577_read_reg(client, IMX577_REG_CHIP_ID,
			       IMX577_REG_VALUE_16BIT, &id);
	if (id != CHIP_ID) {
		dev_err(dev, "Unexpected sensor id(%04x), ret(%d)\n", id, ret);
		return -ENODEV;
	}

	dev_info(dev, "Detected Sony imx%04x sensor\n", CHIP_ID);

	return 0;
}

static int imx577_configure_regulators(struct imx577 *imx577)
{
	unsigned int i;

	for (i = 0; i < IMX577_NUM_SUPPLIES; i++)
		imx577->supplies[i].supply = imx577_supply_names[i];

	return devm_regulator_bulk_get(&imx577->client->dev,
				       IMX577_NUM_SUPPLIES,
				       imx577->supplies);
}

static int imx577_parse_endpoint(struct imx577 *imx577)
{
	struct device *dev = &imx577->client->dev;
	struct device_node *endpoint;
	unsigned int i;
	int ret;

	endpoint = of_graph_get_next_endpoint(dev->of_node, NULL);
	if (!endpoint)
		return dev_err_probe(dev, -EINVAL, "missing sensor endpoint\n");

	imx577->bus_cfg.bus_type = V4L2_MBUS_CSI2_DPHY;
	ret = v4l2_fwnode_endpoint_alloc_parse(of_fwnode_handle(endpoint),
					       &imx577->bus_cfg);
	of_node_put(endpoint);
	if (ret)
		goto out;

	switch (imx577->bus_cfg.bus.mipi_csi2.num_data_lanes) {
	case 2:
		if (imx577->bus_cfg.nr_of_link_frequencies) {
			for (i = 0; i < imx577->bus_cfg.nr_of_link_frequencies; i++)
				if (imx577->bus_cfg.link_frequencies[i] ==
				    IMX577_2LANE_LINK_FREQ)
					break;
			if (i == imx577->bus_cfg.nr_of_link_frequencies) {
				ret = dev_err_probe(dev, -EINVAL,
						    "two-lane modes require a 450 MHz link\n");
				goto out;
			}
		}
		imx577->bus_cfg.bus.mipi_csi2.flags |=
			V4L2_MBUS_CSI2_NONCONTINUOUS_CLOCK;
		imx577->supported_modes = imx577_2lane_modes;
		imx577->cfg_num = ARRAY_SIZE(imx577_2lane_modes);
		imx577->link_freq_menu = imx577_2lane_link_freq_items;
		imx577->link_freq_menu_count = ARRAY_SIZE(imx577_2lane_link_freq_items);
		break;
	case 4:
		imx577->supported_modes = imx577_4lane_modes;
		imx577->cfg_num = ARRAY_SIZE(imx577_4lane_modes);
		imx577->link_freq_menu = imx577_4lane_link_freq_items;
		imx577->link_freq_menu_count = ARRAY_SIZE(imx577_4lane_link_freq_items);
		break;
	default:
		ret = dev_err_probe(dev, -EINVAL, "only 2 or 4 data lanes are supported\n");
		break;
	}

out:
	v4l2_fwnode_endpoint_free(&imx577->bus_cfg);
	return ret;
}

static int imx577_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct imx577 *imx577;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;
	u32 i, hdr_mode = 0;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		DRIVER_VERSION >> 16,
		(DRIVER_VERSION & 0xff00) >> 8,
		DRIVER_VERSION & 0x00ff);

	imx577 = devm_kzalloc(dev, sizeof(*imx577), GFP_KERNEL);
	if (!imx577)
		return -ENOMEM;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &imx577->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &imx577->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &imx577->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &imx577->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

	ret = of_property_read_u32(node, OF_CAMERA_HDR_MODE, &hdr_mode);
	if (ret) {
		hdr_mode = NO_HDR;
		dev_warn(dev, " Get hdr mode failed! no hdr default\n");
	}

	imx577->client = client;
	ret = imx577_parse_endpoint(imx577);
	if (ret)
		return ret;

	for (i = 0; i < imx577->cfg_num; i++) {
		if (hdr_mode == imx577->supported_modes[i].hdr_mode) {
			imx577->cur_mode = &imx577->supported_modes[i];
			break;
		}
	}
	if (i == imx577->cfg_num)
		return dev_err_probe(dev, -EINVAL,
			"HDR mode %u is not supported with %u lanes\n",
			hdr_mode, imx577->bus_cfg.bus.mipi_csi2.num_data_lanes);
	dev_info(dev, "%u data lanes, default %ux%u, link %lld Hz\n",
		 imx577->bus_cfg.bus.mipi_csi2.num_data_lanes,
		 imx577->cur_mode->width, imx577->cur_mode->height,
		 imx577->link_freq_menu[imx577->cur_mode->link_freq_idx]);

	/* xvclk is optional for modules with an always-on 24 MHz crystal. */
	imx577->xvclk = devm_clk_get_optional(dev, "xvclk");
	if (IS_ERR(imx577->xvclk))
		return dev_err_probe(dev, PTR_ERR(imx577->xvclk),
				     "Failed to get xvclk\n");

	imx577->power_gpio = devm_gpiod_get(dev, "power", GPIOD_OUT_LOW);
	if (IS_ERR(imx577->power_gpio))
		dev_warn(dev, "Failed to get power-gpios, maybe no use\n");

	imx577->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(imx577->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios\n");

	imx577->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(imx577->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");

	ret = imx577_configure_regulators(imx577);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	imx577->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(imx577->pinctrl)) {
		imx577->pins_default =
			pinctrl_lookup_state(imx577->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(imx577->pins_default))
			dev_err(dev, "could not get default pinstate\n");

		imx577->pins_sleep =
			pinctrl_lookup_state(imx577->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_SLEEP);
		if (IS_ERR(imx577->pins_sleep))
			dev_err(dev, "could not get sleep pinstate\n");
	}

	mutex_init(&imx577->mutex);

	sd = &imx577->subdev;
	v4l2_i2c_subdev_init(sd, client, &imx577_subdev_ops);
	ret = imx577_initialize_controls(imx577);
	if (ret)
		goto err_destroy_mutex;

	ret = __imx577_power_on(imx577);
	if (ret)
		goto err_free_handler;

	ret = imx577_check_sensor_id(imx577, client);
	if (ret)
		goto err_power_off;

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &imx577_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
		     V4L2_SUBDEV_FL_HAS_EVENTS;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	imx577->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &imx577->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(imx577->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 imx577->module_index, facing,
		 IMX577_NAME, dev_name(sd->dev));
	ret = v4l2_async_register_subdev_sensor(sd);
	if (ret) {
		dev_err(dev, "v4l2 async register subdev failed\n");
		goto err_clean_entity;
	}

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_idle(dev);

	return 0;

err_clean_entity:
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
err_power_off:
	__imx577_power_off(imx577);
err_free_handler:
	v4l2_ctrl_handler_free(&imx577->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&imx577->mutex);

	return ret;
}

static void imx577_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx577 *imx577 = to_imx577(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&imx577->ctrl_handler);
	mutex_destroy(&imx577->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__imx577_power_off(imx577);
	pm_runtime_set_suspended(&client->dev);
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id imx577_of_match[] = {
	{ .compatible = "sony,imx577" },
	{},
};
MODULE_DEVICE_TABLE(of, imx577_of_match);
#endif

static const struct i2c_device_id imx577_match_id[] = {
	{ "sony,imx577", 0 },
	{},
};

static struct i2c_driver imx577_i2c_driver = {
	.driver = {
		.name = IMX577_NAME,
		.pm = &imx577_pm_ops,
		.of_match_table = of_match_ptr(imx577_of_match),
	},
	.probe		= &imx577_probe,
	.remove		= &imx577_remove,
	.id_table	= imx577_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&imx577_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&imx577_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("Sony imx577 sensor driver");
MODULE_LICENSE("GPL");
