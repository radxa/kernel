// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright (C) 2026 by RISCstar Solutions Corporation.  All rights reserved.
 *
 * Derived from code having the following copyrights:
 * Copyright (C) 2011-2012  Vayavya Labs Pvt Ltd
 * Copyright (C) 2025 Toshiba Electronic Devices & Storage Corporation
 */

#include <linux/auxiliary_bus.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/etherdevice.h>
#include <linux/gpio/consumer.h>
#include <linux/iopoll.h>
#include <linux/irqdomain.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/pcs/pcs-xpcs-regmap.h>
#include <linux/pcs/pcs-xpcs.h>
#include <linux/phy.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/stmmac.h>
#include <linux/types.h>
#include <linux/units.h>

#include <soc/toshiba/tc956x-dwmac.h>

#include "common.h"
#include "dwxgmac2.h"
#include "stmmac.h"

#define DRIVER_NAME			"dwmac-tc956x"

#define TC956X_PTP_CLOCK_RATE		(250 * HZ_PER_MHZ)
#define TC956X_MAC_CLOCK_RATE		(125 * HZ_PER_MHZ)

#define TC956X_RX_FIFO_KB		32	/* Shared by all RX queues */
#define TC956X_TX_FIFO_KB		8	/* Shared by all TX queues */
#define TC956X_RX_QUEUES		1
#define TC956X_TX_QUEUES		1

/* Fields and values for the EMACTL registers */
#define EMAC_SP_SEL_MASK		GENMASK(3, 0)

#define SP_SEL_2500BASEX		4
#define SP_SEL_SGMII_1000M		5
#define SP_SEL_SGMII_100M		6
#define SP_SEL_SGMII_10M		7
#define SP_SEL_USXGMII_10G_10G		8
#define SP_SEL_USXGMII_5G_10G		9
#define SP_SEL_USXGMII_2500M_10G	11
#define SP_SEL_USXGMII_2500M_2500M	13
#define EMAC_PHY_INF_SEL_MASK		GENMASK(5, 4)
#define PCS_CLK_PHY			1	/* Clock from PHY */
#define EMAC_INV_SGM_SIG_DET		BIT(6)	/* 1 = polarity inverted */
#define EMAC_LPIHWCLKEN			BIT(8)	/* 1 = low power mode */
#define MISC_CTL_OFFSET			0x1800
#define MISC_SP_ETH0_MASK		GENMASK(18, 16)
#define MISC_SP_ETH1_MASK		GENMASK(26, 24)
#define MISC_SP_ETH_1G			1
#define MISC_SP_ETH_100M			3
#define MISC_SP_ETH_10M			7

#define NRSTCTRL_OFFSET(_id)		(0x1008 + (_id) * 0x8)

#define RST_MAC_MACRST			BIT(7)

#define MAC_TX_CONFIG_OFFSET		0x0000
#define MAC_TX_CONFIG_TE		BIT(0)

#define MAC_RX_CONFIG_OFFSET		0x0004
#define MAC_RX_CONFIG_RE		BIT(0)

/* MSIGEN Registers */
#define MSI_OUT_EN_OFFSET		0x0000
#define MSI_MASK_SET_OFFSET		0x0008
#define MSI_MASK_CLR_OFFSET		0x000c
#define MSI_INT_STS_OFFSET		0x0010
#define MSI_VECT_SET_OFFSET(_src)	(0x0020 + ((_src) / 4) * 4)
#define MSI_VECT_SET_SHIFT(_src)	(((_src) % 4) * 8)
#define MSI_VECT_SET_MASK		GENMASK(4, 0)

enum msigen_hwirq {
	HWIRQ_LPI		= 0,
	HWIRQ_PMT		= 1,
	HWIRQ_EVENT		= 2,
	HWIRQ_TX0		= 3,
	HWIRQ_RX0		= 11,
	HWIRQ_XPCS		= 19,
	HWIRQ_PHY		= 20,
	HWIRQ_PFMAILBOX		= 21,
	HWIRQ_MSIREQ_PLS	= 24
};

#define HWIRQ_COUNT			25
#define TC956X_DMA_CHANS		8
#define MSI_VEC_MISC			1
#define MSI_VEC_TX(_ch)			(2 + (_ch))
#define MSI_VEC_RX(_ch)			(2 + TC956X_DMA_CHANS + (_ch))
#define MSI_VEC_COUNT			(2 + 2 * TC956X_DMA_CHANS)

/* Offset to the XPCS memory block, relative to the EMAC address range */
#define DWMAC_XPCS_OFFSET		0x3a00

/* Offset to the PMATOP memory block, relative to the EMAC address range */
#define DWMAC_PMATOP_OFFSET			0x4000

#define PMA_CML_GL_PM_CFG0			0x01b8
#define PMA_PCS_GL_PC_ST0			0x0168
#define PMA_PCS_GL_PC_ST0_PLL_VALID		BIT(0)

/*
 * Five sets three registers must be configured for PMA.  The HWT_REFCLK
 * registers are each separated by 0x14 bytes.  The Common0 configuration
 * registers are separated by 0x8 bytes.
 */
#define PMA_REG_COUNT				5

#define PMA_HWT_REFCK_R_EN			0x1080
#define PMA_HWT_REFCK_TERM_EN			0x1090
#define PMA_HWT_REFCK_STRIDE			0x0014

#define PMA_COMM_CFG_0_1			0x1888
#define PMA_COMM_CFG_0_1_STRIDE			0x0008

/* PMA_COMM_CFG_0_1 fields (WRITE_MASK is a field name) */
#define COMM_CFG_WRITE_MASK_MASK		GENMASK(16, 9)
#define WRITE_MASK_VALUE			0xf7	/* Power-on value */
#define COMM_CFG_ENABLE				BIT(8)
#define COMM_CFG_WRITE_DATA_MASK		GENMASK(7, 0)
#define WRITE_DATA_VALUE			0x04	/* Power-on value */

struct tc956x_msi_vec {
	struct tc956x_data *td;
	u32 src_mask;		/* MSIGEN sources on this vector */
	u8 vec;			/* MSI vector number */
};

/**
 * struct tc956x_data - Toshiba-specific platform data
 * @dev:		Device pointer
 * @irq_domain:		MSIGEN IRQ domain
 * @auxbus_data:	Pointer to data passed from the parent device
 * @plat:		Pointer to our stmmac platform data
 * @dma_cfg:		DMA config buffer used by plat_stmmacenet_data
 * @mdio_bus_data:	MDIO bus data used by plat_stmmacenet_data
 * @axi:		AXI data used by plat_stmmacenet_data
 * @desc:		DMA descriptor data used by mac_device_info
 * @dma:		DMA operations data used by mac_device_info
 */
struct tc956x_data {
	struct device *dev;
	struct irq_domain *irq_domain;
	struct tc956x_dwmac_data *auxbus_data;
	struct plat_stmmacenet_data *plat;
	bool mode_switch;

	struct tc956x_msi_vec msi_vec[MSI_VEC_COUNT];
	unsigned int msi_vec_used;

	phy_interface_t interface;

	int boot_speed;

	/* These three fields are used by the plat_stmmacenet_data structure */
	struct stmmac_dma_cfg dma_cfg;
	struct stmmac_mdio_bus_data mdio_bus_data;
	struct stmmac_axi axi;

	/* These two fields are used by the mac_device_info structure */
	struct stmmac_desc_ops desc;
	struct stmmac_dma_ops dma;
};

struct tc956x_mac_speed {
	phy_interface_t phy_interface;
	int speed;
	u32 sp_sel;
};

static struct tc956x_mac_speed mac_speed[] = {
	{ PHY_INTERFACE_MODE_USXGMII,	SPEED_10000, SP_SEL_USXGMII_10G_10G },
	{ PHY_INTERFACE_MODE_USXGMII,	SPEED_5000,  SP_SEL_USXGMII_5G_10G },
	{ PHY_INTERFACE_MODE_USXGMII,	SPEED_2500,  SP_SEL_USXGMII_2500M_10G },
	{ PHY_INTERFACE_MODE_USXGMII,	SPEED_1000,  SP_SEL_USXGMII_10G_10G },
	{ PHY_INTERFACE_MODE_USXGMII,	SPEED_100,   SP_SEL_USXGMII_5G_10G },
	{ PHY_INTERFACE_MODE_USXGMII,	SPEED_10,    SP_SEL_USXGMII_5G_10G },
	{ PHY_INTERFACE_MODE_2500BASEX,	SPEED_2500,  SP_SEL_2500BASEX },
	{ PHY_INTERFACE_MODE_1000BASEX,	SPEED_1000,  SP_SEL_SGMII_1000M },
	{ PHY_INTERFACE_MODE_SGMII,	SPEED_2500,  SP_SEL_2500BASEX },
	{ PHY_INTERFACE_MODE_SGMII,	SPEED_1000,  SP_SEL_SGMII_1000M },
	{ PHY_INTERFACE_MODE_SGMII,	SPEED_100,   SP_SEL_SGMII_100M },
	{ PHY_INTERFACE_MODE_SGMII,	SPEED_10,    SP_SEL_SGMII_10M },
};

/* TC956x uses indirect addressing so this need only describe a 1KiB range */
static const struct regmap_config xpcs_regmap_config = {
	.reg_bits	= 32,
	.val_bits	= 32,
	.reg_base	= 0x00,		/* Minimum XPCS reg offset */
	.max_register	= 0xff,		/* Register DW_VR_CSR_VIEWPORT */
	.reg_shift	= REGMAP_UPSHIFT(2),
};

static void tc956x_msigen_irq_handler(struct irq_desc *desc)
{
	struct tc956x_msi_vec *mv = irq_desc_get_handler_data(desc);
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct irq_chip_generic *gc;
	unsigned long status;
	unsigned int hwirq;

	gc = irq_get_domain_generic_chip(mv->td->irq_domain, 0);

	chained_irq_enter(chip, desc);

	status = irq_reg_readl(gc, MSI_INT_STS_OFFSET) & mv->src_mask;
	for_each_set_bit(hwirq, &status, HWIRQ_COUNT)
		generic_handle_domain_irq(mv->td->irq_domain, hwirq);

	/*
	 * Clear the MSI flag. Most interrupts within TC956X are level-high
	 * type. If any interrupts are still asserted then clearing this flag
	 * will cause the (edge-triggered) MSI to be regenerated.
	 */
	irq_reg_writel(gc, BIT(mv->vec), MSI_MASK_CLR_OFFSET);

	chained_irq_exit(chip, desc);
}

static void tc956x_msigen_route(struct irq_chip_generic *gc,
				const u8 *src_vec, unsigned int nsrc)
{
	unsigned int src, reg;
	u32 val;

	irq_reg_writel(gc, 0, MSI_OUT_EN_OFFSET);

	for (reg = 0; reg < DIV_ROUND_UP(nsrc, 4); reg++) {
		val = 0;
		for (src = reg * 4; src < min(nsrc, (reg + 1) * 4); src++)
			val |= (src_vec[src] & MSI_VECT_SET_MASK) <<
				MSI_VECT_SET_SHIFT(src);
		irq_reg_writel(gc, val, MSI_VECT_SET_OFFSET(reg * 4));
	}
}

static void tc956x_msigen_plan(struct tc956x_data *td, u8 *src_vec)
{
	unsigned int src, ch;

	if (td->auxbus_data->msigen_nvec < MSI_VEC_COUNT) {
		for (src = 0; src < HWIRQ_COUNT; src++)
			src_vec[src] = 0;
		td->msi_vec[0] = (struct tc956x_msi_vec){
			.td = td, .vec = 0,
			.src_mask = GENMASK(HWIRQ_COUNT - 1, 0),
		};
		td->msi_vec_used = 1;
		return;
	}

	for (src = 0; src < HWIRQ_COUNT; src++)
		src_vec[src] = MSI_VEC_MISC;
	for (ch = 0; ch < TC956X_DMA_CHANS; ch++) {
		src_vec[HWIRQ_TX0 + ch] = MSI_VEC_TX(ch);
		src_vec[HWIRQ_RX0 + ch] = MSI_VEC_RX(ch);
	}

	td->msi_vec[0] = (struct tc956x_msi_vec){
		.td = td, .vec = MSI_VEC_MISC,
		.src_mask = GENMASK(HWIRQ_EVENT, HWIRQ_LPI) |
			    GENMASK(HWIRQ_COUNT - 1, HWIRQ_XPCS),
	};
	td->msi_vec_used = 1;

	/* Interleaved so that TX and RX of the same channel index differ */
	for (ch = 0; ch < TC956X_DMA_CHANS; ch++) {
		td->msi_vec[td->msi_vec_used++] = (struct tc956x_msi_vec){
			.td = td, .vec = MSI_VEC_TX(ch),
			.src_mask = BIT(HWIRQ_TX0 + ch),
		};
		td->msi_vec[td->msi_vec_used++] = (struct tc956x_msi_vec){
			.td = td, .vec = MSI_VEC_RX(ch),
			.src_mask = BIT(HWIRQ_RX0 + ch),
		};
	}
}

static int tc956x_msigen_irq_chip_init(struct irq_chip_generic *gc)
{
	struct tc956x_data *td = gc->domain->host_data;
	u8 src_vec[HWIRQ_COUNT];
	u32 vec_used = 0;
	unsigned int i;

	gc->reg_base = td->auxbus_data->msigen;
	gc->chip_types[0].regs.mask = MSI_OUT_EN_OFFSET;
	gc->chip_types[0].chip.irq_mask = irq_gc_mask_clr_bit;
	gc->chip_types[0].chip.irq_unmask = irq_gc_mask_set_bit;

	/* Disable all interrupts */
	irq_reg_writel(gc, 0, MSI_OUT_EN_OFFSET);

	tc956x_msigen_plan(td, src_vec);
	tc956x_msigen_route(gc, src_vec, HWIRQ_COUNT);

	for (i = 0; i < td->msi_vec_used; i++)
		vec_used |= BIT(td->msi_vec[i].vec);

	irq_reg_writel(gc, ~vec_used & ~BIT(0), MSI_MASK_SET_OFFSET);
	irq_reg_writel(gc, vec_used, MSI_MASK_CLR_OFFSET);

	return 0;
}

static void tc956x_msigen_irq_chip_exit(struct irq_chip_generic *gc)
{
	irq_reg_writel(gc, 0, MSI_OUT_EN_OFFSET);
}

static int tc956x_msigen_irq_domain_init(struct irq_domain *irq_domain)
{
	struct tc956x_data *td = irq_domain->host_data;
	unsigned int i, cpu;
	int last = -1;

	if (td->msi_vec_used == 1) {
		for_each_cpu(cpu, cpu_online_mask)
			last = cpu;
		irq_set_affinity_and_hint(td->auxbus_data->msigen_irq,
					  cpumask_of(last > 0 ? last - 1 : 0));
		irq_set_chained_handler_and_data(td->auxbus_data->msigen_irq,
						 tc956x_msigen_irq_handler,
						 &td->msi_vec[0]);
		dev_info(td->dev, "%u MSI vector(s), sharing one interrupt\n",
			 td->auxbus_data->msigen_nvec);
		return 0;
	}

	cpu = cpumask_first(cpu_online_mask);
	for (i = 0; i < td->msi_vec_used; i++) {
		unsigned int irq = td->auxbus_data->msigen_irq +
				   td->msi_vec[i].vec;

		irq_set_chained_handler_and_data(irq,
						 tc956x_msigen_irq_handler,
						 &td->msi_vec[i]);

		/* Leave the shared low-rate vector wherever it lands */
		if (td->msi_vec[i].vec == MSI_VEC_MISC)
			continue;

		irq_set_affinity_and_hint(irq, cpumask_of(cpu));
		cpu = cpumask_next_wrap(cpu, cpu_online_mask);
	}

	dev_info(td->dev, "%u MSI vectors, one per DMA channel\n",
		 td->auxbus_data->msigen_nvec);

	return 0;
}

static void tc956x_msigen_irq_domain_exit(struct irq_domain *irq_domain)
{
	struct tc956x_data *td = irq_domain->host_data;
	unsigned int i;

	for (i = 0; i < td->msi_vec_used; i++) {
		unsigned int irq = td->auxbus_data->msigen_irq;

		if (td->msi_vec_used > 1)
			irq += td->msi_vec[i].vec;

		irq_set_chained_handler_and_data(irq, NULL, NULL);
	}
}

/* We have one IRQ chip instance with 25 IRQs in its domain */
static struct irq_domain *
tc956x_msigen_irq_domain_instantiate(struct tc956x_data *td)
{
	struct irq_domain_chip_generic_info dgc_info;
	struct irq_domain_info info;

	dgc_info.name = "tc956x-msigen";
	dgc_info.handler = handle_level_irq;
	dgc_info.irqs_per_chip = HWIRQ_COUNT;
	dgc_info.num_ct = 1;
	dgc_info.init = tc956x_msigen_irq_chip_init;
	dgc_info.exit = tc956x_msigen_irq_chip_exit;

	info.domain_flags = IRQ_DOMAIN_FLAG_DESTROY_GC;
	info.size = HWIRQ_COUNT;
	info.hwirq_max = HWIRQ_COUNT;
	info.ops = &irq_generic_chip_ops;
	info.host_data = td;
	info.dgc_info = &dgc_info;
	info.init = tc956x_msigen_irq_domain_init;
	info.exit = tc956x_msigen_irq_domain_exit;

	return devm_irq_domain_instantiate(td->dev, &info);
}

/**
 * tc956x_pma_init() - Initialize PMA
 * @td:	bsp_priv pointer
 *
 */
static int tc956x_pma_init(struct tc956x_data *td)
{
	const struct tc956x_chip *chip = td->auxbus_data->chip;
	u32 id = td->auxbus_data->mac_id;
	void __iomem *pmatop;
	int ret;
	u32 val;
	u32 i;

	pmatop = td->auxbus_data->emac + DWMAC_PMATOP_OFFSET;
	/*
	 * When we re-initialize the PMA then the reset will already have
	 * been deasserted. We must make sure the PMA reset is asserted before
	 * we change the clock settings.
	 */
	tc956x_reset_assert(chip, id, MAC_RESET_PMA);

	/* Power on CML buffer (0 = normal mode, 1 = power down) */
	writel(0, pmatop + PMA_CML_GL_PM_CFG0);

	/* This value switches clock from C0_REFCK to CLK_REF_I */
	val = u32_encode_bits(WRITE_MASK_VALUE, COMM_CFG_WRITE_MASK_MASK);
	val |= COMM_CFG_ENABLE;
	val |= u32_encode_bits(WRITE_DATA_VALUE, COMM_CFG_WRITE_DATA_MASK);

	for (i = 0; i < PMA_REG_COUNT; i++) {
		u32 offset =  i * PMA_HWT_REFCK_STRIDE;

		/* Disable C0_REFCK and 100 ohm termination */
		writel(0, pmatop + PMA_HWT_REFCK_R_EN + offset);
		writel(0, pmatop + PMA_HWT_REFCK_TERM_EN + offset);

		/* Switch clock from C0_REFCK to CLK_REF_I */
		offset =  i * PMA_COMM_CFG_0_1_STRIDE;
		writel(val, pmatop + PMA_COMM_CFG_0_1 + offset);
	}

	tc956x_reset_deassert(chip, id, MAC_RESET_PMA);

	ret = readl_poll_timeout(pmatop + PMA_PCS_GL_PC_ST0, val,
				 val & PMA_PCS_GL_PC_ST0_PLL_VALID,
				 10, 100000);
	if (ret) {
		dev_err(td->dev,
			"Ethernet PMA PLL did not become valid (PCS_GL_PC_ST0=%08x)\n",
			val);
		return ret;
	}

	return 0;
}

static int tc956x_mac_speed_select(struct tc956x_data *td,
				   phy_interface_t phy_interface, int speed)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mac_speed); i++) {
		if (mac_speed[i].speed != speed)
			continue;

		if (mac_speed[i].phy_interface == phy_interface)
			return mac_speed[i].sp_sel;
	}
	dev_err(td->dev, "%s/%d unsupported\n",
		phy_modes(phy_interface), speed);

	return -EOPNOTSUPP;
}

static int tc956x_mac_configure(struct tc956x_data *td,
				phy_interface_t phy_interface, int speed)
{
	void __iomem *emac_ctl = td->auxbus_data->emac_ctl;
	void __iomem *misc_ctl = td->auxbus_data->sfr + MISC_CTL_OFFSET;
	u32 id = td->auxbus_data->mac_id;
	u32 rate = 0;
	int sp_sel;
	u32 val;

	sp_sel = tc956x_mac_speed_select(td, phy_interface, speed);
	if (sp_sel < 0)
		return sp_sel;

	val = readl(emac_ctl);
	val |= EMAC_LPIHWCLKEN;
	val &= ~EMAC_INV_SGM_SIG_DET;
	val = u32_replace_bits(val, PCS_CLK_PHY, EMAC_PHY_INF_SEL_MASK);
	val = u32_replace_bits(val, sp_sel, EMAC_SP_SEL_MASK);
	writel(val, emac_ctl);

	if (phy_interface == PHY_INTERFACE_MODE_USXGMII) {
		switch (speed) {
		case SPEED_1000:
			rate = MISC_SP_ETH_1G;
			break;
		case SPEED_100:
			rate = MISC_SP_ETH_100M;
			break;
		case SPEED_10:
			rate = MISC_SP_ETH_10M;
			break;
		}
	}

	val = readl(misc_ctl);
	if (id)
		val = u32_replace_bits(val, rate, MISC_SP_ETH1_MASK);
	else
		val = u32_replace_bits(val, rate, MISC_SP_ETH0_MASK);
	writel(val, misc_ctl);

	return 0;
}

static int tc956x_mac_enable(struct tc956x_data *td)
{
	const struct tc956x_chip *chip = td->auxbus_data->chip;
	u32 id = td->auxbus_data->mac_id;
	int ret;

	tc956x_clock_enable(chip, id, MAC_CLOCK_TX);
	tc956x_clock_enable(chip, id, MAC_CLOCK_RX);
	tc956x_clock_enable(chip, id, MAC_CLOCK_ALL);
	if (id)
		tc956x_clock_enable(chip, id, MAC_CLOCK_RMII);

	tc956x_common_clock_enable(chip, COMMON_CLOCK_PLL);
	tc956x_common_clock_enable(chip, COMMON_CLOCK_SGMII);
	tc956x_common_clock_enable(chip, COMMON_CLOCK_REFCLK);
	tc956x_clock_enable(chip, id, MAC_CLOCK_125M);
	tc956x_clock_enable(chip, id, MAC_CLOCK_312_5M);

	ret = tc956x_mac_configure(td, td->interface, td->boot_speed);
	if (ret)
		return dev_err_probe(td->dev, ret,
				     "failed to configure MAC speed\n");

	tc956x_reset_deassert(chip, id, MAC_RESET_MAC);
	ret = tc956x_pma_init(td);
	if (ret)
		return ret;
	tc956x_reset_deassert(chip, id, MAC_RESET_XPCS);

	return 0;
}

static void tc956x_mac_disable(struct tc956x_data *td)
{
	const struct tc956x_chip *chip = td->auxbus_data->chip;
	u32 id = td->auxbus_data->mac_id;
	tc956x_reset_assert(chip, id, MAC_RESET_MAC);
	tc956x_reset_assert(chip, id, MAC_RESET_PMA);
	tc956x_reset_assert(chip, id, MAC_RESET_XPCS);

	tc956x_clock_disable(chip, id, MAC_CLOCK_ALL);
	tc956x_clock_disable(chip, id, MAC_CLOCK_RX);
	tc956x_clock_disable(chip, id, MAC_CLOCK_TX);
	if (id)
		tc956x_clock_disable(chip, id, MAC_CLOCK_RMII);

	tc956x_clock_disable(chip, id, MAC_CLOCK_125M);
	tc956x_clock_disable(chip, id, MAC_CLOCK_312_5M);
}

static void tc956x_mac_init_state(struct tc956x_data *td)
{
	const struct tc956x_chip *chip = td->auxbus_data->chip;
	u32 id = td->auxbus_data->mac_id;

	tc956x_clock_disable(chip, id, MAC_CLOCK_125M);
	tc956x_clock_disable(chip, id, MAC_CLOCK_312_5M);

	tc956x_mac_disable(td);
}

/*
 * Override method for dwxgmac301_dma_ops->init_rx_chan
 *
 * This differs from the dwxgmac301_dma_ops->init_rx_chan by translating the DMA
 * address for TC956x internal bus. The window that provides DMA access to PCI
 * is linearly mapped at 0x10_0000_0000.
 */
static void tc956x_dma_init_rx_chan(struct stmmac_priv *priv,
				    void __iomem *ioaddr,
				    struct stmmac_dma_cfg *dma_cfg,
				    dma_addr_t phy, u32 chan)
{
	dma_addr_t translated = phy + TC956X_SLV00_SRC_ADDR;

	dwxgmac2_dma_init_rx_chan(priv, ioaddr, dma_cfg, phy, chan);

	writel(upper_32_bits(translated),
	       ioaddr + XGMAC_DMA_CH_RxDESC_HADDR(chan));
	writel(lower_32_bits(translated),
	       ioaddr + XGMAC_DMA_CH_RxDESC_LADDR(chan));
}

/* Override method for dwxgmac301_dma_ops->init_tx_chan */
static void tc956x_dma_init_tx_chan(struct stmmac_priv *priv,
				    void __iomem *ioaddr,
				    struct stmmac_dma_cfg *dma_cfg,
				    dma_addr_t phy, u32 chan)
{
	dma_addr_t translated = phy + TC956X_SLV00_SRC_ADDR;

	dwxgmac2_dma_init_tx_chan(priv, ioaddr, dma_cfg, phy, chan);

	writel(upper_32_bits(translated),
	       ioaddr + XGMAC_DMA_CH_TxDESC_HADDR(chan));
	writel(lower_32_bits(translated),
	       ioaddr + XGMAC_DMA_CH_TxDESC_LADDR(chan));
}

/* Override method for dwxgmac210_desc_ops->set_addr */
static void tc956x_desc_set_addr(struct dma_desc *p, dma_addr_t addr)
{
	dma_addr_t translated = addr + TC956X_SLV00_SRC_ADDR;

	p->des0 = cpu_to_le32(lower_32_bits(translated));
	p->des1 = cpu_to_le32(upper_32_bits(translated));
}

/* Override method for dwxgmac210_desc_ops->set_sec_addr */
static void tc956x_desc_set_sec_addr(struct dma_desc *p, dma_addr_t addr,
				     bool is_valid)
{
	dma_addr_t translated = addr + TC956X_SLV00_SRC_ADDR;

	p->des2 = cpu_to_le32(lower_32_bits(translated));
	p->des3 = cpu_to_le32(upper_32_bits(translated));
}

/*
 * Use mac_setup to apply the override methods above.
 *
 * The memory for the modified ops structures is pre-allocated as part of
 * struct tc956x_data.
 */
static int tc956x_mac_setup(void *apriv, struct mac_device_info *mac)
{
	struct stmmac_priv *priv = apriv;
	struct stmmac_desc_ops *desc;
	struct stmmac_dma_ops *dma;
	struct tc956x_data *td;

	td = priv->plat->bsp_priv;

	/* 10G USXGMII traffic needs the maximum descriptor ring to avoid
	 * starving the DMA path. Keep this TC956X-specific instead of raising
	 * stmmac's global defaults for every platform.
	 */
	priv->dma_conf.dma_rx_size = DMA_MAX_RX_SIZE;
	priv->dma_conf.dma_tx_size = DMA_MAX_TX_SIZE;

	/* dwxgmac301_dma_ops needs extending to provide DMA address translation */
	dma = &td->dma;
	*dma = dwxgmac301_dma_ops;
	dma->init_rx_chan = tc956x_dma_init_rx_chan;
	dma->init_tx_chan = tc956x_dma_init_tx_chan;
	mac->dma = dma;

	/* dwxgmac210_desc_ops also needs extending for the same reason */
	desc = &td->desc;
	*desc = dwxgmac210_desc_ops;
	desc->set_addr = tc956x_desc_set_addr;
	desc->set_sec_addr = tc956x_desc_set_sec_addr;
	mac->desc = desc;

	priv->hw = mac;

	return dwxgmac2_setup(priv);
}

static int tc956x_pcs_init(struct stmmac_priv *priv)
{
	struct xpcs_regmap_config xpcs_regmap_cfg;
	struct tc956x_data *td = priv->plat->bsp_priv;
	void __iomem *emac = priv->ioaddr;
	struct regmap *xpcs_regmap;
	void __iomem *xpcs_addr;
	struct dw_xpcs *xpcs;

	xpcs_addr = emac + DWMAC_XPCS_OFFSET;
	xpcs_regmap = devm_regmap_init_mmio(priv->device, xpcs_addr,
					    &xpcs_regmap_config);
	if (IS_ERR(xpcs_regmap))
		return PTR_ERR(xpcs_regmap);

	xpcs_regmap_cfg.regmap = xpcs_regmap;
	xpcs_regmap_cfg.reg_indir = true;

	xpcs = devm_xpcs_regmap_register(priv->device, &xpcs_regmap_cfg);
	if (IS_ERR(xpcs))
		return PTR_ERR(xpcs);

	xpcs_config_qps615(xpcs, td->mode_switch,
			   td->plat->phy_interface == PHY_INTERFACE_MODE_SGMII);
	xpcs_config_eee_mult_fact(xpcs, priv->plat->mult_fact_100ns);
	priv->hw->phylink_pcs = xpcs_to_phylink_pcs(xpcs);

	return 0;
}

static struct phylink_pcs *tc956x_select_pcs(struct stmmac_priv *priv,
					     phy_interface_t interface)
{
	return priv->hw->phylink_pcs;
}

static int tc956x_mac_finish(struct net_device *ndev, void *bsp_priv,
			     unsigned int mode, phy_interface_t interface)
{
	struct tc956x_data *td = bsp_priv;

	td->interface = interface;

	return 0;
}

static int tc956x_mac_prepare(struct net_device *ndev, void *bsp_priv,
			      unsigned int mode,
			      phy_interface_t interface)
{
	struct tc956x_data *td = bsp_priv;
	const struct tc956x_chip *chip = td->auxbus_data->chip;
	u32 id = td->auxbus_data->mac_id;
	void __iomem *rst = td->auxbus_data->sfr + NRSTCTRL_OFFSET(id);
	u32 rst_before, rst_after;
	int speed, ret;

	if (td->interface == interface)
		return 0;
	if (!td->mode_switch &&
	    td->plat->phy_interface != PHY_INTERFACE_MODE_SGMII)
		return -EOPNOTSUPP;

	speed = ndev->phydev ? ndev->phydev->speed : SPEED_UNKNOWN;
	if (tc956x_mac_speed_select(td, interface, speed) < 0) {
		switch (interface) {
		case PHY_INTERFACE_MODE_USXGMII:
			speed = SPEED_10000;
			break;
		case PHY_INTERFACE_MODE_2500BASEX:
			speed = SPEED_2500;
			break;
		case PHY_INTERFACE_MODE_SGMII:
		case PHY_INTERFACE_MODE_1000BASEX:
			speed = SPEED_1000;
			break;
		default:
			return -EOPNOTSUPP;
		}
	}

	rst_before = readl(rst);

	tc956x_reset_assert(chip, id, MAC_RESET_XPCS);

	ret = tc956x_mac_configure(td, interface, speed);
	if (ret)
		return ret;

	ret = tc956x_pma_init(td);
	if (ret)
		return ret;
	td->interface = interface;
	tc956x_reset_deassert(chip, id, MAC_RESET_XPCS);

	rst_after = readl(rst);
	if ((rst_before | rst_after) & RST_MAC_MACRST) {
		netdev_err(ndev,
			   "eMAC core reset unexpectedly asserted during PMA/XPCS-only switch: NRSTCTRL %08x -> %08x\n",
			   rst_before, rst_after);
		return -EIO;
	}
	return 0;
}

static void tc956x_fix_mac_speed(void *bsp_priv, int speed, unsigned int mode)
{
	struct tc956x_data *td = bsp_priv;
	phy_interface_t interface = td->interface;
	int ret;

	ret = tc956x_mac_configure(td, interface, speed);
	if (ret) {
		dev_err(td->dev, "failed to configure %s at %d Mbps: %d\n",
			phy_modes(interface), speed, ret);
		return;
	}
}

static void tc956x_post_mac_link_up(struct net_device *ndev, void *bsp_priv,
				    struct phy_device *phydev,
				    unsigned int mode,
				    phy_interface_t interface, int speed)
{
	struct tc956x_data *td = bsp_priv;
	void __iomem *mac = td->auxbus_data->emac;
	u32 tx, rx;
	int ret;

	if (td->mode_switch && interface == PHY_INTERFACE_MODE_USXGMII) {
		tx = readl(mac + MAC_TX_CONFIG_OFFSET);
		rx = readl(mac + MAC_RX_CONFIG_OFFSET);
		if (!(tx & MAC_TX_CONFIG_TE) || !(rx & MAC_RX_CONFIG_RE))
			return;

		if (!phydev)
			return;

		ret = phy_config_inband(phydev, LINK_INBAND_BYPASS);
		if (ret)
			netdev_err(ndev,
				   "post-MAC-enable USXGMII PHY synchronization failed: %d\n",
				   ret);
	}
}

static int tc956x_dwmac_suspend(struct device *dev, void *bsp_priv)
{
	struct tc956x_data *td = bsp_priv;

	tc956x_mac_disable(td);

	return 0;
}

static int tc956x_dwmac_resume(struct device *dev, void *bsp_priv)
{
	struct tc956x_data *td = bsp_priv;

	return tc956x_mac_enable(td);
}

/* Called by tc956x_dwmac_probe(); return errors with dev_err_probe() */
static int tc956x_dwmac_parse_dt(struct tc956x_data *td)
{
	struct device_node *mdio_node;
	struct device *dev = td->dev;
	struct device_node *np;

	np = dev_of_node(dev);
	if (!np)
		return dev_err_probe(dev, -EINVAL, "no devicetree node\n");

	/* Find the MDIO bus */
	for_each_child_of_node(np, mdio_node) {
		if (of_device_is_compatible(mdio_node,
					    "snps,dwmac-mdio"))
			break;
	}

	/* Pass the MDIO bus (if there is one) to the core driver */
	if (mdio_node) {
		td->plat->mdio_node = mdio_node;
		td->plat->mdio_bus_data->needs_reset = true;
	}

	return 0;
}

static void tc956x_deassert_phy_resets(struct tc956x_data *td)
{
	struct device_node *child;
	struct gpio_desc *reset;
	u32 delay_us;

	if (!td->plat->mdio_node)
		return;

	for_each_available_child_of_node(td->plat->mdio_node, child) {
		reset = fwnode_gpiod_get_index(of_fwnode_handle(child),
					       "reset", 0, GPIOD_OUT_LOW,
					       "phy-reset");
		if (IS_ERR(reset))
			continue;

		gpiod_set_value_cansleep(reset, 0);
		gpiod_put(reset);

		if (!of_property_read_u32(child, "reset-deassert-us", &delay_us))
			fsleep(delay_us);
	}
}

static int tc956x_lookup_max_speed(phy_interface_t phy_interface)
{
	switch (phy_interface) {
	case PHY_INTERFACE_MODE_USXGMII:
		return SPEED_10000;

	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_2500BASEX:
		return SPEED_2500;

	case PHY_INTERFACE_MODE_1000BASEX:
		return SPEED_1000;

	default:
		return -EOPNOTSUPP;
	}
}

static const phy_interface_t tc956x_serdes_interfaces[] = {
	PHY_INTERFACE_MODE_USXGMII,
	PHY_INTERFACE_MODE_2500BASEX,
	PHY_INTERFACE_MODE_SGMII,
	PHY_INTERFACE_MODE_1000BASEX,
};

static bool tc956x_is_serdes_interface(phy_interface_t phy_interface)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(tc956x_serdes_interfaces); i++)
		if (tc956x_serdes_interfaces[i] == phy_interface)
			return true;

	return false;
}

static int tc956x_max_speed(struct tc956x_data *td,
			    phy_interface_t phy_interface)
{
	int max, ret, i;

	max = tc956x_lookup_max_speed(phy_interface);
	if (max < 0 || !td->mode_switch ||
	    !tc956x_is_serdes_interface(phy_interface))
		return max;

	for (i = 0; i < ARRAY_SIZE(tc956x_serdes_interfaces); i++) {
		ret = tc956x_lookup_max_speed(tc956x_serdes_interfaces[i]);
		if (ret > max)
			max = ret;
	}

	return max;
}

/* Called by tc956x_dwmac_probe(); return errors with dev_err_probe() */
static int tc956x_plat_dat_init(struct tc956x_data *td)
{
	struct plat_stmmacenet_data *plat;
	phy_interface_t phy_interface;
	struct device *dev = td->dev;
	struct stmmac_axi *axi;
	u32 speed;
	int ret;
	u32 i;

	phy_interface = device_get_phy_mode(dev);
	if (phy_interface < 0)
		return -ENODEV;

	td->mode_switch = device_property_read_bool(dev,
						    "sgmii-usxgmii-switch-quirk");

	/* The platform structure is allocated with devm_kzalloc() */
	plat = stmmac_plat_dat_alloc(dev);
	if (!plat)
		return -ENOMEM;

	ret = tc956x_lookup_max_speed(phy_interface);
	if (ret < 0)
		return dev_err_probe(dev, ret, "unsupported phy speed\n");
	speed = ret;

	td->interface = phy_interface;
	td->boot_speed = speed;

	plat->core_type = DWMAC_CORE_XGMAC;
	plat->bus_id = to_auxiliary_dev(dev)->id;
	plat->phy_interface = phy_interface;
	plat->mdio_bus_data = &td->mdio_bus_data;
	/* Parent PCI device is used for DMA */
	plat->dma_device = dev->parent;
	plat->dma_cfg = &td->dma_cfg;
	plat->dma_cfg->pbl = 32;
	plat->dma_cfg->pblx8 = true;

	/*
	 * Our MAC clock rate is fixed at 125 MHz.  For XGMAC, clk_csr 0
	 * represents "divide by 62" and gets the best rate under 2.5 MHz.
	 */
	plat->clk_csr = 0;	/* MDC clock = clk_csr_i / 62 */
	plat->mdio_bus_data->default_an_inband =
		!td->mode_switch && phy_interface != PHY_INTERFACE_MODE_USXGMII;
	plat->force_sf_dma_mode = true;
	plat->max_speed = tc956x_max_speed(td, phy_interface);
	plat->unicast_filter_entries = 32;

	plat->rx_queues_to_use = TC956X_RX_QUEUES;
	plat->tx_queues_to_use = TC956X_TX_QUEUES;
	plat->rss_en = TC956X_RX_QUEUES > 1;

	/*
	 * Oversized FIFOs result in reduced performance in bandwidth tests.
	 * Limit them to 8KiB per queue, or the total available.
	 */
	plat->tx_fifo_size = TC956X_TX_FIFO_KB * SZ_1K;
	plat->rx_fifo_size = TC956X_RX_FIFO_KB * SZ_1K;
	plat->host_dma_width = 36;

	plat->rx_sched_algorithm = MTL_RX_ALGORITHM_SP;
	plat->tx_sched_algorithm = MTL_TX_ALGORITHM_WRR;

	/* Default RX chan is set to queue index (0..rx_queues_to_use-1) */
	for (i = 0; i < plat->rx_queues_to_use; i++) {
		plat->rx_queues_cfg[i].mode_to_use = MTL_QUEUE_DCB;
		plat->rx_queues_cfg[i].chan = i;
	}

	for (i = 0; i < plat->tx_queues_to_use; i++) {
		plat->tx_queues_cfg[i].weight = 12;
		plat->tx_queues_cfg[i].mode_to_use = MTL_QUEUE_DCB;

		/* Only queues 5-8 support time-based scheduling on TC956X */
		if (i >= 5)
			plat->tx_queues_cfg[i].tbs_en = 1;
	}

	plat->fix_mac_speed = tc956x_fix_mac_speed;
	plat->post_mac_link_up = tc956x_post_mac_link_up;
	plat->mac_prepare = tc956x_mac_prepare;
	plat->mac_finish = tc956x_mac_finish;
	plat->suspend = tc956x_dwmac_suspend;
	plat->resume = tc956x_dwmac_resume;
	plat->mac_setup = tc956x_mac_setup;
	plat->pcs_init = tc956x_pcs_init;
	plat->select_pcs = tc956x_select_pcs;

	plat->bsp_priv = td;
	plat->clk_ptp_rate = TC956X_PTP_CLOCK_RATE;
	plat->clk_ref_rate = TC956X_MAC_CLOCK_RATE;

	/* AXI Configuration */
	axi = &td->axi;
	axi->axi_lpi_en = 0;
	axi->axi_wr_osr_lmt = 31;
	axi->axi_rd_osr_lmt = 31;
	/* All sizes (2^2..2^8) are supported */
	axi->axi_blen_regval = DMA_AXI_BLEN_MASK;
	plat->axi = axi;

	plat->mac_port_sel_speed = speed;
	plat->flags = STMMAC_FLAG_MULTI_MSI_EN;

	td->plat = plat;

	return 0;
}

/*
 * The domain was created with IRQ_DOMAIN_FLAG_DESTROY_GC, so any mapped IRQs
 * will be disposed when the domain is removed (when the device is destroyed).
 */
static int tc956x_stmmac_resources_init(struct tc956x_data *td,
					struct stmmac_resources *res)
{
	struct irq_domain *irq_domain = td->irq_domain;
	u32 i;

	res->irq = irq_create_mapping(irq_domain, HWIRQ_EVENT);
	if (!res->irq)
		return -EINVAL;

	for (i = 0; i < td->plat->tx_queues_to_use; i++) {
		res->tx_irq[i] = irq_create_mapping(irq_domain, HWIRQ_TX0 + i);
		if (!res->tx_irq[i])
			return -EINVAL;
	}

	for (i = 0; i < td->plat->rx_queues_to_use; i++) {
		res->rx_irq[i] = irq_create_mapping(irq_domain, HWIRQ_RX0 + i);
		if (!res->tx_irq[i])
			return -EINVAL;
	}

	res->addr = td->auxbus_data->emac;

	return 0;
}

static int tc956x_dwmac_probe(struct auxiliary_device *adev,
			      const struct auxiliary_device_id *id)
{
	struct stmmac_resources res = { };
	struct device *dev = &adev->dev;
	struct tc956x_data *td;
	int ret;

	td = devm_kzalloc(dev, sizeof(*td), GFP_KERNEL);
	if (!td)
		return -ENOMEM;

	td->dev = dev;
	td->auxbus_data = dev_get_platdata(dev);
	if (!td->auxbus_data)
		return dev_err_probe(dev, -EINVAL, "no platform data\n");

	ret = tc956x_plat_dat_init(td);
	if (ret)
		return ret;

	ret = tc956x_dwmac_parse_dt(td);
	if (ret)
		return ret;

	td->irq_domain = tc956x_msigen_irq_domain_instantiate(td);
	if (IS_ERR(td->irq_domain))
		return dev_err_probe(dev, PTR_ERR(td->irq_domain),
				     "failed to instantiate IRQ domain\n");

	/* Put the MAC in a known initial state */
	tc956x_mac_init_state(td);
	ret = tc956x_mac_enable(td);
	if (ret)
		return dev_err_probe(dev, ret, "failed initial MAC enable\n");
	tc956x_deassert_phy_resets(td);

	ret = tc956x_stmmac_resources_init(td, &res);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to initialize stmmac resources\n");

	ret = device_get_mac_address(dev, res.mac);
	if (ret == -EPROBE_DEFER)
		return dev_err_probe(dev, ret, "failed to get MAC address\n");

	ret = stmmac_dvr_probe(dev, td->plat, &res);
	if (ret)
		return dev_err_probe(dev, ret, "failed stmmac probe\n");

	return 0;
}

static void tc956x_dwmac_remove(struct auxiliary_device *adev)
{
	struct device *dev = &adev->dev;
	struct net_device *ndev = dev_get_drvdata(dev);
	struct stmmac_priv *priv = netdev_priv(ndev);
	struct tc956x_data *td = priv->plat->bsp_priv;

	stmmac_dvr_remove(dev);
	tc956x_mac_disable(td);
}

static void tc956x_dwmac_shutdown(struct auxiliary_device *adev)
{
	struct device *dev = &adev->dev;
	int ret;

	ret = stmmac_suspend(dev);
	if (ret)
		dev_warn(dev, "failed to suspend MAC during shutdown: %d\n",
			 ret);
}

static const struct auxiliary_device_id tc956x_dwmac_ids[] = {
	{ .name = TC956X_PCIE_DRIVER_NAME "." TC956X_XGMAC_DEV_NAME, },
	{ },
};
MODULE_DEVICE_TABLE(auxiliary, tc956x_dwmac_ids);

static struct auxiliary_driver tc956x_dwmac_driver = {
	.name		= DRIVER_NAME,
	.probe		= tc956x_dwmac_probe,
	.remove		= tc956x_dwmac_remove,
	.shutdown	= tc956x_dwmac_shutdown,
	.id_table	= tc956x_dwmac_ids,
	.driver = {
		.name	= DRIVER_NAME,
		.pm	= &stmmac_simple_pm_ops,
		.owner	= THIS_MODULE,
	},
};
module_auxiliary_driver(tc956x_dwmac_driver);

MODULE_DESCRIPTION("Toshiba TC956x PCIe Ethernet Network Driver");
MODULE_LICENSE("GPL");
