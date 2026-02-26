/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __LINUX_PCS_XPCS_REGMAP_H
#define __LINUX_PCS_XPCS_REGMAP_H

#include <linux/types.h>

struct device;
struct regmap;
struct dw_xpcs;

struct xpcs_regmap_config {
	struct regmap *regmap;
	bool reg_indir;
};

struct dw_xpcs *devm_xpcs_regmap_register(
		struct device *dev, const struct xpcs_regmap_config *config);

#endif /* __LINUX_PCS_XPCS_REGMAP_H */
