// SPDX-License-Identifier: GPL-2.0
/*
 * Qualcomm PCIe root complex driver
 *
 * Copyright (c) 2014-2015, The Linux Foundation. All rights reserved.
 * Copyright 2015 Linaro Limited.
 *
 * Author: Stanimir Varbanov <svarbanov@mm-sol.com>
 */

#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/crc8.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/devcoredump.h>
#include <linux/gpio/consumer.h>
#include <linux/interconnect.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/limits.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/of_pci.h>
#include <linux/pci.h>
#include <linux/pci-ecam.h>
#include <linux/pci-pwrctrl.h>
#include <linux/pm_opp.h>
#include <linux/pm_runtime.h>
#include <linux/platform_device.h>
#include <linux/phy/pcie.h>
#include <linux/phy/phy.h>
#include <linux/regulator/consumer.h>
#include <linux/reset.h>
#include <linux/reboot.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/units.h>
#include <linux/workqueue.h>

#include "../../pci.h"
#include "../pci-host-common.h"
#include "pcie-designware.h"
#include "pcie-qcom-common.h"

/* PARF registers */
#define PARF_SYS_CTRL				0x00
#define PARF_PM_CTRL				0x20
#define PARF_PM_STTS				0x24
#define PARF_PM_STTS_1				0x28
#define PARF_PCS_DEEMPH				0x34
#define PARF_PCS_SWING				0x38
#define PARF_PHY_CTRL				0x40
#define PARF_PHY_REFCLK				0x4c
#define PARF_CONFIG_BITS			0x50
#define PARF_DBI_BASE_ADDR			0x168
#define PARF_SLV_ADDR_SPACE_SIZE		0x16c
#define PARF_MHI_CLOCK_RESET_CTRL		0x174
#define PARF_AXI_MSTR_WR_ADDR_HALT		0x178
#define PARF_AXI_MSTR_WR_ADDR_HALT_V2		0x1a8
#define PARF_Q2A_FLUSH				0x1ac
#define PARF_LTSSM				0x1b0
#define PARF_INT_ALL_STATUS			0x224
#define PARF_INT_ALL_CLEAR			0x228
#define PARF_INT_ALL_MASK			0x22c
#define PARF_STATUS				0x230
#define PARF_SID_OFFSET				0x234
#define PARF_BDF_TRANSLATE_CFG			0x24c
#define PARF_CLKREQ_OVERRIDE			0x2b0
#define PARF_DBI_BASE_ADDR_V2			0x350
#define PARF_DBI_BASE_ADDR_V2_HI		0x354
#define PARF_SLV_ADDR_SPACE_SIZE_V2		0x358
#define PARF_SLV_ADDR_SPACE_SIZE_V2_HI		0x35c
#define PARF_NO_SNOOP_OVERRIDE			0x3d4
#define PARF_CORE_ERRORS			0x3c0
#define PARF_L1SS_SLEEP_MODE_HANDLER_STATUS	0x4d0
#define PARF_L1SS_SLEEP_MODE_HANDLER_CFG	0x4d4
#define PARF_INT_ALL_2_STATUS			0x500
#define PARF_LINK_DOWN_ECAM_BLOCK		0x608
#define PARF_LINK_DOWN_ECAM_BLOCK_CMD		0x60c
#define PARF_LINK_DOWN_ECAM_BLOCK_STATUS		0x610
#define PARF_LINK_DOWN_AXI_ECAM_BLOCK_STATUS	0x630
#define PARF_ATU_BASE_ADDR			0x634
#define PARF_ATU_BASE_ADDR_HI			0x638
#define PARF_DEVICE_TYPE			0x1000
#define PARF_BDF_TO_SID_TABLE_N			0x2000
#define PARF_BDF_TO_SID_CFG			0x2c00

/*
 * ECAM blocker range registers. The blocked range has a write pair
 * (WR_BASE/WR_LIMIT) and a read pair (RD_BASE/RD_LIMIT); each address is
 * split into a low (32-bit) and a HI (upper 32-bit) register.
 */
#define PARF_BLOCK_SLV_AXI_WR_BASE		0x360
#define PARF_BLOCK_SLV_AXI_WR_BASE_HI		0x364
#define PARF_BLOCK_SLV_AXI_WR_LIMIT		0x368
#define PARF_BLOCK_SLV_AXI_WR_LIMIT_HI		0x36c
#define PARF_BLOCK_SLV_AXI_RD_BASE		0x370
#define PARF_BLOCK_SLV_AXI_RD_BASE_HI		0x374
#define PARF_BLOCK_SLV_AXI_RD_LIMIT		0x378
#define PARF_BLOCK_SLV_AXI_RD_LIMIT_HI		0x37c

#define PARF_ECAM_BASE				0x380
#define PARF_ECAM_BASE_HI			0x384

/* ELBI registers */
#define ELBI_SYS_CTRL				0x04

/* DBI registers */
#define AXI_MSTR_RESP_COMP_CTRL0		0x818
#define AXI_MSTR_RESP_COMP_CTRL1		0x81c

/* MHI registers */
#define PARF_DEBUG_CNT_PM_LINKST_IN_L2		0xc04
#define PARF_DEBUG_CNT_PM_LINKST_IN_L1SUB	0xc08
#define PARF_DEBUG_CNT_PM_LINKST_IN_L1		0xc0c
#define PARF_DEBUG_CNT_PM_LINKST_IN_L0S		0xc10
#define PARF_DEBUG_CNT_AUX_CLK_APP_REQ_EXIT_L1		0xc14
#define PARF_DEBUG_CNT_AUX_CLK_APP_READY_ENTER_L23	0xc18
#define PARF_DEBUG_CNT_AUX_CLK_APP_READY_ENTER_L1	0xc1c
#define PARF_DEBUG_CNT_AUX_CLK_IN_L1SUB_L1	0xc84
#define PARF_DEBUG_CNT_AUX_CLK_IN_L1SUB_L2	0xc88

/* PARF_SYS_CTRL register fields */
#define MAC_PHY_POWERDOWN_IN_P2_D_MUX_EN	BIT(29)
#define ECAM_BLOCKER_EN				BIT(26)
#define MST_WAKEUP_EN				BIT(13)
#define SLV_WAKEUP_EN				BIT(12)
#define MSTR_ACLK_CGC_DIS			BIT(10)
#define SLV_ACLK_CGC_DIS			BIT(9)
#define CORE_CLK_CGC_DIS			BIT(6)
#define AUX_PWR_DET				BIT(4)
#define L23_CLK_RMV_DIS				BIT(2)
#define L1_CLK_RMV_DIS				BIT(1)

/* PARF_PM_CTRL register fields */
#define REQ_NOT_ENTR_L1				BIT(5)

/* PARF_PCS_DEEMPH register fields */
#define PCS_DEEMPH_TX_DEEMPH_GEN1(x)		FIELD_PREP(GENMASK(21, 16), x)
#define PCS_DEEMPH_TX_DEEMPH_GEN2_3_5DB(x)	FIELD_PREP(GENMASK(13, 8), x)
#define PCS_DEEMPH_TX_DEEMPH_GEN2_6DB(x)	FIELD_PREP(GENMASK(5, 0), x)

/* PARF_PCS_SWING register fields */
#define PCS_SWING_TX_SWING_FULL(x)		FIELD_PREP(GENMASK(14, 8), x)
#define PCS_SWING_TX_SWING_LOW(x)		FIELD_PREP(GENMASK(6, 0), x)

/* PARF_PHY_CTRL register fields */
#define PHY_CTRL_PHY_TX0_TERM_OFFSET_MASK	GENMASK(20, 16)
#define PHY_CTRL_PHY_TX0_TERM_OFFSET(x)		FIELD_PREP(PHY_CTRL_PHY_TX0_TERM_OFFSET_MASK, x)
#define PHY_TEST_PWR_DOWN			BIT(0)

/* PARF_PHY_REFCLK register fields */
#define PHY_REFCLK_SSP_EN			BIT(16)
#define PHY_REFCLK_USE_PAD			BIT(12)

/* PARF_CONFIG_BITS register fields */
#define PHY_RX0_EQ(x)				FIELD_PREP(GENMASK(26, 24), x)

/* PARF_SLV_ADDR_SPACE_SIZE register value */
#define SLV_ADDR_SPACE_SZ			0x80000000

/* PARF_MHI_CLOCK_RESET_CTRL register fields */
#define AHB_CLK_EN				BIT(0)
#define MSTR_AXI_CLK_EN				BIT(1)
#define BYPASS					BIT(4)

/* PARF_AXI_MSTR_WR_ADDR_HALT register fields */
#define EN					BIT(31)

/* PARF_LTSSM register fields */
#define LTSSM_EN				BIT(8)
#define SW_CLEAR_FLUSH_MODE			BIT(10)
#define FLUSH_MODE				BIT(11)

/* PARF_INT_ALL_{STATUS/CLEAR/MASK} register fields */
#define INT_ALL_LINK_DOWN			1
#define PARF_INT_ALL_LINK_DOWN			BIT(INT_ALL_LINK_DOWN)
#define PARF_INT_LINK_REQ_RST_FLUSH		BIT(12)

/* PARF_LINK_DOWN_ECAM_BLOCK register fields */
#define LINK_DOWN_ECAM_IGNORE_LINK_DOWN		BIT(5)
#define LINK_DOWN_ECAM_CLK_REQ_OVERRIDE_VAL	BIT(8)
#define LINK_DOWN_ECAM_CLK_REQ_OVERRIDE		BIT(7)
#define LINK_DOWN_ECAM_BLOCK_DBI_EN		BIT(2)
#define LINK_DOWN_ECAM_AUTO_RELEASE_EN		BIT(1)
#define LINK_DOWN_ECAM_AUTO_BLOCK_EN		BIT(0)

/* PARF_LINK_DOWN_ECAM_BLOCK_CMD register fields */
#define LINK_DOWN_ECAM_SW_BLOCK_TRAFFIC		BIT(2)

/* PARF_NO_SNOOP_OVERRIDE register fields */
#define WR_NO_SNOOP_OVERRIDE_EN			BIT(1)
#define RD_NO_SNOOP_OVERRIDE_EN			BIT(3)

/* PARF_DEVICE_TYPE register fields */
#define DEVICE_TYPE_RC				0x4

/* PARF_BDF_TO_SID_CFG fields */
#define BDF_TO_SID_BYPASS			BIT(0)

/* PARF_STATUS fields */
#define FLUSH_COMPLETED				BIT(8)

/* ELBI_SYS_CTRL register fields */
#define ELBI_SYS_CTRL_LT_ENABLE			BIT(0)

/* AXI_MSTR_RESP_COMP_CTRL0 register fields */
#define CFG_REMOTE_RD_REQ_BRIDGE_SIZE_2K	0x4
#define CFG_REMOTE_RD_REQ_BRIDGE_SIZE_4K	0x5

/* AXI_MSTR_RESP_COMP_CTRL1 register fields */
#define CFG_BRIDGE_SB_INIT			BIT(0)

/* PCI_EXP_SLTCAP register fields */
#define PCIE_CAP_SLOT_POWER_LIMIT_VAL		FIELD_PREP(PCI_EXP_SLTCAP_SPLV, 250)
#define PCIE_CAP_SLOT_POWER_LIMIT_SCALE		FIELD_PREP(PCI_EXP_SLTCAP_SPLS, 1)
#define PCIE_CAP_SLOT_VAL			(PCI_EXP_SLTCAP_ABP | \
						PCI_EXP_SLTCAP_PCP | \
						PCI_EXP_SLTCAP_MRLSP | \
						PCI_EXP_SLTCAP_AIP | \
						PCI_EXP_SLTCAP_PIP | \
						PCI_EXP_SLTCAP_HPS | \
						PCI_EXP_SLTCAP_EIP | \
						PCIE_CAP_SLOT_POWER_LIMIT_VAL | \
						PCIE_CAP_SLOT_POWER_LIMIT_SCALE)

#define PERST_DELAY_US				1000
#define LINK_DOWN_TSTOP_US			1000
#define LINK_DOWN_FLUSH_TIMEOUT_US		100000
#define LINK_REQ_RST_TIMEOUT_MS			100

/* Dump buffer size for the link-down register dump */
#define QCOM_PCIE_DUMP_BUF_SIZE			SZ_8K

#define QCOM_PCIE_CRC8_POLYNOMIAL		(BIT(2) | BIT(1) | BIT(0))

#define QCOM_PCIE_LINK_SPEED_TO_BW(speed) \
		Mbps_to_icc(PCIE_SPEED2MBS_ENC(pcie_link_speed[speed]))

#define QCOM_PCIE_DUMP_REGS_PER_LINE	4

static const u32 qcom_pcie_parf_dump_regs[] = {
	PARF_LTSSM,
	PARF_INT_ALL_STATUS,
	PARF_SYS_CTRL,
};

static const u32 qcom_pcie_ext_parf_dump_regs[] = {
	PARF_LTSSM,
	PARF_PM_STTS,
	PARF_PM_STTS_1,
	PARF_INT_ALL_STATUS,
	PARF_INT_ALL_2_STATUS,
	PARF_CLKREQ_OVERRIDE,
	PARF_L1SS_SLEEP_MODE_HANDLER_STATUS,
	PARF_L1SS_SLEEP_MODE_HANDLER_CFG,
	PARF_CORE_ERRORS,
	PARF_LINK_DOWN_AXI_ECAM_BLOCK_STATUS,
	PARF_STATUS,
	PARF_SYS_CTRL,
};

static const u32 qcom_pcie_mhi_dump_regs[] = {
	PARF_DEBUG_CNT_PM_LINKST_IN_L2,
	PARF_DEBUG_CNT_PM_LINKST_IN_L1SUB,
	PARF_DEBUG_CNT_PM_LINKST_IN_L1,
	PARF_DEBUG_CNT_PM_LINKST_IN_L0S,
	PARF_DEBUG_CNT_AUX_CLK_APP_REQ_EXIT_L1,
	PARF_DEBUG_CNT_AUX_CLK_APP_READY_ENTER_L23,
	PARF_DEBUG_CNT_AUX_CLK_APP_READY_ENTER_L1,
	PARF_DEBUG_CNT_AUX_CLK_IN_L1SUB_L1,
	PARF_DEBUG_CNT_AUX_CLK_IN_L1SUB_L2,
};

struct qcom_pcie_resources_1_0_0 {
	struct clk_bulk_data *clks;
	int num_clks;
	struct reset_control *core;
	struct regulator *vdda;
};

#define QCOM_PCIE_2_1_0_MAX_RESETS		6
#define QCOM_PCIE_2_1_0_MAX_SUPPLY		3
struct qcom_pcie_resources_2_1_0 {
	struct clk_bulk_data *clks;
	int num_clks;
	struct reset_control_bulk_data resets[QCOM_PCIE_2_1_0_MAX_RESETS];
	int num_resets;
	struct regulator_bulk_data supplies[QCOM_PCIE_2_1_0_MAX_SUPPLY];
};

#define QCOM_PCIE_2_3_2_MAX_SUPPLY		2
struct qcom_pcie_resources_2_3_2 {
	struct clk_bulk_data *clks;
	int num_clks;
	struct regulator_bulk_data supplies[QCOM_PCIE_2_3_2_MAX_SUPPLY];
};

#define QCOM_PCIE_2_3_3_MAX_RESETS		7
struct qcom_pcie_resources_2_3_3 {
	struct clk_bulk_data *clks;
	int num_clks;
	struct reset_control_bulk_data rst[QCOM_PCIE_2_3_3_MAX_RESETS];
};

#define QCOM_PCIE_2_4_0_MAX_RESETS		12
struct qcom_pcie_resources_2_4_0 {
	struct clk_bulk_data *clks;
	int num_clks;
	struct reset_control_bulk_data resets[QCOM_PCIE_2_4_0_MAX_RESETS];
	int num_resets;
};

#define QCOM_PCIE_2_7_0_MAX_SUPPLIES		2
struct qcom_pcie_resources_2_7_0 {
	struct clk_bulk_data *clks;
	int num_clks;
	struct regulator_bulk_data supplies[QCOM_PCIE_2_7_0_MAX_SUPPLIES];
	struct reset_control *rst;
};

struct qcom_pcie_resources_2_9_0 {
	struct clk_bulk_data *clks;
	int num_clks;
	struct reset_control *rst;
};

union qcom_pcie_resources {
	struct qcom_pcie_resources_1_0_0 v1_0_0;
	struct qcom_pcie_resources_2_1_0 v2_1_0;
	struct qcom_pcie_resources_2_3_2 v2_3_2;
	struct qcom_pcie_resources_2_3_3 v2_3_3;
	struct qcom_pcie_resources_2_4_0 v2_4_0;
	struct qcom_pcie_resources_2_7_0 v2_7_0;
	struct qcom_pcie_resources_2_9_0 v2_9_0;
};

struct qcom_pcie;

struct qcom_pcie_ops {
	int (*get_resources)(struct qcom_pcie *pcie);
	int (*init)(struct qcom_pcie *pcie);
	int (*post_init)(struct qcom_pcie *pcie);
	void (*host_post_init)(struct qcom_pcie *pcie);
	void (*deinit)(struct qcom_pcie *pcie);
	void (*ltssm_enable)(struct qcom_pcie *pcie);
	int (*config_sid)(struct qcom_pcie *pcie);
};

/**
 * struct qcom_pcie_cfg - Per SoC config struct
 * @ops: qcom PCIe ops structure
 * @override_no_snoop: Override NO_SNOOP attribute in TLP to enable cache
 * snooping
 * @firmware_managed: Set if the Root Complex is firmware managed
 * @no_l0s: Set if the controller does not support the L0s link state
 * @has_ext_parf_regs: Set if the PARF block implements the extended
 * register set used by qcom_pcie_dump_regs().
 * @has_link_down_recovery: Set if the controller implements the Link Down
 * ECAM blocker and Q2A flush sequence.
 */
struct qcom_pcie_cfg {
	const struct qcom_pcie_ops *ops;
	bool override_no_snoop;
	bool firmware_managed;
	bool no_l0s;
	bool has_ext_parf_regs;
	bool has_link_down_recovery;
};

struct qcom_pcie_perst {
	struct list_head list;
	struct gpio_desc *desc;
};

struct qcom_pcie_port {
	struct list_head list;
	struct phy *phy;
	struct list_head perst;
};

struct qcom_pcie {
	struct dw_pcie *pci;
	void __iomem *parf;			/* DT parf */
	void __iomem *mhi;
	union qcom_pcie_resources res;
	struct icc_path *icc_mem;
	struct icc_path *icc_cpu;
	const struct qcom_pcie_cfg *cfg;
	struct dentry *debugfs;
	struct work_struct link_down_work;
	struct completion link_req_rst;
	struct notifier_block reboot_nb;
	struct reset_control *link_down_reset;
	struct list_head ports;
	atomic_long_t global_irq_status;
	int global_irq;
	bool global_irq_enabled;
	bool shutting_down;
	bool suspended;
	bool use_pm_opp;
};

#define to_qcom_pcie(x)		dev_get_drvdata((x)->dev)
static int qcom_pcie_reset_root_port(struct pci_host_bridge *bridge,
				  struct pci_dev *pdev);

static void __qcom_pcie_perst_assert(struct qcom_pcie *pcie, bool assert)
{
	struct qcom_pcie_perst *perst;
	struct qcom_pcie_port *port;
	int val = assert ? 1 : 0;

	list_for_each_entry(port, &pcie->ports, list) {
		list_for_each_entry(perst, &port->perst, list)
			gpiod_set_value_cansleep(perst->desc, val);
	}

	usleep_range(PERST_DELAY_US, PERST_DELAY_US + 500);
}

static void qcom_pcie_perst_assert(struct qcom_pcie *pcie)
{
	__qcom_pcie_perst_assert(pcie, true);
}

static void qcom_pcie_perst_deassert(struct qcom_pcie *pcie)
{
	/* Ensure that PERST# has been asserted for at least 100 ms */
	msleep(PCIE_T_PVPERL_MS);
	__qcom_pcie_perst_assert(pcie, false);
}

static int qcom_pcie_start_link(struct dw_pcie *pci)
{
	struct qcom_pcie *pcie = to_qcom_pcie(pci);

	qcom_pcie_common_set_equalization(pci);

	if (pcie_link_speed[pci->max_link_speed] == PCIE_SPEED_16_0GT)
		qcom_pcie_common_set_16gt_lane_margining(pci);

	/* Enable Link Training state machine */
	if (pcie->cfg->ops->ltssm_enable)
		pcie->cfg->ops->ltssm_enable(pcie);

	return 0;
}

static void qcom_pcie_clear_aspm_l0s(struct dw_pcie *pci)
{
	struct qcom_pcie *pcie = to_qcom_pcie(pci);
	u16 offset;
	u32 val;

	if (!pcie->cfg->no_l0s)
		return;

	offset = dw_pcie_find_capability(pci, PCI_CAP_ID_EXP);

	dw_pcie_dbi_ro_wr_en(pci);

	val = readl(pci->dbi_base + offset + PCI_EXP_LNKCAP);
	val &= ~PCI_EXP_LNKCAP_ASPM_L0S;
	writel(val, pci->dbi_base + offset + PCI_EXP_LNKCAP);

	dw_pcie_dbi_ro_wr_dis(pci);
}

static void qcom_pcie_set_slot_nccs(struct dw_pcie *pci)
{
	u16 offset = dw_pcie_find_capability(pci, PCI_CAP_ID_EXP);
	u32 val;

	dw_pcie_dbi_ro_wr_en(pci);

	/*
	 * Qcom PCIe Root Ports do not support generating command completion
	 * notifications for the Hot-Plug commands. So set the NCCS field to
	 * avoid waiting for the completions.
	 */
	val = readl(pci->dbi_base + offset + PCI_EXP_SLTCAP);
	val |= PCI_EXP_SLTCAP_NCCS;
	writel(val, pci->dbi_base + offset + PCI_EXP_SLTCAP);

	dw_pcie_dbi_ro_wr_dis(pci);
}

static void qcom_pcie_configure_dbi_base(struct qcom_pcie *pcie)
{
	struct dw_pcie *pci = pcie->pci;

	if (pci->dbi_phys_addr) {
		/*
		 * PARF_DBI_BASE_ADDR register is in CPU domain and require to
		 * be programmed with CPU physical address.
		 */
		writel(lower_32_bits(pci->dbi_phys_addr), pcie->parf +
							PARF_DBI_BASE_ADDR);
		writel(SLV_ADDR_SPACE_SZ, pcie->parf +
						PARF_SLV_ADDR_SPACE_SIZE);
	}
}

static void qcom_pcie_configure_dbi_atu_base(struct qcom_pcie *pcie)
{
	struct dw_pcie *pci = pcie->pci;

	if (pci->dbi_phys_addr) {
		/*
		 * PARF_DBI_BASE_ADDR_V2 and PARF_ATU_BASE_ADDR registers are
		 * in CPU domain and require to be programmed with CPU
		 * physical addresses.
		 */
		writel(lower_32_bits(pci->dbi_phys_addr), pcie->parf +
							PARF_DBI_BASE_ADDR_V2);
		writel(upper_32_bits(pci->dbi_phys_addr), pcie->parf +
						PARF_DBI_BASE_ADDR_V2_HI);

		if (pci->atu_phys_addr) {
			writel(lower_32_bits(pci->atu_phys_addr), pcie->parf +
							PARF_ATU_BASE_ADDR);
			writel(upper_32_bits(pci->atu_phys_addr), pcie->parf +
							PARF_ATU_BASE_ADDR_HI);
		}

		writel(0x0, pcie->parf + PARF_SLV_ADDR_SPACE_SIZE_V2);
		writel(SLV_ADDR_SPACE_SZ, pcie->parf +
					PARF_SLV_ADDR_SPACE_SIZE_V2_HI);
	}
}

static void qcom_pcie_2_1_0_ltssm_enable(struct qcom_pcie *pcie)
{
	struct dw_pcie *pci = pcie->pci;
	u32 val;

	if (!pci->elbi_base) {
		dev_err(pci->dev, "ELBI is not present\n");
		return;
	}
	/* enable link training */
	val = readl(pci->elbi_base + ELBI_SYS_CTRL);
	val |= ELBI_SYS_CTRL_LT_ENABLE;
	writel(val, pci->elbi_base + ELBI_SYS_CTRL);
}

static int qcom_pcie_get_resources_2_1_0(struct qcom_pcie *pcie)
{
	struct qcom_pcie_resources_2_1_0 *res = &pcie->res.v2_1_0;
	struct dw_pcie *pci = pcie->pci;
	struct device *dev = pci->dev;
	bool is_apq = of_device_is_compatible(dev->of_node, "qcom,pcie-apq8064");
	int ret;

	res->supplies[0].supply = "vdda";
	res->supplies[1].supply = "vdda_phy";
	res->supplies[2].supply = "vdda_refclk";
	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(res->supplies),
				      res->supplies);
	if (ret)
		return ret;

	res->num_clks = devm_clk_bulk_get_all(dev, &res->clks);
	if (res->num_clks < 0) {
		dev_err(dev, "Failed to get clocks\n");
		return res->num_clks;
	}

	res->resets[0].id = "pci";
	res->resets[1].id = "axi";
	res->resets[2].id = "ahb";
	res->resets[3].id = "por";
	res->resets[4].id = "phy";
	res->resets[5].id = "ext";

	/* ext is optional on APQ8016 */
	res->num_resets = is_apq ? 5 : 6;
	ret = devm_reset_control_bulk_get_exclusive(dev, res->num_resets, res->resets);
	if (ret < 0)
		return ret;

	return 0;
}

static void qcom_pcie_deinit_2_1_0(struct qcom_pcie *pcie)
{
	struct qcom_pcie_resources_2_1_0 *res = &pcie->res.v2_1_0;

	clk_bulk_disable_unprepare(res->num_clks, res->clks);
	reset_control_bulk_assert(res->num_resets, res->resets);

	writel(1, pcie->parf + PARF_PHY_CTRL);

	regulator_bulk_disable(ARRAY_SIZE(res->supplies), res->supplies);
}

static int qcom_pcie_init_2_1_0(struct qcom_pcie *pcie)
{
	struct qcom_pcie_resources_2_1_0 *res = &pcie->res.v2_1_0;
	struct dw_pcie *pci = pcie->pci;
	struct device *dev = pci->dev;
	int ret;

	/* reset the PCIe interface as uboot can leave it undefined state */
	ret = reset_control_bulk_assert(res->num_resets, res->resets);
	if (ret < 0) {
		dev_err(dev, "cannot assert resets\n");
		return ret;
	}

	ret = regulator_bulk_enable(ARRAY_SIZE(res->supplies), res->supplies);
	if (ret < 0) {
		dev_err(dev, "cannot enable regulators\n");
		return ret;
	}

	ret = reset_control_bulk_deassert(res->num_resets, res->resets);
	if (ret < 0) {
		dev_err(dev, "cannot deassert resets\n");
		regulator_bulk_disable(ARRAY_SIZE(res->supplies), res->supplies);
		return ret;
	}

	return 0;
}

static int qcom_pcie_post_init_2_1_0(struct qcom_pcie *pcie)
{
	struct qcom_pcie_resources_2_1_0 *res = &pcie->res.v2_1_0;
	struct dw_pcie *pci = pcie->pci;
	struct device *dev = pci->dev;
	struct device_node *node = dev->of_node;
	u32 val;
	int ret;

	/* enable PCIe clocks and resets */
	val = readl(pcie->parf + PARF_PHY_CTRL);
	val &= ~PHY_TEST_PWR_DOWN;
	writel(val, pcie->parf + PARF_PHY_CTRL);

	ret = clk_bulk_prepare_enable(res->num_clks, res->clks);
	if (ret)
		return ret;

	if (of_device_is_compatible(node, "qcom,pcie-ipq8064") ||
	    of_device_is_compatible(node, "qcom,pcie-ipq8064-v2")) {
		writel(PCS_DEEMPH_TX_DEEMPH_GEN1(24) |
			       PCS_DEEMPH_TX_DEEMPH_GEN2_3_5DB(24) |
			       PCS_DEEMPH_TX_DEEMPH_GEN2_6DB(34),
		       pcie->parf + PARF_PCS_DEEMPH);
		writel(PCS_SWING_TX_SWING_FULL(120) |
			       PCS_SWING_TX_SWING_LOW(120),
		       pcie->parf + PARF_PCS_SWING);
		writel(PHY_RX0_EQ(4), pcie->parf + PARF_CONFIG_BITS);
	}

	if (of_device_is_compatible(node, "qcom,pcie-ipq8064")) {
		/* set TX termination offset */
		val = readl(pcie->parf + PARF_PHY_CTRL);
		val &= ~PHY_CTRL_PHY_TX0_TERM_OFFSET_MASK;
		val |= PHY_CTRL_PHY_TX0_TERM_OFFSET(7);
		writel(val, pcie->parf + PARF_PHY_CTRL);
	}

	/* enable external reference clock */
	val = readl(pcie->parf + PARF_PHY_REFCLK);
	/* USE_PAD is required only for ipq806x */
	if (!of_device_is_compatible(node, "qcom,pcie-apq8064"))
		val &= ~PHY_REFCLK_USE_PAD;
	val |= PHY_REFCLK_SSP_EN;
	writel(val, pcie->parf + PARF_PHY_REFCLK);

	/* wait for clock acquisition */
	usleep_range(1000, 1500);

	/* Set the Max TLP size to 2K, instead of using default of 4K */
	writel(CFG_REMOTE_RD_REQ_BRIDGE_SIZE_2K,
	       pci->dbi_base + AXI_MSTR_RESP_COMP_CTRL0);
	writel(CFG_BRIDGE_SB_INIT,
	       pci->dbi_base + AXI_MSTR_RESP_COMP_CTRL1);

	qcom_pcie_set_slot_nccs(pcie->pci);

	return 0;
}

static int qcom_pcie_get_resources_1_0_0(struct qcom_pcie *pcie)
{
	struct qcom_pcie_resources_1_0_0 *res = &pcie->res.v1_0_0;
	struct dw_pcie *pci = pcie->pci;
	struct device *dev = pci->dev;

	res->vdda = devm_regulator_get(dev, "vdda");
	if (IS_ERR(res->vdda))
		return PTR_ERR(res->vdda);

	res->num_clks = devm_clk_bulk_get_all(dev, &res->clks);
	if (res->num_clks < 0) {
		dev_err(dev, "Failed to get clocks\n");
		return res->num_clks;
	}

	res->core = devm_reset_control_get_exclusive(dev, "core");
	return PTR_ERR_OR_ZERO(res->core);
}

static void qcom_pcie_deinit_1_0_0(struct qcom_pcie *pcie)
{
	struct qcom_pcie_resources_1_0_0 *res = &pcie->res.v1_0_0;

	reset_control_assert(res->core);
	clk_bulk_disable_unprepare(res->num_clks, res->clks);
	regulator_disable(res->vdda);
}

static int qcom_pcie_init_1_0_0(struct qcom_pcie *pcie)
{
	struct qcom_pcie_resources_1_0_0 *res = &pcie->res.v1_0_0;
	struct dw_pcie *pci = pcie->pci;
	struct device *dev = pci->dev;
	int ret;

	ret = reset_control_deassert(res->core);
	if (ret) {
		dev_err(dev, "cannot deassert core reset\n");
		return ret;
	}

	ret = clk_bulk_prepare_enable(res->num_clks, res->clks);
	if (ret) {
		dev_err(dev, "cannot prepare/enable clocks\n");
		goto err_assert_reset;
	}

	ret = regulator_enable(res->vdda);
	if (ret) {
		dev_err(dev, "cannot enable vdda regulator\n");
		goto err_disable_clks;
	}

	return 0;

err_disable_clks:
	clk_bulk_disable_unprepare(res->num_clks, res->clks);
err_assert_reset:
	reset_control_assert(res->core);

	return ret;
}

static int qcom_pcie_post_init_1_0_0(struct qcom_pcie *pcie)
{
	qcom_pcie_configure_dbi_base(pcie);

	if (IS_ENABLED(CONFIG_PCI_MSI)) {
		u32 val = readl(pcie->parf + PARF_AXI_MSTR_WR_ADDR_HALT);

		val |= EN;
		writel(val, pcie->parf + PARF_AXI_MSTR_WR_ADDR_HALT);
	}

	qcom_pcie_set_slot_nccs(pcie->pci);

	return 0;
}

static void qcom_pcie_2_3_2_ltssm_enable(struct qcom_pcie *pcie)
{
	u32 val;

	/* enable link training */
	val = readl(pcie->parf + PARF_LTSSM);
	val |= LTSSM_EN;
	writel(val, pcie->parf + PARF_LTSSM);
}

static int qcom_pcie_get_resources_2_3_2(struct qcom_pcie *pcie)
{
	struct qcom_pcie_resources_2_3_2 *res = &pcie->res.v2_3_2;
	struct dw_pcie *pci = pcie->pci;
	struct device *dev = pci->dev;
	int ret;

	res->supplies[0].supply = "vdda";
	res->supplies[1].supply = "vddpe-3v3";
	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(res->supplies),
				      res->supplies);
	if (ret)
		return ret;

	res->num_clks = devm_clk_bulk_get_all(dev, &res->clks);
	if (res->num_clks < 0) {
		dev_err(dev, "Failed to get clocks\n");
		return res->num_clks;
	}

	return 0;
}

static void qcom_pcie_deinit_2_3_2(struct qcom_pcie *pcie)
{
	struct qcom_pcie_resources_2_3_2 *res = &pcie->res.v2_3_2;

	clk_bulk_disable_unprepare(res->num_clks, res->clks);
	regulator_bulk_disable(ARRAY_SIZE(res->supplies), res->supplies);
}

static int qcom_pcie_init_2_3_2(struct qcom_pcie *pcie)
{
	struct qcom_pcie_resources_2_3_2 *res = &pcie->res.v2_3_2;
	struct dw_pcie *pci = pcie->pci;
	struct device *dev = pci->dev;
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(res->supplies), res->supplies);
	if (ret < 0) {
		dev_err(dev, "cannot enable regulators\n");
		return ret;
	}

	ret = clk_bulk_prepare_enable(res->num_clks, res->clks);
	if (ret) {
		dev_err(dev, "cannot prepare/enable clocks\n");
		regulator_bulk_disable(ARRAY_SIZE(res->supplies), res->supplies);
		return ret;
	}

	return 0;
}

static int qcom_pcie_post_init_2_3_2(struct qcom_pcie *pcie)
{
	u32 val;

	/* enable PCIe clocks and resets */
	val = readl(pcie->parf + PARF_PHY_CTRL);
	val &= ~PHY_TEST_PWR_DOWN;
	writel(val, pcie->parf + PARF_PHY_CTRL);

	qcom_pcie_configure_dbi_base(pcie);

	/* MAC PHY_POWERDOWN MUX DISABLE  */
	val = readl(pcie->parf + PARF_SYS_CTRL);
	val &= ~MAC_PHY_POWERDOWN_IN_P2_D_MUX_EN;
	writel(val, pcie->parf + PARF_SYS_CTRL);

	val = readl(pcie->parf + PARF_MHI_CLOCK_RESET_CTRL);
	val |= BYPASS;
	writel(val, pcie->parf + PARF_MHI_CLOCK_RESET_CTRL);

	val = readl(pcie->parf + PARF_AXI_MSTR_WR_ADDR_HALT_V2);
	val |= EN;
	writel(val, pcie->parf + PARF_AXI_MSTR_WR_ADDR_HALT_V2);

	qcom_pcie_set_slot_nccs(pcie->pci);

	return 0;
}

static int qcom_pcie_get_resources_2_4_0(struct qcom_pcie *pcie)
{
	struct qcom_pcie_resources_2_4_0 *res = &pcie->res.v2_4_0;
	struct dw_pcie *pci = pcie->pci;
	struct device *dev = pci->dev;
	bool is_ipq = of_device_is_compatible(dev->of_node, "qcom,pcie-ipq4019");
	int ret;

	res->num_clks = devm_clk_bulk_get_all(dev, &res->clks);
	if (res->num_clks < 0) {
		dev_err(dev, "Failed to get clocks\n");
		return res->num_clks;
	}

	res->resets[0].id = "axi_m";
	res->resets[1].id = "axi_s";
	res->resets[2].id = "axi_m_sticky";
	res->resets[3].id = "pipe_sticky";
	res->resets[4].id = "pwr";
	res->resets[5].id = "ahb";
	res->resets[6].id = "pipe";
	res->resets[7].id = "axi_m_vmid";
	res->resets[8].id = "axi_s_xpu";
	res->resets[9].id = "parf";
	res->resets[10].id = "phy";
	res->resets[11].id = "phy_ahb";

	res->num_resets = is_ipq ? 12 : 6;

	ret = devm_reset_control_bulk_get_exclusive(dev, res->num_resets, res->resets);
	if (ret < 0)
		return ret;

	return 0;
}

static void qcom_pcie_deinit_2_4_0(struct qcom_pcie *pcie)
{
	struct qcom_pcie_resources_2_4_0 *res = &pcie->res.v2_4_0;

	reset_control_bulk_assert(res->num_resets, res->resets);
	clk_bulk_disable_unprepare(res->num_clks, res->clks);
}

static int qcom_pcie_init_2_4_0(struct qcom_pcie *pcie)
{
	struct qcom_pcie_resources_2_4_0 *res = &pcie->res.v2_4_0;
	struct dw_pcie *pci = pcie->pci;
	struct device *dev = pci->dev;
	int ret;

	ret = reset_control_bulk_assert(res->num_resets, res->resets);
	if (ret < 0) {
		dev_err(dev, "cannot assert resets\n");
		return ret;
	}

	usleep_range(10000, 12000);

	ret = reset_control_bulk_deassert(res->num_resets, res->resets);
	if (ret < 0) {
		dev_err(dev, "cannot deassert resets\n");
		return ret;
	}

	usleep_range(10000, 12000);

	ret = clk_bulk_prepare_enable(res->num_clks, res->clks);
	if (ret) {
		reset_control_bulk_assert(res->num_resets, res->resets);
		return ret;
	}

	return 0;
}

static int qcom_pcie_get_resources_2_3_3(struct qcom_pcie *pcie)
{
	struct qcom_pcie_resources_2_3_3 *res = &pcie->res.v2_3_3;
	struct dw_pcie *pci = pcie->pci;
	struct device *dev = pci->dev;
	int ret;

	res->num_clks = devm_clk_bulk_get_all(dev, &res->clks);
	if (res->num_clks < 0) {
		dev_err(dev, "Failed to get clocks\n");
		return res->num_clks;
	}

	res->rst[0].id = "axi_m";
	res->rst[1].id = "axi_s";
	res->rst[2].id = "pipe";
	res->rst[3].id = "axi_m_sticky";
	res->rst[4].id = "sticky";
	res->rst[5].id = "ahb";
	res->rst[6].id = "sleep";

	ret = devm_reset_control_bulk_get_exclusive(dev, ARRAY_SIZE(res->rst), res->rst);
	if (ret < 0)
		return ret;

	return 0;
}

static void qcom_pcie_deinit_2_3_3(struct qcom_pcie *pcie)
{
	struct qcom_pcie_resources_2_3_3 *res = &pcie->res.v2_3_3;

	clk_bulk_disable_unprepare(res->num_clks, res->clks);
}

static int qcom_pcie_init_2_3_3(struct qcom_pcie *pcie)
{
	struct qcom_pcie_resources_2_3_3 *res = &pcie->res.v2_3_3;
	struct dw_pcie *pci = pcie->pci;
	struct device *dev = pci->dev;
	int ret;

	ret = reset_control_bulk_assert(ARRAY_SIZE(res->rst), res->rst);
	if (ret < 0) {
		dev_err(dev, "cannot assert resets\n");
		return ret;
	}

	usleep_range(2000, 2500);

	ret = reset_control_bulk_deassert(ARRAY_SIZE(res->rst), res->rst);
	if (ret < 0) {
		dev_err(dev, "cannot deassert resets\n");
		return ret;
	}

	/*
	 * Don't have a way to see if the reset has completed.
	 * Wait for some time.
	 */
	usleep_range(2000, 2500);

	ret = clk_bulk_prepare_enable(res->num_clks, res->clks);
	if (ret) {
		dev_err(dev, "cannot prepare/enable clocks\n");
		goto err_assert_resets;
	}

	return 0;

err_assert_resets:
	/*
	 * Not checking for failure, will anyway return
	 * the original failure in 'ret'.
	 */
	reset_control_bulk_assert(ARRAY_SIZE(res->rst), res->rst);

	return ret;
}

static int qcom_pcie_post_init_2_3_3(struct qcom_pcie *pcie)
{
	struct dw_pcie *pci = pcie->pci;
	u16 offset = dw_pcie_find_capability(pci, PCI_CAP_ID_EXP);
	u32 val;

	val = readl(pcie->parf + PARF_PHY_CTRL);
	val &= ~PHY_TEST_PWR_DOWN;
	writel(val, pcie->parf + PARF_PHY_CTRL);

	qcom_pcie_configure_dbi_atu_base(pcie);

	writel(MST_WAKEUP_EN | SLV_WAKEUP_EN | MSTR_ACLK_CGC_DIS
		| SLV_ACLK_CGC_DIS | CORE_CLK_CGC_DIS |
		AUX_PWR_DET | L23_CLK_RMV_DIS | L1_CLK_RMV_DIS,
		pcie->parf + PARF_SYS_CTRL);
	writel(0, pcie->parf + PARF_Q2A_FLUSH);

	writel(PCI_COMMAND_MASTER, pci->dbi_base + PCI_COMMAND);

	dw_pcie_dbi_ro_wr_en(pci);

	writel(PCIE_CAP_SLOT_VAL, pci->dbi_base + offset + PCI_EXP_SLTCAP);

	val = readl(pci->dbi_base + offset + PCI_EXP_LNKCAP);
	val &= ~PCI_EXP_LNKCAP_ASPMS;
	writel(val, pci->dbi_base + offset + PCI_EXP_LNKCAP);

	writel(PCI_EXP_DEVCTL2_COMP_TMOUT_DIS, pci->dbi_base + offset +
		PCI_EXP_DEVCTL2);

	dw_pcie_dbi_ro_wr_dis(pci);

	return 0;
}

static int qcom_pcie_get_resources_2_7_0(struct qcom_pcie *pcie)
{
	struct qcom_pcie_resources_2_7_0 *res = &pcie->res.v2_7_0;
	struct dw_pcie *pci = pcie->pci;
	struct device *dev = pci->dev;
	int ret;

	if (pcie->cfg->has_link_down_recovery)
		res->rst = devm_reset_control_get_exclusive(dev, "pci");
	else
		res->rst = devm_reset_control_array_get_exclusive(dev);
	if (IS_ERR(res->rst))
		return PTR_ERR(res->rst);

	if (pcie->cfg->has_link_down_recovery) {
		pcie->link_down_reset =
			devm_reset_control_get_optional_exclusive(dev, "link_down");
		if (IS_ERR(pcie->link_down_reset))
			return PTR_ERR(pcie->link_down_reset);
	}

	res->supplies[0].supply = "vdda";
	res->supplies[1].supply = "vddpe-3v3";
	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(res->supplies),
				      res->supplies);
	if (ret)
		return ret;

	res->num_clks = devm_clk_bulk_get_all(dev, &res->clks);
	if (res->num_clks < 0) {
		dev_err(dev, "Failed to get clocks\n");
		return res->num_clks;
	}

	return 0;
}

static int qcom_pcie_init_2_7_0(struct qcom_pcie *pcie)
{
	struct qcom_pcie_resources_2_7_0 *res = &pcie->res.v2_7_0;
	struct dw_pcie *pci = pcie->pci;
	struct device *dev = pci->dev;
	u32 val;
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(res->supplies), res->supplies);
	if (ret < 0) {
		dev_err(dev, "cannot enable regulators\n");
		return ret;
	}

	ret = clk_bulk_prepare_enable(res->num_clks, res->clks);
	if (ret < 0)
		goto err_disable_regulators;

	ret = reset_control_assert(res->rst);
	if (ret) {
		dev_err(dev, "reset assert failed (%d)\n", ret);
		goto err_disable_clocks;
	}

	usleep_range(1000, 1500);

	ret = reset_control_deassert(res->rst);
	if (ret) {
		dev_err(dev, "reset deassert failed (%d)\n", ret);
		goto err_disable_clocks;
	}

	/* Wait for reset to complete, required on SM8450 */
	usleep_range(1000, 1500);

	/* configure PCIe to RC mode */
	writel(DEVICE_TYPE_RC, pcie->parf + PARF_DEVICE_TYPE);

	/* enable PCIe clocks and resets */
	val = readl(pcie->parf + PARF_PHY_CTRL);
	val &= ~PHY_TEST_PWR_DOWN;
	writel(val, pcie->parf + PARF_PHY_CTRL);

	qcom_pcie_configure_dbi_atu_base(pcie);

	/* MAC PHY_POWERDOWN MUX DISABLE  */
	val = readl(pcie->parf + PARF_SYS_CTRL);
	val &= ~MAC_PHY_POWERDOWN_IN_P2_D_MUX_EN;
	writel(val, pcie->parf + PARF_SYS_CTRL);

	val = readl(pcie->parf + PARF_MHI_CLOCK_RESET_CTRL);
	val |= BYPASS;
	writel(val, pcie->parf + PARF_MHI_CLOCK_RESET_CTRL);

	/* Enable L1 and L1SS */
	val = readl(pcie->parf + PARF_PM_CTRL);
	val &= ~REQ_NOT_ENTR_L1;
	writel(val, pcie->parf + PARF_PM_CTRL);

	pci->l1ss_support = true;

	val = readl(pcie->parf + PARF_AXI_MSTR_WR_ADDR_HALT_V2);
	val |= EN;
	writel(val, pcie->parf + PARF_AXI_MSTR_WR_ADDR_HALT_V2);

	return 0;
err_disable_clocks:
	clk_bulk_disable_unprepare(res->num_clks, res->clks);
err_disable_regulators:
	regulator_bulk_disable(ARRAY_SIZE(res->supplies), res->supplies);

	return ret;
}

static int qcom_pcie_post_init_2_7_0(struct qcom_pcie *pcie)
{
	const struct qcom_pcie_cfg *pcie_cfg = pcie->cfg;

	if (pcie_cfg->override_no_snoop)
		writel(WR_NO_SNOOP_OVERRIDE_EN | RD_NO_SNOOP_OVERRIDE_EN,
				pcie->parf + PARF_NO_SNOOP_OVERRIDE);

	qcom_pcie_set_slot_nccs(pcie->pci);

	return 0;
}

static int qcom_pcie_enable_aspm(struct pci_dev *pdev, void *userdata)
{
	/*
	 * Downstream devices need to be in D0 state before enabling PCI PM
	 * substates.
	 */
	pci_set_power_state_locked(pdev, PCI_D0);
	pci_enable_link_state_locked(pdev, PCIE_LINK_STATE_ALL);

	return 0;
}

static void qcom_pcie_host_post_init_2_7_0(struct qcom_pcie *pcie)
{
	struct dw_pcie_rp *pp = &pcie->pci->pp;

	pci_walk_bus(pp->bridge->bus, qcom_pcie_enable_aspm, NULL);
}

static void qcom_pcie_deinit_2_7_0(struct qcom_pcie *pcie)
{
	struct qcom_pcie_resources_2_7_0 *res = &pcie->res.v2_7_0;

	clk_bulk_disable_unprepare(res->num_clks, res->clks);

	regulator_bulk_disable(ARRAY_SIZE(res->supplies), res->supplies);
}

static int qcom_pcie_config_sid_1_9_0(struct qcom_pcie *pcie)
{
	/* iommu map structure */
	struct {
		u32 bdf;
		u32 phandle;
		u32 smmu_sid;
		u32 smmu_sid_len;
	} *map;
	void __iomem *bdf_to_sid_base = pcie->parf + PARF_BDF_TO_SID_TABLE_N;
	struct device *dev = pcie->pci->dev;
	u8 qcom_pcie_crc8_table[CRC8_TABLE_SIZE];
	int i, nr_map, size = 0;
	u32 smmu_sid_base;
	u32 val;

	of_get_property(dev->of_node, "iommu-map", &size);
	if (!size)
		return 0;

	/* Enable BDF to SID translation by disabling bypass mode (default) */
	val = readl(pcie->parf + PARF_BDF_TO_SID_CFG);
	val &= ~BDF_TO_SID_BYPASS;
	writel(val, pcie->parf + PARF_BDF_TO_SID_CFG);

	map = kzalloc(size, GFP_KERNEL);
	if (!map)
		return -ENOMEM;

	of_property_read_u32_array(dev->of_node, "iommu-map", (u32 *)map,
				   size / sizeof(u32));

	nr_map = size / (sizeof(*map));

	crc8_populate_msb(qcom_pcie_crc8_table, QCOM_PCIE_CRC8_POLYNOMIAL);

	/* Registers need to be zero out first */
	memset_io(bdf_to_sid_base, 0, CRC8_TABLE_SIZE * sizeof(u32));

	/* Extract the SMMU SID base from the first entry of iommu-map */
	smmu_sid_base = map[0].smmu_sid;

	/* Look for an available entry to hold the mapping */
	for (i = 0; i < nr_map; i++) {
		__be16 bdf_be = cpu_to_be16(map[i].bdf);
		u32 val;
		u8 hash;

		hash = crc8(qcom_pcie_crc8_table, (u8 *)&bdf_be, sizeof(bdf_be), 0);

		val = readl(bdf_to_sid_base + hash * sizeof(u32));

		/* If the register is already populated, look for next available entry */
		while (val) {
			u8 current_hash = hash++;
			u8 next_mask = 0xff;

			/* If NEXT field is NULL then update it with next hash */
			if (!(val & next_mask)) {
				val |= (u32)hash;
				writel(val, bdf_to_sid_base + current_hash * sizeof(u32));
			}

			val = readl(bdf_to_sid_base + hash * sizeof(u32));
		}

		/* BDF [31:16] | SID [15:8] | NEXT [7:0] */
		val = map[i].bdf << 16 | (map[i].smmu_sid - smmu_sid_base) << 8 | 0;
		writel(val, bdf_to_sid_base + hash * sizeof(u32));
	}

	kfree(map);

	return 0;
}

static int qcom_pcie_get_resources_2_9_0(struct qcom_pcie *pcie)
{
	struct qcom_pcie_resources_2_9_0 *res = &pcie->res.v2_9_0;
	struct dw_pcie *pci = pcie->pci;
	struct device *dev = pci->dev;

	res->num_clks = devm_clk_bulk_get_all(dev, &res->clks);
	if (res->num_clks < 0) {
		dev_err(dev, "Failed to get clocks\n");
		return res->num_clks;
	}

	res->rst = devm_reset_control_array_get_exclusive(dev);
	if (IS_ERR(res->rst))
		return PTR_ERR(res->rst);

	return 0;
}

static void qcom_pcie_deinit_2_9_0(struct qcom_pcie *pcie)
{
	struct qcom_pcie_resources_2_9_0 *res = &pcie->res.v2_9_0;

	clk_bulk_disable_unprepare(res->num_clks, res->clks);
}

static int qcom_pcie_init_2_9_0(struct qcom_pcie *pcie)
{
	struct qcom_pcie_resources_2_9_0 *res = &pcie->res.v2_9_0;
	struct device *dev = pcie->pci->dev;
	int ret;

	ret = reset_control_assert(res->rst);
	if (ret) {
		dev_err(dev, "reset assert failed (%d)\n", ret);
		return ret;
	}

	/*
	 * Delay periods before and after reset deassert are working values
	 * from downstream Codeaurora kernel
	 */
	usleep_range(2000, 2500);

	ret = reset_control_deassert(res->rst);
	if (ret) {
		dev_err(dev, "reset deassert failed (%d)\n", ret);
		return ret;
	}

	usleep_range(2000, 2500);

	return clk_bulk_prepare_enable(res->num_clks, res->clks);
}

static int qcom_pcie_post_init_2_9_0(struct qcom_pcie *pcie)
{
	struct dw_pcie *pci = pcie->pci;
	u16 offset = dw_pcie_find_capability(pci, PCI_CAP_ID_EXP);
	u32 val;
	int i;

	val = readl(pcie->parf + PARF_PHY_CTRL);
	val &= ~PHY_TEST_PWR_DOWN;
	writel(val, pcie->parf + PARF_PHY_CTRL);

	qcom_pcie_configure_dbi_atu_base(pcie);

	writel(DEVICE_TYPE_RC, pcie->parf + PARF_DEVICE_TYPE);
	writel(BYPASS | MSTR_AXI_CLK_EN | AHB_CLK_EN,
		pcie->parf + PARF_MHI_CLOCK_RESET_CTRL);
	writel(GEN3_RELATED_OFF_RXEQ_RGRDLESS_RXTS |
		GEN3_RELATED_OFF_GEN3_ZRXDC_NONCOMPL,
		pci->dbi_base + GEN3_RELATED_OFF);

	writel(MST_WAKEUP_EN | SLV_WAKEUP_EN | MSTR_ACLK_CGC_DIS |
		SLV_ACLK_CGC_DIS | CORE_CLK_CGC_DIS |
		AUX_PWR_DET | L23_CLK_RMV_DIS | L1_CLK_RMV_DIS,
		pcie->parf + PARF_SYS_CTRL);

	writel(0, pcie->parf + PARF_Q2A_FLUSH);

	dw_pcie_dbi_ro_wr_en(pci);

	writel(PCIE_CAP_SLOT_VAL, pci->dbi_base + offset + PCI_EXP_SLTCAP);

	val = readl(pci->dbi_base + offset + PCI_EXP_LNKCAP);
	val &= ~PCI_EXP_LNKCAP_ASPMS;
	writel(val, pci->dbi_base + offset + PCI_EXP_LNKCAP);

	writel(PCI_EXP_DEVCTL2_COMP_TMOUT_DIS, pci->dbi_base + offset +
			PCI_EXP_DEVCTL2);

	dw_pcie_dbi_ro_wr_dis(pci);

	for (i = 0; i < 256; i++)
		writel(0, pcie->parf + PARF_BDF_TO_SID_TABLE_N + (4 * i));

	return 0;
}

static bool qcom_pcie_link_up(struct dw_pcie *pci)
{
	u16 offset = dw_pcie_find_capability(pci, PCI_CAP_ID_EXP);
	u16 val = readw(pci->dbi_base + offset + PCI_EXP_LNKSTA);

	return val & PCI_EXP_LNKSTA_DLLLA;
}

static void qcom_pcie_phy_power_off(struct qcom_pcie *pcie)
{
	struct qcom_pcie_port *port;

	list_for_each_entry(port, &pcie->ports, list)
		phy_power_off(port->phy);
}

static int qcom_pcie_phy_power_on(struct qcom_pcie *pcie)
{
	struct qcom_pcie_port *port;
	int ret;

	list_for_each_entry(port, &pcie->ports, list) {
		ret = phy_set_mode_ext(port->phy, PHY_MODE_PCIE, PHY_MODE_PCIE_RC);
		if (ret)
			return ret;

		ret = phy_power_on(port->phy);
		if (ret) {
			qcom_pcie_phy_power_off(pcie);
			return ret;
		}
	}

	return 0;
}

static void qcom_pcie_init_ecam_blocker(struct qcom_pcie *pcie)
{
	struct dw_pcie_rp *pp = &pcie->pci->pp;
	struct dw_pcie *pci = pcie->pci;
	u64 blocker_base = pci->dbi_phys_addr + SZ_4K;
	u64 blocker_limit = pci->dbi_phys_addr + SZ_1M - 1;
	u32 val;

	/*
	 * PARF_ECAM_BASE is the base of the complete ECAM aperture: DBI starts
	 * at offset 0 and the downstream configuration window at offset 1 MiB.
	 */
	writel(lower_32_bits(pci->dbi_phys_addr),
	       pcie->parf + PARF_ECAM_BASE);
	writel(upper_32_bits(pci->dbi_phys_addr),
	       pcie->parf + PARF_ECAM_BASE_HI);

	/* Keep the Root Port's 4 KiB DBI aperture available for recovery. */
	writel(lower_32_bits(blocker_base),
	       pcie->parf + PARF_BLOCK_SLV_AXI_WR_BASE);
	writel(upper_32_bits(blocker_base),
	       pcie->parf + PARF_BLOCK_SLV_AXI_WR_BASE_HI);
	writel(lower_32_bits(blocker_limit),
	       pcie->parf + PARF_BLOCK_SLV_AXI_WR_LIMIT);
	writel(upper_32_bits(blocker_limit),
	       pcie->parf + PARF_BLOCK_SLV_AXI_WR_LIMIT_HI);

	writel(lower_32_bits(blocker_base),
	       pcie->parf + PARF_BLOCK_SLV_AXI_RD_BASE);
	writel(upper_32_bits(blocker_base),
	       pcie->parf + PARF_BLOCK_SLV_AXI_RD_BASE_HI);
	writel(lower_32_bits(blocker_limit),
	       pcie->parf + PARF_BLOCK_SLV_AXI_RD_LIMIT);
	writel(upper_32_bits(blocker_limit),
	       pcie->parf + PARF_BLOCK_SLV_AXI_RD_LIMIT_HI);

	/* Keep the hardware Link Down blocker and automatic release enabled. */
	val = readl(pcie->parf + PARF_LINK_DOWN_ECAM_BLOCK);
	val |= LINK_DOWN_ECAM_BLOCK_DBI_EN |
	       LINK_DOWN_ECAM_AUTO_RELEASE_EN |
	       LINK_DOWN_ECAM_AUTO_BLOCK_EN;
	val &= ~LINK_DOWN_ECAM_IGNORE_LINK_DOWN;
	writel(val, pcie->parf + PARF_LINK_DOWN_ECAM_BLOCK);

	/* Keep recovery traffic blocked until the link is known to be usable. */
	val = readl(pcie->parf + PARF_SYS_CTRL);
	if (READ_ONCE(pp->cfg_access_blocked))
		val |= ECAM_BLOCKER_EN;
	else
		val &= ~ECAM_BLOCKER_EN;
	writel(val, pcie->parf + PARF_SYS_CTRL);
}

static void qcom_pcie_enable_ecam_blocker(struct qcom_pcie *pcie)
{
	u32 sys_ctrl;

	sys_ctrl = readl(pcie->parf + PARF_SYS_CTRL);
	sys_ctrl |= ECAM_BLOCKER_EN;
	writel(sys_ctrl, pcie->parf + PARF_SYS_CTRL);

	/* Flush the write so the blocker is enabled before this function returns */
	readl(pcie->parf + PARF_SYS_CTRL);
}

static void qcom_pcie_block_link_down_accesses(struct qcom_pcie *pcie)
{
	struct dw_pcie_rp *pp = &pcie->pci->pp;
	u32 val;

	/*
	 * Stop new config accesses before arming the hardware backstop. An access
	 * already in flight is handled by the Q2A Link Down flush state machine.
	 */
	WRITE_ONCE(pp->cfg_access_blocked, true);
	qcom_pcie_enable_ecam_blocker(pcie);

	/*
	 * The range blocker is now an immediate backstop for downstream traffic.
	 * Also ask the Q2A state machine to block DBI and non-DBI traffic and
	 * ignore another Link Down while recovery is in progress.
	 */
	val = readl(pcie->parf + PARF_LINK_DOWN_ECAM_BLOCK);
	val |= LINK_DOWN_ECAM_IGNORE_LINK_DOWN;
	writel(val, pcie->parf + PARF_LINK_DOWN_ECAM_BLOCK);

	writel(LINK_DOWN_ECAM_SW_BLOCK_TRAFFIC,
	       pcie->parf + PARF_LINK_DOWN_ECAM_BLOCK_CMD);
}

static void qcom_pcie_unblock_accesses(struct qcom_pcie *pcie)
{
	struct dw_pcie_rp *pp = &pcie->pci->pp;
	u32 val;

	val = readl(pcie->parf + PARF_SYS_CTRL);
	val &= ~ECAM_BLOCKER_EN;
	writel(val, pcie->parf + PARF_SYS_CTRL);
	readl(pcie->parf + PARF_SYS_CTRL);

	WRITE_ONCE(pp->cfg_access_blocked, false);
}

static int qcom_pcie_mark_device_frozen(struct pci_dev *pdev, void *data)
{
	pci_dev_set_io_state(pdev, pci_channel_io_frozen);

	return 0;
}

static int qcom_pcie_reboot_notifier(struct notifier_block *nb,
				     unsigned long action, void *data)
{
	struct qcom_pcie *pcie = container_of(nb, struct qcom_pcie, reboot_nb);
	struct dw_pcie_rp *pp = &pcie->pci->pp;
	bool accesses_blocked;

	/*
	 * The reboot notifier runs before device_shutdown(). Do not let an
	 * in-flight recovery reopen config access while endpoint drivers are
	 * shutting down.
	 */
	WRITE_ONCE(pcie->shutting_down, true);
	cancel_work_sync(&pcie->link_down_work);
	mutex_lock(&pp->bridge->recovery_lock);
	mutex_unlock(&pp->bridge->recovery_lock);

	accesses_blocked = READ_ONCE(pp->cfg_access_blocked);
	if (!accesses_blocked &&
	    ((readl_relaxed(pcie->parf + PARF_INT_ALL_STATUS) &
	      PARF_INT_ALL_LINK_DOWN) || !dw_pcie_link_up(pcie->pci))) {
		qcom_pcie_block_link_down_accesses(pcie);
		accesses_blocked = true;
	}

	/*
	 * Preserve an orderly endpoint shutdown while the link is healthy.  If
	 * Link Down recovery had already started (or the link is already down),
	 * tell endpoint shutdown paths that the channel is unusable and retain the
	 * hardware blocker. A healthy link remains fully accessible so endpoint
	 * drivers can perform their normal shutdown handshake.
	 */
	if (accesses_blocked) {
		pci_walk_bus(pp->bridge->bus,
			     qcom_pcie_mark_device_frozen, NULL);
		qcom_pcie_block_link_down_accesses(pcie);
	}

	return NOTIFY_DONE;
}

static int qcom_pcie_host_init(struct dw_pcie_rp *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct qcom_pcie *pcie = to_qcom_pcie(pci);
	int ret;

	qcom_pcie_perst_assert(pcie);

	ret = pcie->cfg->ops->init(pcie);
	if (ret)
		return ret;

	ret = qcom_pcie_phy_power_on(pcie);
	if (ret)
		goto err_deinit;

	ret = pci_pwrctrl_create_devices(pci->dev);
	if (ret)
		goto err_disable_phy;

	ret = pci_pwrctrl_power_on_devices(pci->dev);
	if (ret)
		goto err_pwrctrl_destroy;

	if (pcie->cfg->ops->post_init) {
		ret = pcie->cfg->ops->post_init(pcie);
		if (ret)
			goto err_pwrctrl_power_off;
	}

	qcom_pcie_clear_aspm_l0s(pcie->pci);
	dw_pcie_remove_capability(pcie->pci, PCI_CAP_ID_MSIX);
	dw_pcie_remove_ext_capability(pcie->pci, PCI_EXT_CAP_ID_DPC);

	if (pcie->cfg->has_link_down_recovery)
		qcom_pcie_init_ecam_blocker(pcie);
	qcom_pcie_perst_deassert(pcie);

	if (pcie->cfg->ops->config_sid) {
		ret = pcie->cfg->ops->config_sid(pcie);
		if (ret)
			goto err_assert_reset;
	}

	if (pcie->cfg->has_link_down_recovery)
		pp->bridge->reset_root_port = qcom_pcie_reset_root_port;

	return 0;

err_assert_reset:
	qcom_pcie_perst_assert(pcie);
err_pwrctrl_power_off:
	pci_pwrctrl_power_off_devices(pci->dev);
err_pwrctrl_destroy:
	if (ret != -EPROBE_DEFER)
		pci_pwrctrl_destroy_devices(pci->dev);
err_disable_phy:
	qcom_pcie_phy_power_off(pcie);
err_deinit:
	pcie->cfg->ops->deinit(pcie);

	return ret;
}

static void qcom_pcie_host_deinit(struct dw_pcie_rp *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct qcom_pcie *pcie = to_qcom_pcie(pci);

	qcom_pcie_perst_assert(pcie);

	/*
	 * No need to destroy pwrctrl devices as this function only gets called
	 * during system suspend as of now.
	 */
	pci_pwrctrl_power_off_devices(pci->dev);
	qcom_pcie_phy_power_off(pcie);
	pcie->cfg->ops->deinit(pcie);
}

static void qcom_pcie_host_post_init(struct dw_pcie_rp *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct qcom_pcie *pcie = to_qcom_pcie(pci);

	if (pcie->cfg->ops->host_post_init)
		pcie->cfg->ops->host_post_init(pcie);
}

static const struct dw_pcie_host_ops qcom_pcie_dw_ops = {
	.init		= qcom_pcie_host_init,
	.deinit		= qcom_pcie_host_deinit,
	.post_init	= qcom_pcie_host_post_init,
};

/* Qcom IP rev.: 2.1.0	Synopsys IP rev.: 4.01a */
static const struct qcom_pcie_ops ops_2_1_0 = {
	.get_resources = qcom_pcie_get_resources_2_1_0,
	.init = qcom_pcie_init_2_1_0,
	.post_init = qcom_pcie_post_init_2_1_0,
	.deinit = qcom_pcie_deinit_2_1_0,
	.ltssm_enable = qcom_pcie_2_1_0_ltssm_enable,
};

/* Qcom IP rev.: 1.0.0	Synopsys IP rev.: 4.11a */
static const struct qcom_pcie_ops ops_1_0_0 = {
	.get_resources = qcom_pcie_get_resources_1_0_0,
	.init = qcom_pcie_init_1_0_0,
	.post_init = qcom_pcie_post_init_1_0_0,
	.deinit = qcom_pcie_deinit_1_0_0,
	.ltssm_enable = qcom_pcie_2_1_0_ltssm_enable,
};

/* Qcom IP rev.: 2.3.2	Synopsys IP rev.: 4.21a */
static const struct qcom_pcie_ops ops_2_3_2 = {
	.get_resources = qcom_pcie_get_resources_2_3_2,
	.init = qcom_pcie_init_2_3_2,
	.post_init = qcom_pcie_post_init_2_3_2,
	.deinit = qcom_pcie_deinit_2_3_2,
	.ltssm_enable = qcom_pcie_2_3_2_ltssm_enable,
};

/* Qcom IP rev.: 2.4.0	Synopsys IP rev.: 4.20a */
static const struct qcom_pcie_ops ops_2_4_0 = {
	.get_resources = qcom_pcie_get_resources_2_4_0,
	.init = qcom_pcie_init_2_4_0,
	.post_init = qcom_pcie_post_init_2_3_2,
	.deinit = qcom_pcie_deinit_2_4_0,
	.ltssm_enable = qcom_pcie_2_3_2_ltssm_enable,
};

/* Qcom IP rev.: 2.3.3	Synopsys IP rev.: 4.30a */
static const struct qcom_pcie_ops ops_2_3_3 = {
	.get_resources = qcom_pcie_get_resources_2_3_3,
	.init = qcom_pcie_init_2_3_3,
	.post_init = qcom_pcie_post_init_2_3_3,
	.deinit = qcom_pcie_deinit_2_3_3,
	.ltssm_enable = qcom_pcie_2_3_2_ltssm_enable,
};

/* Qcom IP rev.: 2.7.0	Synopsys IP rev.: 4.30a */
static const struct qcom_pcie_ops ops_2_7_0 = {
	.get_resources = qcom_pcie_get_resources_2_7_0,
	.init = qcom_pcie_init_2_7_0,
	.post_init = qcom_pcie_post_init_2_7_0,
	.deinit = qcom_pcie_deinit_2_7_0,
	.ltssm_enable = qcom_pcie_2_3_2_ltssm_enable,
};

/* Qcom IP rev.: 1.9.0 */
static const struct qcom_pcie_ops ops_1_9_0 = {
	.get_resources = qcom_pcie_get_resources_2_7_0,
	.init = qcom_pcie_init_2_7_0,
	.post_init = qcom_pcie_post_init_2_7_0,
	.host_post_init = qcom_pcie_host_post_init_2_7_0,
	.deinit = qcom_pcie_deinit_2_7_0,
	.ltssm_enable = qcom_pcie_2_3_2_ltssm_enable,
	.config_sid = qcom_pcie_config_sid_1_9_0,
};

/* Qcom IP rev.: 1.21.0  Synopsys IP rev.: 5.60a */
static const struct qcom_pcie_ops ops_1_21_0 = {
	.get_resources = qcom_pcie_get_resources_2_7_0,
	.init = qcom_pcie_init_2_7_0,
	.post_init = qcom_pcie_post_init_2_7_0,
	.host_post_init = qcom_pcie_host_post_init_2_7_0,
	.deinit = qcom_pcie_deinit_2_7_0,
	.ltssm_enable = qcom_pcie_2_3_2_ltssm_enable,
};

/* Qcom IP rev.: 2.9.0  Synopsys IP rev.: 5.00a */
static const struct qcom_pcie_ops ops_2_9_0 = {
	.get_resources = qcom_pcie_get_resources_2_9_0,
	.init = qcom_pcie_init_2_9_0,
	.post_init = qcom_pcie_post_init_2_9_0,
	.deinit = qcom_pcie_deinit_2_9_0,
	.ltssm_enable = qcom_pcie_2_3_2_ltssm_enable,
};

static const struct qcom_pcie_cfg cfg_1_0_0 = {
	.ops = &ops_1_0_0,
};

static const struct qcom_pcie_cfg cfg_1_9_0 = {
	.ops = &ops_1_9_0,
	.has_ext_parf_regs = true,
};

static const struct qcom_pcie_cfg cfg_1_34_0 = {
	.ops = &ops_1_9_0,
	.override_no_snoop = true,
	.has_ext_parf_regs = true,
};

static const struct qcom_pcie_cfg cfg_2_1_0 = {
	.ops = &ops_2_1_0,
};

static const struct qcom_pcie_cfg cfg_2_3_2 = {
	.ops = &ops_2_3_2,
	.no_l0s = true,
};

static const struct qcom_pcie_cfg cfg_2_3_3 = {
	.ops = &ops_2_3_3,
};

static const struct qcom_pcie_cfg cfg_2_4_0 = {
	.ops = &ops_2_4_0,
};

static const struct qcom_pcie_cfg cfg_2_7_0 = {
	.ops = &ops_2_7_0,
	.has_ext_parf_regs = true,
};

static const struct qcom_pcie_cfg cfg_2_9_0 = {
	.ops = &ops_2_9_0,
};

static const struct qcom_pcie_cfg cfg_sc8280xp = {
	.ops = &ops_1_21_0,
	.no_l0s = true,
	.override_no_snoop = true,
	.has_ext_parf_regs = true,
};

static const struct qcom_pcie_cfg cfg_sc8280xp_link_down = {
	.ops = &ops_1_21_0,
	.no_l0s = true,
	.override_no_snoop = true,
	.has_ext_parf_regs = true,
	.has_link_down_recovery = true,
};

static const struct qcom_pcie_cfg cfg_fw_managed = {
	.firmware_managed = true,
};

static const struct dw_pcie_ops dw_pcie_ops = {
	.link_up = qcom_pcie_link_up,
	.start_link = qcom_pcie_start_link,
};

static int qcom_pcie_icc_init(struct qcom_pcie *pcie)
{
	struct dw_pcie *pci = pcie->pci;
	int ret;

	pcie->icc_mem = devm_of_icc_get(pci->dev, "pcie-mem");
	if (IS_ERR(pcie->icc_mem))
		return PTR_ERR(pcie->icc_mem);

	pcie->icc_cpu = devm_of_icc_get(pci->dev, "cpu-pcie");
	if (IS_ERR(pcie->icc_cpu))
		return PTR_ERR(pcie->icc_cpu);
	/*
	 * Some Qualcomm platforms require interconnect bandwidth constraints
	 * to be set before enabling interconnect clocks.
	 *
	 * Set an initial peak bandwidth corresponding to single-lane Gen 1
	 * for the pcie-mem path.
	 */
	ret = icc_set_bw(pcie->icc_mem, 0, QCOM_PCIE_LINK_SPEED_TO_BW(1));
	if (ret) {
		dev_err(pci->dev, "Failed to set bandwidth for PCIe-MEM interconnect path: %d\n",
			ret);
		return ret;
	}

	/*
	 * Since the CPU-PCIe path is only used for activities like register
	 * access of the host controller and endpoint Config/BAR space access,
	 * HW team has recommended to use a minimal bandwidth of 1KBps just to
	 * keep the path active.
	 */
	ret = icc_set_bw(pcie->icc_cpu, 0, kBps_to_icc(1));
	if (ret) {
		dev_err(pci->dev, "Failed to set bandwidth for CPU-PCIe interconnect path: %d\n",
			ret);
		icc_set_bw(pcie->icc_mem, 0, 0);
		return ret;
	}

	return 0;
}

static void qcom_pcie_icc_opp_update(struct qcom_pcie *pcie)
{
	u32 offset, status, width, speed;
	struct dw_pcie *pci = pcie->pci;
	struct dev_pm_opp_key key = {};
	unsigned long freq_kbps;
	struct dev_pm_opp *opp;
	int ret, freq_mbps;

	offset = dw_pcie_find_capability(pci, PCI_CAP_ID_EXP);
	status = readw(pci->dbi_base + offset + PCI_EXP_LNKSTA);

	/* Only update constraints if link is up. */
	if (!(status & PCI_EXP_LNKSTA_DLLLA))
		return;

	speed = FIELD_GET(PCI_EXP_LNKSTA_CLS, status);
	width = FIELD_GET(PCI_EXP_LNKSTA_NLW, status);

	if (pcie->icc_mem) {
		ret = icc_set_bw(pcie->icc_mem, 0,
				 width * QCOM_PCIE_LINK_SPEED_TO_BW(speed));
		if (ret) {
			dev_err(pci->dev, "Failed to set bandwidth for PCIe-MEM interconnect path: %d\n",
				ret);
		}
	} else if (pcie->use_pm_opp) {
		freq_mbps = pcie_dev_speed_mbps(pcie_link_speed[speed]);
		if (freq_mbps < 0)
			return;

		freq_kbps = freq_mbps * KILO;
		opp = dev_pm_opp_find_level_exact(pci->dev, speed);
		if (IS_ERR(opp)) {
			 /* opp-level is not defined use only frequency */
			opp = dev_pm_opp_find_freq_exact(pci->dev, freq_kbps * width,
							 true);
		} else {
			/* put opp-level OPP */
			dev_pm_opp_put(opp);

			key.freq = freq_kbps * width;
			key.level = speed;
			key.bw = 0;
			opp = dev_pm_opp_find_key_exact(pci->dev, &key, true);
		}
		if (!IS_ERR(opp)) {
			ret = dev_pm_opp_set_opp(pci->dev, opp);
			if (ret)
				dev_err(pci->dev, "Failed to set OPP for freq (%lu): %d\n",
					freq_kbps * width, ret);
			dev_pm_opp_put(opp);
		}
	}
}

static int qcom_pcie_has_storage_ep_cb(struct pci_dev *pdev, void *data)
{
	bool *found = data;

	if (pdev->class >> 16 == PCI_BASE_CLASS_STORAGE) {
		*found = true;
		return 1;
	}

	return 0;
}

static bool qcom_pcie_has_storage_ep(struct qcom_pcie *pcie)
{
	struct dw_pcie_rp *pp = &pcie->pci->pp;
	bool found = false;

	pci_walk_bus(pp->bridge->bus, qcom_pcie_has_storage_ep_cb, &found);

	return found;
}

/*
 * qcom_pcie_dump_advance_col - advance @col and append a separator to @buf.
 *
 * @col tracks the current column within the active section and must be
 * reset to 0 by the caller at the start of each new section; a newline is
 * emitted every QCOM_PCIE_DUMP_REGS_PER_LINE entries to keep the dump
 * compact in dmesg.
 */
static size_t qcom_pcie_dump_advance_col(char *buf, size_t buf_size, int *col)
{
	*col += 1;

	return scnprintf(buf, buf_size,
			 (*col % QCOM_PCIE_DUMP_REGS_PER_LINE) ? "  " : "\n");
}

static size_t qcom_pcie_dump_reg_val(char *buf, size_t buf_size, int *col,
				     u32 offset, u32 val)
{
	size_t len;

	len = scnprintf(buf, buf_size, "0x%03x: 0x%08x", offset, val);
	len += qcom_pcie_dump_advance_col(buf + len, buf_size - len, col);

	return len;
}

/* Same as qcom_pcie_dump_reg_val(), but tags the entry with a lane number
 * since the RAS-DES SD-EQ status registers are read once per lane at a
 * fixed offset, so the offset alone can't tell entries apart.
 */
static size_t qcom_pcie_dump_lane_reg_val(char *buf, size_t buf_size, int *col,
					  int lane, u32 offset, u32 val)
{
	size_t len;

	len = scnprintf(buf, buf_size, "L%d/0x%03x: 0x%08x", lane, offset, val);
	len += qcom_pcie_dump_advance_col(buf + len, buf_size - len, col);

	return len;
}

static size_t qcom_pcie_dump_reg_table(void __iomem *base, const u32 *regs,
				       int nregs, char *buf, size_t buf_size)
{
	size_t len = 0;
	int col = 0;
	int i;

	for (i = 0; i < nregs; i++)
		len += qcom_pcie_dump_reg_val(buf + len, buf_size - len, &col,
					      regs[i], readl_relaxed(base + regs[i]));

	if (col % QCOM_PCIE_DUMP_REGS_PER_LINE)
		len += scnprintf(buf + len, buf_size - len, "\n");

	return len;
}

/*
 * qcom_pcie_fill_dump_buf - fill @buf with key PCIe register values captured
 * at Link Down time.
 *
 * Returns the number of bytes written into @buf.
 */
static size_t qcom_pcie_fill_dump_buf(struct qcom_pcie *pcie, char *buf,
				      size_t buf_size, u32 irq_status,
				      bool dump_dbi)
{
	struct dw_pcie *pci = pcie->pci;
	u16 exp_cap, aer_cap, l1ss_cap, secpci_cap, pl16gt_cap, pl32gt_cap;
	size_t len = 0;
	int col = 0;
	u32 val;

	len += scnprintf(buf + len, buf_size - len,
			 "Captured PARF_INT_ALL_STATUS: 0x%08x\n\n",
			 irq_status);

	if (!dump_dbi) {
		len += scnprintf(buf + len, buf_size - len,
				 "DBI registers: skipped after Link Down\n");
		goto dump_parf;
	}

	len += scnprintf(buf + len, buf_size - len, "DBI registers:\n");

	exp_cap = dw_pcie_find_capability(pci, PCI_CAP_ID_EXP);
	if (exp_cap) {
		val = dw_pcie_readl_dbi(pci, exp_cap + PCI_EXP_LNKCAP);
		len += qcom_pcie_dump_reg_val(buf + len, buf_size - len, &col,
					     exp_cap + PCI_EXP_LNKCAP, val);

		val = dw_pcie_readl_dbi(pci, exp_cap + PCI_EXP_LNKCTL);
		len += qcom_pcie_dump_reg_val(buf + len, buf_size - len, &col,
					     exp_cap + PCI_EXP_LNKCTL, val);

		val = dw_pcie_readl_dbi(pci, exp_cap + PCI_EXP_LNKCTL2);
		len += qcom_pcie_dump_reg_val(buf + len, buf_size - len, &col,
					     exp_cap + PCI_EXP_LNKCTL2, val);
	}

	aer_cap = dw_pcie_find_ext_capability(pci, PCI_EXT_CAP_ID_ERR);
	if (aer_cap) {
		val = dw_pcie_readl_dbi(pci, aer_cap + PCI_ERR_UNCOR_STATUS);
		len += qcom_pcie_dump_reg_val(buf + len, buf_size - len, &col,
					     aer_cap + PCI_ERR_UNCOR_STATUS, val);

		val = dw_pcie_readl_dbi(pci, aer_cap + PCI_ERR_COR_STATUS);
		len += qcom_pcie_dump_reg_val(buf + len, buf_size - len, &col,
					     aer_cap + PCI_ERR_COR_STATUS, val);
	}

	l1ss_cap = dw_pcie_find_ext_capability(pci, PCI_EXT_CAP_ID_L1SS);
	if (l1ss_cap) {
		val = dw_pcie_readl_dbi(pci, l1ss_cap + PCI_L1SS_CTL1);
		len += qcom_pcie_dump_reg_val(buf + len, buf_size - len, &col,
					     l1ss_cap + PCI_L1SS_CTL1, val);
	}

	secpci_cap = dw_pcie_find_ext_capability(pci, PCI_EXT_CAP_ID_SECPCI);
	if (secpci_cap) {
		val = dw_pcie_readl_dbi(pci, secpci_cap + PCI_SECPCI_LE_CTRL);
		len += qcom_pcie_dump_reg_val(buf + len, buf_size - len, &col,
					     secpci_cap + PCI_SECPCI_LE_CTRL, val);

		val = dw_pcie_readl_dbi(pci, secpci_cap + PCI_SECPCI_LE_CTRL + 4);
		len += qcom_pcie_dump_reg_val(buf + len, buf_size - len, &col,
					     secpci_cap + PCI_SECPCI_LE_CTRL + 4, val);
	}

	pl16gt_cap = dw_pcie_find_ext_capability(pci, PCI_EXT_CAP_ID_PL_16GT);
	if (pl16gt_cap) {
		val = dw_pcie_readl_dbi(pci, pl16gt_cap + PCI_PL_16GT_LE_CTRL);
		len += qcom_pcie_dump_reg_val(buf + len, buf_size - len, &col,
					     pl16gt_cap + PCI_PL_16GT_LE_CTRL, val);

		val = dw_pcie_readl_dbi(pci, pl16gt_cap + PCI_PL_16GT_STATUS);
		len += qcom_pcie_dump_reg_val(buf + len, buf_size - len, &col,
					     pl16gt_cap + PCI_PL_16GT_STATUS, val);
	}

	pl32gt_cap = dw_pcie_find_ext_capability(pci, PCI_EXT_CAP_ID_PL_32GT);
	if (pl32gt_cap) {
		val = dw_pcie_readl_dbi(pci, pl32gt_cap + PCI_PL_32GT_LE_CTRL);
		len += qcom_pcie_dump_reg_val(buf + len, buf_size - len, &col,
					     pl32gt_cap + PCI_PL_32GT_LE_CTRL, val);

		val = dw_pcie_readl_dbi(pci, pl32gt_cap + PCI_PL_32GT_STATUS);
		len += qcom_pcie_dump_reg_val(buf + len, buf_size - len, &col,
					     pl32gt_cap + PCI_PL_32GT_STATUS, val);
	}

	/*
	 * Gen5 (32 GT/s) local/remote pre/post cursor status, read through the
	 * SD-EQ registers inside the DWC RAS-DES vendor-specific extended
	 * capability. For each lane, write the lane selector into
	 * PCIE_RAS_DES_SD_EQ_CONTROL1 then read back
	 * PCIE_RAS_DES_SD_EQ_STATUS2/3, which carry the local/remote pre/post
	 * cursor values.
	 */
	if (pcie_link_speed[pci->max_link_speed] == PCIE_SPEED_32_0GT) {
		u16 rasdes_cap = dw_pcie_find_rasdes_capability(pci);

		if (rasdes_cap) {
			int num_lanes = dw_pcie_link_get_max_link_width(pci);
			int lane;

			for (lane = 0; lane < num_lanes; lane++) {
				dw_pcie_writel_dbi(pci,
						   rasdes_cap + PCIE_RAS_DES_SD_EQ_CONTROL1,
						   lane & PCIE_RAS_DES_SD_EQ_CONTROL1_LANE_SEL);

				val = dw_pcie_readl_dbi(pci,
							rasdes_cap + PCIE_RAS_DES_SD_EQ_STATUS2);
				len += qcom_pcie_dump_lane_reg_val(buf + len, buf_size - len, &col,
							lane,
							rasdes_cap + PCIE_RAS_DES_SD_EQ_STATUS2,
							val);

				val = dw_pcie_readl_dbi(pci,
							rasdes_cap + PCIE_RAS_DES_SD_EQ_STATUS3);
				len += qcom_pcie_dump_lane_reg_val(buf + len, buf_size - len, &col,
							lane,
							rasdes_cap + PCIE_RAS_DES_SD_EQ_STATUS3,
							val);
			}
		}
	}

	if (col % QCOM_PCIE_DUMP_REGS_PER_LINE)
		len += scnprintf(buf + len, buf_size - len, "\n");

dump_parf:
	len += scnprintf(buf + len, buf_size - len, "\nPARF registers:\n");
	if (pcie->cfg->has_ext_parf_regs)
		len += qcom_pcie_dump_reg_table(pcie->parf,
						qcom_pcie_ext_parf_dump_regs,
						ARRAY_SIZE(qcom_pcie_ext_parf_dump_regs),
						buf + len, buf_size - len);
	else
		len += qcom_pcie_dump_reg_table(pcie->parf,
						qcom_pcie_parf_dump_regs,
						ARRAY_SIZE(qcom_pcie_parf_dump_regs),
						buf + len, buf_size - len);

	if (pcie->cfg->has_link_down_recovery) {
		int q2a_col = 0;

		len += scnprintf(buf + len, buf_size - len,
				 "Q2A Link Down blocker:\n");
		len += qcom_pcie_dump_reg_val(buf + len, buf_size - len,
					      &q2a_col,
					      PARF_LINK_DOWN_ECAM_BLOCK_STATUS,
					      readl_relaxed(pcie->parf +
							    PARF_LINK_DOWN_ECAM_BLOCK_STATUS));
		if (q2a_col % QCOM_PCIE_DUMP_REGS_PER_LINE)
			len += scnprintf(buf + len, buf_size - len, "\n");
	}

	if (!pcie->mhi)
		return len;

	len += scnprintf(buf + len, buf_size - len, "\nMHI registers:\n");
	len += qcom_pcie_dump_reg_table(pcie->mhi, qcom_pcie_mhi_dump_regs,
					ARRAY_SIZE(qcom_pcie_mhi_dump_regs),
					buf + len, buf_size - len);

	return len;
}

/*
 * qcom_pcie_dump_regs - capture a PCIe register dump on Link Down.
 *
 * If a storage endpoint is present downstream, print the dump directly to
 * dmesg via dev_err() so it is immediately visible even if userspace is not
 * available to read a devcoredump after a storage failure. Otherwise hand
 * the buffer to the devcoredump framework so it is accessible under
 * /sys/class/devcoredump/ for offline analysis.
 */
static void qcom_pcie_dump_regs(struct qcom_pcie *pcie, u32 irq_status)
{
	struct device *dev = pcie->pci->dev;
	bool storage_ep = qcom_pcie_has_storage_ep(pcie);
	char *buf;
	size_t len;

	buf = vmalloc(QCOM_PCIE_DUMP_BUF_SIZE);
	if (!buf)
		return;

	len = qcom_pcie_fill_dump_buf(pcie, buf, QCOM_PCIE_DUMP_BUF_SIZE,
				      irq_status, false);

	if (storage_ep) {
		dev_err(dev, "PCIe Link Down register dump:\n%s", buf);
		vfree(buf);
	} else {
		dev_coredumpv(dev, buf, len, GFP_KERNEL);
	}
}

static int qcom_pcie_regdump_show(struct seq_file *s, void *data)
{
	struct qcom_pcie *pcie = (struct qcom_pcie *)dev_get_drvdata(s->private);
	struct dw_pcie_rp *pp = &pcie->pci->pp;
	bool dump_dbi;
	char *buf;
	size_t len;

	buf = vmalloc(QCOM_PCIE_DUMP_BUF_SIZE);
	if (!buf)
		return -ENOMEM;

	dump_dbi = !READ_ONCE(pp->cfg_access_blocked);
	len = qcom_pcie_fill_dump_buf(pcie, buf, QCOM_PCIE_DUMP_BUF_SIZE,
				      readl_relaxed(pcie->parf +
						    PARF_INT_ALL_STATUS),
				      dump_dbi);
	seq_write(s, buf, len);

	vfree(buf);

	return 0;
}

/*
 * Qcom PCIe controllers only support one Root Port per controller instance. So
 * this function ignores the 'pci_dev' associated with the Root Port and just
 * resets the host bridge, which in turn resets the Root Port also.
 */
static int qcom_pcie_link_down_hot_reset(struct qcom_pcie *pcie)
{
	struct qcom_pcie_port *port;
	int deassert_ret;
	int ret;

	/* Old DTs rely on the full host reset below. */
	if (!pcie->link_down_reset)
		return 0;

	ret = reset_control_assert(pcie->link_down_reset);
	if (ret)
		return ret;

	list_for_each_entry(port, &pcie->ports, list) {
		ret = phy_reset(port->phy);
		if (ret)
			break;
	}

	deassert_ret = reset_control_deassert(pcie->link_down_reset);
	if (!ret)
		ret = deassert_ret;

	return ret;
}

static void qcom_pcie_disable_global_irq(struct qcom_pcie *pcie)
{
	if (!pcie->global_irq || !pcie->global_irq_enabled)
		return;

	disable_irq(pcie->global_irq);
	pcie->global_irq_enabled = false;
}

static void qcom_pcie_enable_global_irq(struct qcom_pcie *pcie)
{
	if (!pcie->global_irq || pcie->global_irq_enabled)
		return;

	pcie->global_irq_enabled = true;
	enable_irq(pcie->global_irq);
}

static int qcom_pcie_reset_root_port(struct pci_host_bridge *bridge,
				  struct pci_dev *pdev)
{
	struct device *dev = bridge->dev.parent;
	struct qcom_pcie *pcie = dev_get_drvdata(dev);
	struct dw_pcie *pci = pcie->pci;
	struct dw_pcie_rp *pp = &pci->pp;
	u32 clk_req_override;
	u32 cgc_disable;
	u32 val;
	int ret;

	if (READ_ONCE(pcie->shutting_down))
		return -ESHUTDOWN;

	/* Fatal AER may reach this callback without a preceding Link Down IRQ. */
	if (!READ_ONCE(pp->cfg_access_blocked)) {
		reinit_completion(&pcie->link_req_rst);
		qcom_pcie_block_link_down_accesses(pcie);
	}

	/*
	 * Allow the last transaction to reach the subordinate interface before
	 * checking FLUSH_COMPLETED. The TRM requires Tstop to be at least 1 ms;
	 * the Q2A state register is for debug only and is not a completion
	 * handshake.
	 */
	usleep_range(LINK_DOWN_TSTOP_US, LINK_DOWN_TSTOP_US + 100);

	ret = readl_relaxed_poll_timeout(pcie->parf + PARF_STATUS, val,
					 val & FLUSH_COMPLETED, 10,
					 LINK_DOWN_FLUSH_TIMEOUT_US);
	if (ret) {
		dev_err(dev, "Flush completion failed: status=%#x q2a=%#x: %d\n",
			val, readl_relaxed(pcie->parf +
					   PARF_LINK_DOWN_ECAM_BLOCK_STATUS), ret);
		goto err_disable_irq;
	}

	/* Leave flush mode only after Q2A and the AXI interface have drained. */
	val = readl(pcie->parf + PARF_LTSSM);
	val |= SW_CLEAR_FLUSH_MODE;
	writel(val, pcie->parf + PARF_LTSSM);

	ret = readl_relaxed_poll_timeout(pcie->parf + PARF_LTSSM, val,
					 !(val & FLUSH_MODE), 10,
					 LINK_DOWN_FLUSH_TIMEOUT_US);
	if (ret) {
		dev_err(dev, "Flush mode clear failed: ltssm=%#x q2a=%#x: %d\n",
			val, readl_relaxed(pcie->parf +
					   PARF_LINK_DOWN_ECAM_BLOCK_STATUS), ret);
		goto err_disable_irq;
	}

	/*
	 * Force the subordinate bridge clock request high to disable GCC's
	 * power-active gating, then disable the internal manager/subordinate CGCs.
	 * Both are required for LINK_REQ_RST_FLUSH to reach the global interrupt.
	 */
	val = readl(pcie->parf + PARF_LINK_DOWN_ECAM_BLOCK);
	clk_req_override = val & (LINK_DOWN_ECAM_CLK_REQ_OVERRIDE |
				  LINK_DOWN_ECAM_CLK_REQ_OVERRIDE_VAL);
	val |= LINK_DOWN_ECAM_CLK_REQ_OVERRIDE |
	       LINK_DOWN_ECAM_CLK_REQ_OVERRIDE_VAL;
	writel(val, pcie->parf + PARF_LINK_DOWN_ECAM_BLOCK);

	val = readl(pcie->parf + PARF_SYS_CTRL);
	cgc_disable = val & (MSTR_ACLK_CGC_DIS | SLV_ACLK_CGC_DIS);
	val |= MSTR_ACLK_CGC_DIS | SLV_ACLK_CGC_DIS;
	writel(val, pcie->parf + PARF_SYS_CTRL);

	if (pcie->global_irq && pcie->global_irq_enabled) {
		if (wait_for_completion_timeout(&pcie->link_req_rst,
						msecs_to_jiffies(LINK_REQ_RST_TIMEOUT_MS)))
			ret = 0;
		else
			ret = -ETIMEDOUT;
	} else {
		ret = readl_relaxed_poll_timeout(pcie->parf +
						 PARF_INT_ALL_STATUS, val,
						 val & PARF_INT_LINK_REQ_RST_FLUSH,
						 10,
						 LINK_REQ_RST_TIMEOUT_MS * 1000);
		if (!ret)
			writel_relaxed(PARF_INT_LINK_REQ_RST_FLUSH,
				       pcie->parf + PARF_INT_ALL_CLEAR);
	}
	if (ret) {
		/* Cover an event that was latched before its IRQ was observed. */
		val = readl_relaxed(pcie->parf + PARF_INT_ALL_STATUS);
		if (val & PARF_INT_LINK_REQ_RST_FLUSH) {
			writel_relaxed(PARF_INT_LINK_REQ_RST_FLUSH,
				       pcie->parf + PARF_INT_ALL_CLEAR);
			ret = 0;
		}
	}

	/* Restore the GCC vote override before re-enabling the internal CGCs. */
	val = readl(pcie->parf + PARF_LINK_DOWN_ECAM_BLOCK);
	val &= ~(LINK_DOWN_ECAM_CLK_REQ_OVERRIDE |
		 LINK_DOWN_ECAM_CLK_REQ_OVERRIDE_VAL);
	val |= clk_req_override;
	writel(val, pcie->parf + PARF_LINK_DOWN_ECAM_BLOCK);

	val = readl(pcie->parf + PARF_SYS_CTRL);
	val &= ~(MSTR_ACLK_CGC_DIS | SLV_ACLK_CGC_DIS);
	val |= cgc_disable;
	writel(val, pcie->parf + PARF_SYS_CTRL);

	if (ret) {
		dev_err(dev, "Link request reset timed out: status=%#x q2a=%#x\n",
			readl_relaxed(pcie->parf + PARF_INT_ALL_STATUS),
			readl_relaxed(pcie->parf +
				      PARF_LINK_DOWN_ECAM_BLOCK_STATUS));
		goto err_disable_irq;
	}

	/* Reset-generated events are stale and must not queue another recovery. */
	qcom_pcie_disable_global_irq(pcie);

	ret = qcom_pcie_link_down_hot_reset(pcie);
	if (ret) {
		dev_err(dev, "Link Down hot reset failed: %d\n", ret);
		goto err_disable_irq;
	}

	qcom_pcie_host_deinit(pp);

	ret = qcom_pcie_host_init(pp);
	if (ret) {
		dev_err(dev, "Host init failed\n");
		goto err_disable_irq;
	}

	ret = dw_pcie_setup_rc(pp);
	if (ret)
		goto err_disable_irq;

	/* Drop only recovery events generated by the reset itself. */
	if (pcie->global_irq)
		writel_relaxed(PARF_INT_ALL_LINK_DOWN |
				PARF_INT_LINK_REQ_RST_FLUSH,
			       pcie->parf + PARF_INT_ALL_CLEAR);

	qcom_pcie_start_link(pci);

	ret = dw_pcie_wait_for_link(pci);
	if (ret)
		goto err_disable_irq;

	/* PARF_INT_ALL_MASK is non-sticky; restore it after link training. */
	if (pcie->global_irq) {
		writel_relaxed(PARF_INT_ALL_LINK_DOWN |
				PARF_INT_LINK_REQ_RST_FLUSH,
				pcie->parf + PARF_INT_ALL_MASK);

		val = readl_relaxed(pcie->parf + PARF_INT_ALL_STATUS);
		if ((val & PARF_INT_ALL_LINK_DOWN) || !dw_pcie_link_up(pci)) {
			atomic_long_or(val | PARF_INT_ALL_LINK_DOWN,
				       &pcie->global_irq_status);
			dev_err(dev, "Link went down again during recovery: status=%#x\n",
				val);
			ret = -ENOLINK;
			goto err_disable_irq;
		}

		writel_relaxed(val & PARF_INT_LINK_REQ_RST_FLUSH,
			       pcie->parf + PARF_INT_ALL_CLEAR);
	}

	if (READ_ONCE(pcie->shutting_down)) {
		ret = -ESHUTDOWN;
		goto err_disable_irq;
	}

	qcom_pcie_unblock_accesses(pcie);
	if (pcie->global_irq) {
		atomic_long_set(&pcie->global_irq_status, 0);
		qcom_pcie_enable_global_irq(pcie);
	}

	dev_dbg(dev, "Root Port reset completed\n");

	return 0;

err_disable_irq:
	qcom_pcie_disable_global_irq(pcie);

	return ret;
}

static int qcom_pcie_link_transition_count(struct seq_file *s, void *data)
{
	struct qcom_pcie *pcie = (struct qcom_pcie *)dev_get_drvdata(s->private);

	seq_printf(s, "L0s transition count: %u\n",
		   readl_relaxed(pcie->mhi + PARF_DEBUG_CNT_PM_LINKST_IN_L0S));

	seq_printf(s, "L1 transition count: %u\n",
		   readl_relaxed(pcie->mhi + PARF_DEBUG_CNT_PM_LINKST_IN_L1));

	seq_printf(s, "L1.1 transition count: %u\n",
		   readl_relaxed(pcie->mhi + PARF_DEBUG_CNT_AUX_CLK_IN_L1SUB_L1));

	seq_printf(s, "L1.2 transition count: %u\n",
		   readl_relaxed(pcie->mhi + PARF_DEBUG_CNT_AUX_CLK_IN_L1SUB_L2));

	seq_printf(s, "L2 transition count: %u\n",
		   readl_relaxed(pcie->mhi + PARF_DEBUG_CNT_PM_LINKST_IN_L2));

	return 0;
}

static void qcom_pcie_init_debugfs(struct qcom_pcie *pcie)
{
	struct dw_pcie *pci = pcie->pci;
	struct device *dev = pci->dev;
	char *name;

	name = devm_kasprintf(dev, GFP_KERNEL, "%pOFP", dev->of_node);
	if (!name)
		return;

	pcie->debugfs = debugfs_create_dir(name, NULL);
	debugfs_create_devm_seqfile(dev, "link_transition_count", pcie->debugfs,
				    qcom_pcie_link_transition_count);
	debugfs_create_devm_seqfile(dev, "regdump", pcie->debugfs,
				    qcom_pcie_regdump_show);
}

static void qcom_pcie_link_down_work(struct work_struct *work)
{
	struct qcom_pcie *pcie = container_of(work, struct qcom_pcie,
					      link_down_work);
	struct dw_pcie_rp *pp = &pcie->pci->pp;
	struct device *dev = pcie->pci->dev;
	struct pci_dev *port;
	unsigned long status = atomic_long_xchg(&pcie->global_irq_status, 0);

	if (!(status & PARF_INT_ALL_LINK_DOWN))
		return;

	dev_dbg(dev, "Received Link down event, status=%#lx\n", status);

	/*
	 * Reboot/shutdown teardown must not start a new Root Port recovery.
	 * The hardware blocker has already stopped downstream accesses, so
	 * propagate the channel state before endpoint shutdown paths try to
	 * access MMIO (for example, an MSI-X table).
	 */
	if (READ_ONCE(pcie->shutting_down)) {
		pci_walk_bus(pp->bridge->bus,
			     qcom_pcie_mark_device_frozen, NULL);
		dev_dbg(dev, "Skipping Link Down recovery during system shutdown\n");
		return;
	}

	qcom_pcie_dump_regs(pcie, status);
	if (READ_ONCE(pcie->shutting_down))
		return;
	if (!READ_ONCE(pp->cfg_access_blocked))
		return;

	for_each_pci_bridge(port, pp->bridge->bus) {
		if (pci_pcie_type(port) == PCI_EXP_TYPE_ROOT_PORT)
			pci_host_handle_link_down(port);
	}
}

static irqreturn_t qcom_pcie_global_irq(int irq, void *data)
{
	struct qcom_pcie *pcie = data;
	struct dw_pcie_rp *pp = &pcie->pci->pp;
	u32 handled;
	bool link_down;
	bool start_recovery = false;
	u32 status;

	status = readl_relaxed(pcie->parf + PARF_INT_ALL_STATUS);
	if (!status)
		return IRQ_NONE;

	handled = status & (PARF_INT_ALL_LINK_DOWN |
			    PARF_INT_LINK_REQ_RST_FLUSH);
	if (!handled)
		return IRQ_NONE;

	link_down = handled & PARF_INT_ALL_LINK_DOWN;
	if (link_down) {
		start_recovery = !READ_ONCE(pp->cfg_access_blocked);
		if (start_recovery)
			reinit_completion(&pcie->link_req_rst);
		qcom_pcie_block_link_down_accesses(pcie);
		atomic_long_or(status, &pcie->global_irq_status);
	}

	if (handled & PARF_INT_LINK_REQ_RST_FLUSH)
		complete(&pcie->link_req_rst);

	/* MSI diagnostic bits and unrelated events are not owned by this handler. */
	writel_relaxed(handled, pcie->parf + PARF_INT_ALL_CLEAR);

	/* The work item either recovers the hierarchy or marks it offline. */
	if (start_recovery)
		schedule_work(&pcie->link_down_work);

	return IRQ_HANDLED;
}

static void qcom_pcie_cancel_link_down_work(void *data)
{
	struct qcom_pcie *pcie = data;

	cancel_work_sync(&pcie->link_down_work);
}

static void qcom_pci_free_msi(void *ptr)
{
	struct dw_pcie_rp *pp = (struct dw_pcie_rp *)ptr;

	if (pp && pp->use_imsi_rx)
		dw_pcie_free_msi(pp);
}

static int qcom_pcie_ecam_host_init(struct pci_config_window *cfg)
{
	struct device *dev = cfg->parent;
	struct dw_pcie_rp *pp;
	struct dw_pcie *pci;
	int ret;

	pci = devm_kzalloc(dev, sizeof(*pci), GFP_KERNEL);
	if (!pci)
		return -ENOMEM;

	pci->dev = dev;
	pp = &pci->pp;
	pci->dbi_base = cfg->win;
	pp->num_vectors = MSI_DEF_NUM_VECTORS;

	ret = dw_pcie_msi_host_init(pp);
	if (ret)
		return ret;

	pp->use_imsi_rx = true;
	dw_pcie_msi_init(pp);

	return devm_add_action_or_reset(dev, qcom_pci_free_msi, pp);
}

static const struct pci_ecam_ops pci_qcom_ecam_ops = {
	.init		= qcom_pcie_ecam_host_init,
	.pci_ops	= {
		.map_bus	= pci_ecam_map_bus,
		.read		= pci_generic_config_read,
		.write		= pci_generic_config_write,
	}
};

static bool qcom_pcie_is_bridge_node(struct device_node *np)
{
	if (!of_node_is_type(np, "pci"))
		return false;

	return of_property_present(np, "bus-range") ||
	       of_device_is_compatible(np, "pciclass,0604");
}

/* Parse PERST# from all PCIe bridge nodes starting from @np */
static int qcom_pcie_parse_perst(struct qcom_pcie *pcie,
				 struct qcom_pcie_port *port,
				 struct device_node *np)
{
	struct device *dev = pcie->pci->dev;
	struct qcom_pcie_perst *perst;
	struct gpio_desc *reset;
	int ret;

	if (!of_find_property(np, "reset-gpios", NULL))
		goto parse_child_node;

	reset = devm_fwnode_gpiod_get(dev, of_fwnode_handle(np), "reset",
				      GPIOD_OUT_HIGH, "PERST#");
	if (IS_ERR(reset)) {
		/*
		 * FIXME: GPIOLIB currently supports exclusive GPIO access only.
		 * Non exclusive access is broken. But shared PERST# requires
		 * non-exclusive access. So once GPIOLIB properly supports it,
		 * implement it here.
		 */
		if (PTR_ERR(reset) == -EBUSY)
			dev_err(dev, "Shared PERST# is not supported\n");

		return PTR_ERR(reset);
	}

	perst = devm_kzalloc(dev, sizeof(*perst), GFP_KERNEL);
	if (!perst)
		return -ENOMEM;

	INIT_LIST_HEAD(&perst->list);
	perst->desc = reset;
	list_add_tail(&perst->list, &port->perst);

parse_child_node:
	for_each_available_child_of_node_scoped(np, child) {
		if (!qcom_pcie_is_bridge_node(child))
			continue;

		ret = qcom_pcie_parse_perst(pcie, port, child);
		if (ret)
			return ret;
	}

	return 0;
}

static int qcom_pcie_parse_port(struct qcom_pcie *pcie, struct device_node *node)
{
	struct device *dev = pcie->pci->dev;
	struct qcom_pcie_port *port;
	u32 num_lanes;
	struct phy *phy;
	int ret;

	phy = devm_of_phy_get(dev, node, NULL);
	if (IS_ERR(phy))
		return PTR_ERR(phy);

	if (!of_property_read_u32(dev->of_node, "num-lanes", &num_lanes))
		phy_set_bus_width(phy, num_lanes);

	port = devm_kzalloc(dev, sizeof(*port), GFP_KERNEL);
	if (!port)
		return -ENOMEM;

	ret = phy_init(phy);
	if (ret)
		return ret;

	INIT_LIST_HEAD(&port->perst);

	ret = qcom_pcie_parse_perst(pcie, port, node);
	if (ret)
		return ret;

	port->phy = phy;
	INIT_LIST_HEAD(&port->list);
	list_add_tail(&port->list, &pcie->ports);

	return 0;
}

static int qcom_pcie_parse_ports(struct qcom_pcie *pcie)
{
	struct qcom_pcie_perst *perst, *tmp_perst;
	struct qcom_pcie_port *port, *tmp_port;
	struct device *dev = pcie->pci->dev;
	int ret = -ENODEV;

	for_each_available_child_of_node_scoped(dev->of_node, of_port) {
		if (!of_node_is_type(of_port, "pci"))
			continue;
		ret = qcom_pcie_parse_port(pcie, of_port);
		if (ret)
			goto err_port_del;
	}

	return ret;

err_port_del:
	list_for_each_entry_safe(port, tmp_port, &pcie->ports, list) {
		list_for_each_entry_safe(perst, tmp_perst, &port->perst, list)
			list_del(&perst->list);
		phy_exit(port->phy);
		list_del(&port->list);
	}

	return ret;
}

static int qcom_pcie_parse_legacy_binding(struct qcom_pcie *pcie)
{
	struct device *dev = pcie->pci->dev;
	struct qcom_pcie_perst *perst;
	struct qcom_pcie_port *port;
	struct gpio_desc *reset;
	u32 num_lanes;
	struct phy *phy;
	int ret;

	phy = devm_phy_optional_get(dev, "pciephy");
	if (IS_ERR(phy))
		return PTR_ERR(phy);

	if (phy && !of_property_read_u32(dev->of_node, "num-lanes", &num_lanes))
		phy_set_bus_width(phy, num_lanes);

	reset = devm_gpiod_get_optional(dev, "perst", GPIOD_OUT_HIGH);
	if (IS_ERR(reset))
		return PTR_ERR(reset);

	ret = phy_init(phy);
	if (ret)
		return ret;

	port = devm_kzalloc(dev, sizeof(*port), GFP_KERNEL);
	if (!port)
		return -ENOMEM;

	perst = devm_kzalloc(dev, sizeof(*perst), GFP_KERNEL);
	if (!perst)
		return -ENOMEM;

	port->phy = phy;
	INIT_LIST_HEAD(&port->list);
	list_add_tail(&port->list, &pcie->ports);

	perst->desc = reset;
	INIT_LIST_HEAD(&port->perst);
	INIT_LIST_HEAD(&perst->list);
	list_add_tail(&perst->list, &port->perst);

	return 0;
}

static int qcom_pcie_probe(struct platform_device *pdev)
{
	struct qcom_pcie_perst *perst, *tmp_perst;
	struct qcom_pcie_port *port, *tmp_port;
	const struct qcom_pcie_cfg *pcie_cfg;
	unsigned long max_freq = ULONG_MAX;
	struct device *dev = &pdev->dev;
	struct dev_pm_opp *opp;
	struct qcom_pcie *pcie;
	struct dw_pcie_rp *pp;
	struct resource *res;
	struct dw_pcie *pci;
	int ret, irq;

	pcie_cfg = of_device_get_match_data(dev);
	if (!pcie_cfg) {
		dev_err(dev, "No platform data\n");
		return -ENODATA;
	}

	if (!pcie_cfg->firmware_managed && !pcie_cfg->ops) {
		dev_err(dev, "No platform ops\n");
		return -ENODATA;
	}

	pm_runtime_enable(dev);
	ret = pm_runtime_get_sync(dev);
	if (ret < 0)
		goto err_pm_runtime_put;

	if (pcie_cfg->firmware_managed) {
		struct pci_host_bridge *bridge;
		struct pci_config_window *cfg;

		bridge = devm_pci_alloc_host_bridge(dev, 0);
		if (!bridge) {
			ret = -ENOMEM;
			goto err_pm_runtime_put;
		}

		/* Parse and map our ECAM configuration space area */
		cfg = pci_host_common_ecam_create(dev, bridge,
				&pci_qcom_ecam_ops);
		if (IS_ERR(cfg)) {
			ret = PTR_ERR(cfg);
			goto err_pm_runtime_put;
		}

		bridge->sysdata = cfg;
		bridge->ops = (struct pci_ops *)&pci_qcom_ecam_ops.pci_ops;
		bridge->msi_domain = true;

		ret = pci_host_probe(bridge);
		if (ret)
			goto err_pm_runtime_put;

		return 0;
	}

	pcie = devm_kzalloc(dev, sizeof(*pcie), GFP_KERNEL);
	if (!pcie) {
		ret = -ENOMEM;
		goto err_pm_runtime_put;
	}

	pci = devm_kzalloc(dev, sizeof(*pci), GFP_KERNEL);
	if (!pci) {
		ret = -ENOMEM;
		goto err_pm_runtime_put;
	}

	INIT_LIST_HEAD(&pcie->ports);

	pci->dev = dev;
	pci->ops = &dw_pcie_ops;
	pp = &pci->pp;

	pcie->pci = pci;

	pcie->cfg = pcie_cfg;
	if (pcie->cfg->has_link_down_recovery) {
		INIT_WORK(&pcie->link_down_work, qcom_pcie_link_down_work);
		init_completion(&pcie->link_req_rst);
		atomic_long_set(&pcie->global_irq_status, 0);
	}

	pcie->parf = devm_platform_ioremap_resource_byname(pdev, "parf");
	if (IS_ERR(pcie->parf)) {
		ret = PTR_ERR(pcie->parf);
		goto err_pm_runtime_put;
	}

	/* MHI region is optional */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "mhi");
	if (res) {
		pcie->mhi = devm_ioremap_resource(dev, res);
		if (IS_ERR(pcie->mhi)) {
			ret = PTR_ERR(pcie->mhi);
			goto err_pm_runtime_put;
		}
	}

	/* OPP table is optional */
	ret = devm_pm_opp_of_add_table(dev);
	if (ret && ret != -ENODEV) {
		dev_err_probe(dev, ret, "Failed to add OPP table\n");
		goto err_pm_runtime_put;
	}

	/*
	 * Before the PCIe link is initialized, vote for highest OPP in the OPP
	 * table, so that we are voting for maximum voltage corner for the
	 * link to come up in maximum supported speed. At the end of the
	 * probe(), OPP will be updated using qcom_pcie_icc_opp_update().
	 */
	if (!ret) {
		opp = dev_pm_opp_find_freq_floor(dev, &max_freq);
		if (IS_ERR(opp)) {
			ret = PTR_ERR(opp);
			dev_err_probe(pci->dev, ret,
				      "Unable to find max freq OPP\n");
			goto err_pm_runtime_put;
		} else {
			ret = dev_pm_opp_set_opp(dev, opp);
		}

		dev_pm_opp_put(opp);
		if (ret) {
			dev_err_probe(pci->dev, ret,
				      "Failed to set OPP for freq %lu\n",
				      max_freq);
			goto err_pm_runtime_put;
		}

		pcie->use_pm_opp = true;
	} else {
		/* Skip ICC init if OPP is supported as it is handled by OPP */
		ret = qcom_pcie_icc_init(pcie);
		if (ret)
			goto err_pm_runtime_put;
	}

	ret = pcie->cfg->ops->get_resources(pcie);
	if (ret)
		goto err_pm_runtime_put;

	pp->ops = &qcom_pcie_dw_ops;

	ret = qcom_pcie_parse_ports(pcie);
	if (ret) {
		if (ret != -ENODEV) {
			dev_err_probe(pci->dev, ret,
				      "Failed to parse Root Port: %d\n", ret);
			goto err_pm_runtime_put;
		}

		/*
		 * In the case of properties not populated in Root Port node,
		 * fallback to the legacy method of parsing the Host Bridge
		 * node. This is to maintain DT backwards compatibility.
		 */
		ret = qcom_pcie_parse_legacy_binding(pcie);
		if (ret)
			goto err_pm_runtime_put;
	}

	platform_set_drvdata(pdev, pcie);

	ret = dw_pcie_host_init(pp);
	if (ret) {
		dev_err_probe(dev, ret, "cannot initialize host\n");
		goto err_phy_exit;
	}

	irq = platform_get_irq_byname_optional(pdev, "global");
	if (irq < 0 && irq != -ENXIO) {
		ret = dev_err_probe(dev, irq, "Failed to get Global IRQ\n");
		goto err_host_deinit;
	}

	if (pcie->cfg->has_link_down_recovery) {
		pcie->reboot_nb.notifier_call = qcom_pcie_reboot_notifier;
		ret = devm_register_reboot_notifier(dev, &pcie->reboot_nb);
		if (ret) {
			dev_err_probe(dev, ret,
				      "Failed to register reboot notifier\n");
			goto err_host_deinit;
		}
	}

	if (irq > 0) {
		const char *name;

		if (!pcie->cfg->has_link_down_recovery) {
			dev_warn(dev, "Global IRQ present without Link Down recovery support\n");
			goto skip_global_irq;
		}

		name = devm_kasprintf(dev, GFP_KERNEL, "qcom_pcie_global_irq%d",
				      pci_domain_nr(pp->bridge->bus));
		if (!name) {
			ret = -ENOMEM;
			goto err_host_deinit;
		}

		/*
		 * The IRQ devres is released before this action, so no handler can
		 * queue new work while the action drains the final work item.
		 */
		ret = devm_add_action_or_reset(dev,
					       qcom_pcie_cancel_link_down_work, pcie);
		if (ret)
			goto err_host_deinit;

		pcie->global_irq = irq;
		ret = devm_request_irq(dev, irq, qcom_pcie_global_irq,
				       IRQF_NO_AUTOEN, name, pcie);
		if (ret) {
			pcie->global_irq = 0;
			dev_err_probe(dev, ret, "Failed to request Global IRQ\n");
			goto err_host_deinit;
		}

		/* Preserve a Link Down latched while the host bus was enumerated. */
		writel_relaxed(PARF_INT_LINK_REQ_RST_FLUSH,
			       pcie->parf + PARF_INT_ALL_CLEAR);
		writel_relaxed(PARF_INT_ALL_LINK_DOWN |
				PARF_INT_LINK_REQ_RST_FLUSH,
				pcie->parf + PARF_INT_ALL_MASK);

		pcie->global_irq_enabled = true;
		enable_irq(irq);
	}

skip_global_irq:

	qcom_pcie_icc_opp_update(pcie);

	if (pcie->mhi)
		qcom_pcie_init_debugfs(pcie);

	return 0;

err_host_deinit:
	dw_pcie_host_deinit(pp);
err_phy_exit:
	list_for_each_entry_safe(port, tmp_port, &pcie->ports, list) {
		list_for_each_entry_safe(perst, tmp_perst, &port->perst, list)
			list_del(&perst->list);
		phy_exit(port->phy);
		list_del(&port->list);
	}
err_pm_runtime_put:
	pm_runtime_put(dev);
	pm_runtime_disable(dev);

	return ret;
}

static int qcom_pcie_suspend_noirq(struct device *dev)
{
	struct qcom_pcie *pcie;
	int ret = 0;

	pcie = dev_get_drvdata(dev);
	if (!pcie)
		return 0;

	/*
	 * Set minimum bandwidth required to keep data path functional during
	 * suspend.
	 */
	if (pcie->icc_mem) {
		ret = icc_set_bw(pcie->icc_mem, 0, kBps_to_icc(1));
		if (ret) {
			dev_err(dev,
				"Failed to set bandwidth for PCIe-MEM interconnect path: %d\n",
				ret);
			return ret;
		}
	}

	/*
	 * Turn OFF the resources only for controllers without active PCIe
	 * devices. For controllers with active devices, the resources are kept
	 * ON and the link is expected to be in L0/L1 (sub)states.
	 *
	 * Turning OFF the resources for controllers with active PCIe devices
	 * will trigger access violation during the end of the suspend cycle,
	 * as kernel tries to access the PCIe devices config space for masking
	 * MSIs.
	 *
	 * Also, it is not desirable to put the link into L2/L3 state as that
	 * implies VDD supply will be removed and the devices may go into
	 * powerdown state. This will affect the lifetime of the storage devices
	 * like NVMe.
	 */
	if (READ_ONCE(pcie->pci->pp.cfg_access_blocked) ||
	    !dw_pcie_link_up(pcie->pci)) {
		qcom_pcie_host_deinit(&pcie->pci->pp);
		pcie->suspended = true;
	}

	/*
	 * Only disable CPU-PCIe interconnect path if the suspend is non-S2RAM.
	 * Because on some platforms, DBI access can happen very late during the
	 * S2RAM and a non-active CPU-PCIe interconnect path may lead to NoC
	 * error.
	 */
	if (pm_suspend_target_state != PM_SUSPEND_MEM) {
		ret = icc_disable(pcie->icc_cpu);
		if (ret)
			dev_err(dev, "Failed to disable CPU-PCIe interconnect path: %d\n", ret);

		if (pcie->use_pm_opp)
			dev_pm_opp_set_opp(pcie->pci->dev, NULL);
	}
	return ret;
}

static int qcom_pcie_resume_noirq(struct device *dev)
{
	struct qcom_pcie *pcie;
	int ret;

	pcie = dev_get_drvdata(dev);
	if (!pcie)
		return 0;

	if (pm_suspend_target_state != PM_SUSPEND_MEM) {
		ret = icc_enable(pcie->icc_cpu);
		if (ret) {
			dev_err(dev, "Failed to enable CPU-PCIe interconnect path: %d\n", ret);
			return ret;
		}
	}

	if (pcie->suspended) {
		ret = qcom_pcie_host_init(&pcie->pci->pp);
		if (ret)
			return ret;

		pcie->suspended = false;
	}

	qcom_pcie_icc_opp_update(pcie);

	return 0;
}

static const struct of_device_id qcom_pcie_match[] = {
	{ .compatible = "qcom,pcie-apq8064", .data = &cfg_2_1_0 },
	{ .compatible = "qcom,pcie-apq8084", .data = &cfg_1_0_0 },
	{ .compatible = "qcom,pcie-ipq4019", .data = &cfg_2_4_0 },
	{ .compatible = "qcom,pcie-ipq5018", .data = &cfg_2_9_0 },
	{ .compatible = "qcom,pcie-ipq6018", .data = &cfg_2_9_0 },
	{ .compatible = "qcom,pcie-ipq8064", .data = &cfg_2_1_0 },
	{ .compatible = "qcom,pcie-ipq8064-v2", .data = &cfg_2_1_0 },
	{ .compatible = "qcom,pcie-ipq8074", .data = &cfg_2_3_3 },
	{ .compatible = "qcom,pcie-ipq8074-gen3", .data = &cfg_2_9_0 },
	{ .compatible = "qcom,pcie-ipq9574", .data = &cfg_2_9_0 },
	{ .compatible = "qcom,pcie-msm8996", .data = &cfg_2_3_2 },
	{ .compatible = "qcom,pcie-qcs404", .data = &cfg_2_4_0 },
	{ .compatible = "qcom,pcie-sa8255p", .data = &cfg_fw_managed },
	{ .compatible = "qcom,pcie-sa8540p", .data = &cfg_sc8280xp },
	{ .compatible = "qcom,pcie-sa8775p", .data = &cfg_1_34_0},
	{ .compatible = "qcom,pcie-sc7280", .data = &cfg_1_9_0 },
	{ .compatible = "qcom,pcie-sc8180x", .data = &cfg_1_9_0 },
	{ .compatible = "qcom,pcie-sc8280xp", .data = &cfg_sc8280xp_link_down },
	{ .compatible = "qcom,pcie-sdm845", .data = &cfg_2_7_0 },
	{ .compatible = "qcom,pcie-sdx55", .data = &cfg_1_9_0 },
	{ .compatible = "qcom,pcie-sm8150", .data = &cfg_1_9_0 },
	{ .compatible = "qcom,pcie-sm8250", .data = &cfg_1_9_0 },
	{ .compatible = "qcom,pcie-sm8350", .data = &cfg_1_9_0 },
	{ .compatible = "qcom,pcie-sm8450-pcie0", .data = &cfg_1_9_0 },
	{ .compatible = "qcom,pcie-sm8450-pcie1", .data = &cfg_1_9_0 },
	{ .compatible = "qcom,pcie-sm8550", .data = &cfg_1_9_0 },
	{ .compatible = "qcom,pcie-x1e80100", .data = &cfg_sc8280xp },
	{ }
};

static void qcom_fixup_class(struct pci_dev *dev)
{
	dev->class = PCI_CLASS_BRIDGE_PCI_NORMAL;
}
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_QCOM, 0x0101, qcom_fixup_class);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_QCOM, 0x0104, qcom_fixup_class);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_QCOM, 0x0106, qcom_fixup_class);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_QCOM, 0x0107, qcom_fixup_class);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_QCOM, 0x0302, qcom_fixup_class);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_QCOM, 0x1000, qcom_fixup_class);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_QCOM, 0x1001, qcom_fixup_class);

static const struct dev_pm_ops qcom_pcie_pm_ops = {
	NOIRQ_SYSTEM_SLEEP_PM_OPS(qcom_pcie_suspend_noirq, qcom_pcie_resume_noirq)
};

static struct platform_driver qcom_pcie_driver = {
	.probe = qcom_pcie_probe,
	.driver = {
		.name = "qcom-pcie",
		.suppress_bind_attrs = true,
		.of_match_table = qcom_pcie_match,
		.pm = &qcom_pcie_pm_ops,
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
};
builtin_platform_driver(qcom_pcie_driver);
