/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2021 MediaTek Inc.
 * Copyright (c) 2021 BayLibre, SAS
 */

#ifndef _MTK_HDMI_PHY_8195_H
#define _MTK_HDMI_PHY_8195_H

#include <linux/types.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>

#define DA_HDMITXPLL_ISO_EN BIT(1)
#define DA_HDMITXPLL_ISO_EN_SHIFT (1)
#define DA_HDMITXPLL_PWR_ON BIT(2)
#define DA_HDMITXPLL_PWR_ON_SHIFT (2)

#define HDMI20_CLK_CFG 0x70

#define HDMI_1_CFG_0 0x00
#define HDMI_1_CFG_1 0x04
#define HDMI_1_CFG_10 0x40
#define HDMI_1_CFG_2 0x08
#define HDMI_1_CFG_21 0x84
#define HDMI_1_CFG_22 0x88
#define HDMI_1_CFG_3 0x0c
#define HDMI_1_CFG_6 0x18
#define HDMI_1_CFG_9 0x24
#define HDMI_1_PLL_CFG_0 0x44
#define HDMI_1_PLL_CFG_1 0x48
#define HDMI_1_PLL_CFG_2 0x4c
#define HDMI_1_PLL_CFG_3 0x50
#define HDMI_1_PLL_CFG_4 0x54

#define HDMI_ANA_CTL 0x7c

#define HDMI_CTL_1 0xc4
#define HDMI_CTL_3 0xcc

#define REG_ANA_HDMI20_FIFO_EN BIT(16)

#define REG_HDMITXPLL_DIV GENMASK(4, 0)
#define REG_HDMITXPLL_DIV_SHIFT (0)
#define REG_HDMITX_PIXEL_CLOCK BIT(23)
#define REG_HDMITX_PIXEL_CLOCK_SHIFT (23)
#define REG_HDMITX_REF_RESPLL_SEL BIT(9)
#define REG_HDMITX_REF_RESPLL_SEL_SHIFT (9)
#define REG_HDMITX_REF_XTAL_SEL BIT(7)
#define REG_HDMITX_REF_XTAL_SEL_SHIFT (7)

#define REG_PIXEL_CLOCK_SEL BIT(10)
#define REG_PIXEL_CLOCK_SEL_SHIFT (10)

#define REG_TXC_DIV GENMASK(31, 30)
#define REG_TXC_DIV_SHIFT (30)

#define RG_HDMITX21_BG_PWD BIT(20)
#define RG_HDMITX21_BG_PWD_SHIFT (20)

#define RG_HDMITX21_BIAS_EN BIT(29)
#define RG_HDMITX21_BIAS_EN_SHIFT (29)
#define RG_HDMITX21_BIAS_PE_BG_VREF_SEL GENMASK(16, 15)
#define RG_HDMITX21_BIAS_PE_BG_VREF_SEL_SHIFT (15)
#define RG_HDMITX21_BIAS_PE_VREF_SELB GENMASK(10, 10)
#define RG_HDMITX21_BIAS_PE_VREF_SELB_SHIFT (10)

#define RG_HDMITX21_CKLDO_EN BIT(3)
#define RG_HDMITX21_CKLDO_EN_SHIFT (3)

#define RG_HDMITX21_CK_DRV_OP_EN BIT(11)
#define RG_HDMITX21_CK_DRV_OP_EN_SHIFT (11)

#define RG_HDMITX21_D0_DRV_OP_EN BIT(10)
#define RG_HDMITX21_D0_DRV_OP_EN_SHIFT (10)

#define RG_HDMITX21_D1_DRV_OP_EN BIT(9)
#define RG_HDMITX21_D1_DRV_OP_EN_SHIFT (9)

#define RG_HDMITX21_D2_DRV_OP_EN BIT(8)
#define RG_HDMITX21_D2_DRV_OP_EN_SHIFT (8)

#define RG_HDMITX21_DRV_EN GENMASK(27, 24)
#define RG_HDMITX21_DRV_EN_SHIFT (24)
#define RG_HDMITX21_DRV_IBIAS_CLK GENMASK(10, 5)
#define RG_HDMITX21_DRV_IBIAS_CLK_SHIFT (5)
#define RG_HDMITX21_DRV_IBIAS_D0 GENMASK(19, 14)
#define RG_HDMITX21_DRV_IBIAS_D0_FFE1 GENMASK(21, 17)
#define RG_HDMITX21_DRV_IBIAS_D0_FFE1_SHIFT (17)
#define RG_HDMITX21_DRV_IBIAS_D0_FFE2 GENMASK(23, 20)
#define RG_HDMITX21_DRV_IBIAS_D0_FFE2_SHIFT (20)
#define RG_HDMITX21_DRV_IBIAS_D0_SHIFT (14)
#define RG_HDMITX21_DRV_IBIAS_D1 GENMASK(25, 20)
#define RG_HDMITX21_DRV_IBIAS_D1_FFE1 GENMASK(26, 22)
#define RG_HDMITX21_DRV_IBIAS_D1_FFE1_SHIFT (22)
#define RG_HDMITX21_DRV_IBIAS_D1_FFE2 GENMASK(27, 24)
#define RG_HDMITX21_DRV_IBIAS_D1_FFE2_SHIFT (24)
#define RG_HDMITX21_DRV_IBIAS_D1_SHIFT (20)
#define RG_HDMITX21_DRV_IBIAS_D2 GENMASK(31, 26)
#define RG_HDMITX21_DRV_IBIAS_D2_FFE1 GENMASK(31, 27)
#define RG_HDMITX21_DRV_IBIAS_D2_FFE1_SHIFT (27)
#define RG_HDMITX21_DRV_IBIAS_D2_FFE2 GENMASK(31, 28)
#define RG_HDMITX21_DRV_IBIAS_D2_FFE2_SHIFT (28)
#define RG_HDMITX21_DRV_IBIAS_D2_SHIFT (26)
#define RG_HDMITX21_DRV_IMP_CLK_EN1 GENMASK(31, 26)
#define RG_HDMITX21_DRV_IMP_CLK_EN1_SHIFT (26)
#define RG_HDMITX21_DRV_IMP_D0_EN1 GENMASK(13, 8)
#define RG_HDMITX21_DRV_IMP_D0_EN1_SHIFT (8)
#define RG_HDMITX21_DRV_IMP_D1_EN1 GENMASK(19, 14)
#define RG_HDMITX21_DRV_IMP_D1_EN1_SHIFT (14)
#define RG_HDMITX21_DRV_IMP_D2_EN1 GENMASK(25, 20)
#define RG_HDMITX21_DRV_IMP_D2_EN1_SHIFT (20)
#define RG_HDMITX21_DRV_IMP_EN GENMASK(23, 20)
#define RG_HDMITX21_DRV_IMP_EN_SHIFT (20)

#define RG_HDMITX21_FRL_CK_EN BIT(13)
#define RG_HDMITX21_FRL_CK_EN_SHIFT (13)
#define RG_HDMITX21_FRL_D0_EN BIT(14)
#define RG_HDMITX21_FRL_D0_EN_SHIFT (14)
#define RG_HDMITX21_FRL_D1_EN BIT(15)
#define RG_HDMITX21_FRL_D1_EN_SHIFT (15)
#define RG_HDMITX21_FRL_D2_EN BIT(16)
#define RG_HDMITX21_FRL_D2_EN_SHIFT (16)
#define RG_HDMITX21_FRL_EN BIT(12)
#define RG_HDMITX21_FRL_EN_SHIFT (12)

#define RG_HDMITX21_INTR_CAL GENMASK(22, 18)
#define RG_HDMITX21_INTR_CAL_READOUT GENMASK(22, 18)
#define RG_HDMITX21_INTR_CAL_READOUT_SHIFT (18)
#define RG_HDMITX21_INTR_CAL_SHIFT (18)

#define RG_HDMITX21_SER_EN GENMASK(31, 28)
#define RG_HDMITX21_SER_EN_SHIFT (28)

#define RG_HDMITX21_SLDOLPF_EN BIT(7)
#define RG_HDMITX21_SLDOLPF_EN_SHIFT (7)
#define RG_HDMITX21_SLDO_EN GENMASK(11, 8)
#define RG_HDMITX21_SLDO_EN_SHIFT (8)
#define RG_HDMITX21_SLDO_VREF_SEL GENMASK(5, 4)
#define RG_HDMITX21_SLDO_VREF_SEL_SHIFT (4)

#define RG_HDMITX21_TX_POSDIV GENMASK(27, 26)
#define RG_HDMITX21_TX_POSDIV_EN GENMASK(28, 28)
#define RG_HDMITX21_TX_POSDIV_EN_SHIFT (28)
#define RG_HDMITX21_TX_POSDIV_SHIFT (26)

#define RG_HDMITX21_VREF_SEL BIT(4)
#define RG_HDMITX21_VREF_SEL_SHIFT (4)

#define RG_HDMITXPLL_BC GENMASK(28, 27)
#define RG_HDMITXPLL_BC_SHIFT (27)
#define RG_HDMITXPLL_BP GENMASK(13, 10)
#define RG_HDMITXPLL_BP2 BIT(30)
#define RG_HDMITXPLL_BP2_SHIFT (30)
#define RG_HDMITXPLL_BP_SHIFT (10)
#define RG_HDMITXPLL_BR GENMASK(21, 19)
#define RG_HDMITXPLL_BR_SHIFT (19)
#define RG_HDMITXPLL_DIV_CTRL GENMASK(25, 24)
#define RG_HDMITXPLL_DIV_CTRL_SHIFT (24)
#define RG_HDMITXPLL_FBKDIV_HIGH BIT(31)
#define RG_HDMITXPLL_FBKDIV_high_SHIFT (31)
#define RG_HDMITXPLL_FBKDIV_low (0xffffffff)
#define RG_HDMITXPLL_FBKDIV_low_SHIFT (0)
#define RG_HDMITXPLL_HIKVCO GENMASK(29, 29)
#define RG_HDMITXPLL_HIKVCO_SHIFT (29)
#define RG_HDMITXPLL_HREN GENMASK(13, 12)
#define RG_HDMITXPLL_HREN_SHIFT (12)
#define RG_HDMITXPLL_IBAND_FIX_EN GENMASK(24, 24)
#define RG_HDMITXPLL_IBAND_FIX_EN_SHIFT (24)
#define RG_HDMITXPLL_IC GENMASK(26, 22)
#define RG_HDMITXPLL_IC_SHIFT (22)
#define RG_HDMITXPLL_IR GENMASK(18, 14)
#define RG_HDMITXPLL_IR_SHIFT (14)
#define RG_HDMITXPLL_LVR_SEL GENMASK(27, 26)
#define RG_HDMITXPLL_LVR_SEL_SHIFT (26)
#define RG_HDMITXPLL_POSDIV GENMASK(23, 22)
#define RG_HDMITXPLL_POSDIV_DIV3_CTRL BIT(21)
#define RG_HDMITXPLL_POSDIV_DIV3_CTRL_SHIFT (21)
#define RG_HDMITXPLL_POSDIV_SHIFT (22)
#define RG_HDMITXPLL_PREDIV GENMASK(29, 28)
#define RG_HDMITXPLL_PREDIV_SHIFT (28)
#define RG_HDMITXPLL_PWD BIT(31)
#define RG_HDMITXPLL_PWD_SHIFT (31)
#define RG_HDMITXPLL_REF_CK_SEL GENMASK(2, 1)
#define RG_HDMITXPLL_REF_CK_SEL_SHIFT (1)
#define RG_HDMITXPLL_RESERVE GENMASK(15, 0)
#define RG_HDMITXPLL_RESERVE_BIT12_11 GENMASK(12, 11)
#define RG_HDMITXPLL_RESERVE_BIT12_11_SHIFT (11)
#define RG_HDMITXPLL_RESERVE_BIT13 BIT(13)
#define RG_HDMITXPLL_RESERVE_BIT13_SHIFT (13)
#define RG_HDMITXPLL_RESERVE_BIT14 BIT(14)
#define RG_HDMITXPLL_RESERVE_BIT14_SHIFT (14)
#define RG_HDMITXPLL_RESERVE_BIT1_0 GENMASK(1, 0)
#define RG_HDMITXPLL_RESERVE_BIT1_0_SHIFT (0)
#define RG_HDMITXPLL_RESERVE_BIT3_2 GENMASK(3, 2)
#define RG_HDMITXPLL_RESERVE_BIT3_2_SHIFT (2)
#define RG_HDMITXPLL_RESERVE_SHIFT (0)
#define RG_HDMITXPLL_TCL_EN BIT(31)
#define RG_HDMITXPLL_TCL_EN_SHIFT (31)

#define RG_INTR_IMP_RG_MODE GENMASK(7, 3)
#define RG_INTR_IMP_RG_MODE_SHIFT (3)

#endif /* MTK_HDMI_PHY_8195_H */