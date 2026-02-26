// SPDX-License-Identifier: GPL-2.0
/*
 * Synopsys DesignWare XPCS regmap helpers
 *
 * Copyright (C) 2026 RISCstar Solutions.
 * Copyright (C) 2024 Serge Semin
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/mdio.h>
#include <linux/pcs/pcs-xpcs.h>
#include <linux/pcs/pcs-xpcs-regmap.h>
#include <linux/regmap.h>

#include "pcs-xpcs.h"

/* Page select register for the indirect MMIO CSRs access */
#define DW_VR_CSR_VIEWPORT		0xff

struct dw_xpcs_regmap {
	struct device *dev;
	struct mii_bus *bus;
	struct regmap *regmap;
	bool reg_indir;
};

static ptrdiff_t xpcs_regmap_addr_format(int dev, int reg)
{
	return FIELD_PREP(0x1f0000, dev) | FIELD_PREP(0xffff, reg);
}

static u16 xpcs_regmap_addr_page(ptrdiff_t csr)
{
	return FIELD_GET(0x1fff00, csr);
}

static ptrdiff_t xpcs_regmap_addr_offset(ptrdiff_t csr)
{
	return FIELD_GET(0xff, csr);
}

static int xpcs_regmap_read_reg_indirect(struct dw_xpcs_regmap *pxpcs, int dev,
					 int reg)
{
	ptrdiff_t csr, ofs;
	unsigned int val;
	u16 page;
	int res;

	csr = xpcs_regmap_addr_format(dev, reg);
	page = xpcs_regmap_addr_page(csr);
	ofs = xpcs_regmap_addr_offset(csr);

	res = regmap_write(pxpcs->regmap, DW_VR_CSR_VIEWPORT, page);
	if (res < 0)
		return res;

	res = regmap_read(pxpcs->regmap, ofs, &val);
	if (res < 0)
		return res;

	return val & 0xffff;
}

static int xpcs_regmap_write_reg_indirect(struct dw_xpcs_regmap *pxpcs, int dev,
					  int reg, u16 val)
{
	ptrdiff_t csr, ofs;
	u16 page;
	int res;

	csr = xpcs_regmap_addr_format(dev, reg);
	page = xpcs_regmap_addr_page(csr);
	ofs = xpcs_regmap_addr_offset(csr);

	res = regmap_write(pxpcs->regmap, DW_VR_CSR_VIEWPORT, page);
	if (res < 0)
		return res;

	return regmap_write(pxpcs->regmap, ofs, val);
}

static int xpcs_regmap_read_reg_direct(struct dw_xpcs_regmap *pxpcs, int dev,
				       int reg)
{
	unsigned int val;
	ptrdiff_t csr;
	int res;

	csr = xpcs_regmap_addr_format(dev, reg);
	res = regmap_read(pxpcs->regmap, csr, &val);
	if (res < 0)
		return res;

	return val & 0xffff;
}

static int xpcs_regmap_write_reg_direct(struct dw_xpcs_regmap *pxpcs, int dev,
					int reg, u16 val)
{
	ptrdiff_t csr = xpcs_regmap_addr_format(dev, reg);

	return regmap_write(pxpcs->regmap, csr, val);
}

static int xpcs_regmap_read_c22(struct mii_bus *bus, int addr, int reg)
{
	struct dw_xpcs_regmap *pxpcs = bus->priv;

	if (addr != 0)
		return -ENODEV;

	if (pxpcs->reg_indir)
		return xpcs_regmap_read_reg_indirect(pxpcs, MDIO_MMD_VEND2, reg);
	else
		return xpcs_regmap_read_reg_direct(pxpcs, MDIO_MMD_VEND2, reg);
}

static int xpcs_regmap_write_c22(struct mii_bus *bus, int addr, int reg, u16 val)
{
	struct dw_xpcs_regmap *pxpcs = bus->priv;

	if (addr != 0)
		return -ENODEV;

	if (pxpcs->reg_indir)
		return xpcs_regmap_write_reg_indirect(pxpcs, MDIO_MMD_VEND2, reg, val);
	else
		return xpcs_regmap_write_reg_direct(pxpcs, MDIO_MMD_VEND2, reg, val);
}

static int xpcs_regmap_read_c45(struct mii_bus *bus, int addr, int dev, int reg)
{
	struct dw_xpcs_regmap *pxpcs = bus->priv;

	if (addr != 0)
		return -ENODEV;

	if (pxpcs->reg_indir)
		return xpcs_regmap_read_reg_indirect(pxpcs, dev, reg);
	else
		return xpcs_regmap_read_reg_direct(pxpcs, dev, reg);
}

static int xpcs_regmap_write_c45(struct mii_bus *bus, int addr, int dev,
				 int reg, u16 val)
{
	struct dw_xpcs_regmap *pxpcs = bus->priv;

	if (addr != 0)
		return -ENODEV;

	if (pxpcs->reg_indir)
		return xpcs_regmap_write_reg_indirect(pxpcs, dev, reg, val);
	else
		return xpcs_regmap_write_reg_direct(pxpcs, dev, reg, val);
}

struct dw_xpcs *devm_xpcs_regmap_register(struct device *dev,
					  const struct xpcs_regmap_config *config)
{
	static atomic_t id = ATOMIC_INIT(-1);
	struct dw_xpcs_regmap *pxpcs;
	int ret;

	pxpcs = devm_kzalloc(dev, sizeof(*pxpcs), GFP_KERNEL);
	if (!pxpcs)
		return ERR_PTR(-ENOMEM);

	pxpcs->dev = dev;
	pxpcs->regmap = config->regmap;
	pxpcs->reg_indir = config->reg_indir;

	pxpcs->bus = devm_mdiobus_alloc_size(dev, 0);
	if (!pxpcs->bus)
		return ERR_PTR(-ENOMEM);

	pxpcs->bus->name = "DW XPCS MCI/APB3";
	pxpcs->bus->read = xpcs_regmap_read_c22;
	pxpcs->bus->write = xpcs_regmap_write_c22;
	pxpcs->bus->read_c45 = xpcs_regmap_read_c45;
	pxpcs->bus->write_c45 = xpcs_regmap_write_c45;
	pxpcs->bus->phy_mask = ~0;
	pxpcs->bus->parent = dev;
	pxpcs->bus->priv = pxpcs;

	snprintf(pxpcs->bus->id, MII_BUS_ID_SIZE,
		 "dwxpcs-%x", atomic_inc_return(&id));

	/* MDIO-bus here serves as just a back-end engine abstracting out
	 * the MDIO and MCI/APB3 IO interfaces utilized for the DW XPCS CSRs
	 * access.
	 */
	ret = devm_mdiobus_register(dev, pxpcs->bus);
	if (ret) {
		dev_err(dev, "Failed to create MDIO bus\n");
		return ERR_PTR(ret);
	}

	return xpcs_create_mdiodev(pxpcs->bus, 0);
}
EXPORT_SYMBOL_GPL(devm_xpcs_regmap_register);
