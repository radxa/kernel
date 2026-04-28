/* SPDX-License-Identifier: GPL-2.0 */

/*
 * Copyright (C) 2026 by RISCstar Solutions Corporation.  All rights reserved.
 */

#ifndef __TOSHIBA_TC956X_DWMAC_H__
#define __TOSHIBA_TC956X_DWMAC_H__

#include <linux/compiler_types.h>
#include <linux/types.h>

#define TC956X_PCIE_DRIVER_NAME	"tc956x_pci"

#define TC956X_XGMAC_DEV_NAME	"dwmac-tc956x"

/* Starting address of the space translated by the PCIe endpoint bridge */
#define TC956X_SLV00_SRC_ADDR	0x0000001000000000ULL

enum tc956x_reset_id {
	MAC_RESET_MAC		= 7,
	MAC_RESET_PMA		= 30,
	MAC_RESET_XPCS		= 31,
};

enum tc956x_clock_id {
	MAC_CLOCK_TX		= 7,
	MAC_CLOCK_RX		= 14,
	MAC_CLOCK_ALL		= 31,
	MAC_CLOCK_125M		= 29,
	MAC_CLOCK_312_5M	= 30,
	MAC_CLOCK_RMII		= 15,	/* eMAC 1 only */
};

/**
 * struct tc956x_dwmac_data - Structure passed to stmmac auxiliary devices.
 * @chip:		Context pointer needed for reset and clock operations
 * @emac:		I/O mapped address used by eMAC
 * @emac_ctl:		I/O mapped address used for eMAC control
 * @msigen:		I/O mapped address used by MSIGEN
 * @msigen_irq:		IRQ number used by MSIGEN
 * @rev_id:		Chip revision ID (for quirks)
 * @mac_id:		Unique device ID (0 or 1)
 *
 * This structure is passed via platform data to the stmmac auxiliary devices.
 */
struct tc956x_dwmac_data {
	const struct tc956x_chip *chip;
	void __iomem *emac;
	void __iomem *emac_ctl;
	void __iomem *msigen;
	unsigned int msigen_irq;
	u8 rev_id;
	u8 mac_id;
};

extern void tc956x_reset_clock_set(const struct tc956x_chip *chip, bool reset,
				   bool reg0, bool set, u8 bit);

static inline void tc956x_reset_assert(const struct tc956x_chip *chip,
				       u8 mac_id, enum tc956x_reset_id id)
{
	tc956x_reset_clock_set(chip, true, !mac_id, true, (u8)id);
}

static inline void tc956x_reset_deassert(const struct tc956x_chip *chip,
					 u8 mac_id, enum tc956x_reset_id id)
{
	tc956x_reset_clock_set(chip, true, !mac_id, false, (u8)id);
}

static inline void tc956x_clock_enable(const struct tc956x_chip *chip,
				       u8 mac_id, enum tc956x_clock_id id)
{
	tc956x_reset_clock_set(chip, false, !mac_id, true, (u8)id);
}

static inline void tc956x_clock_disable(const struct tc956x_chip *chip,
					u8 mac_id, enum tc956x_clock_id id)
{
	tc956x_reset_clock_set(chip, false, !mac_id, false, (u8)id);
}

#endif /* __TOSHIBA_TC956X_DWMAC_H__*/
