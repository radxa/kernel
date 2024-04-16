// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for Analog Devices (Linear Technology) LTC3350
 * High Current Supercapacitor Backup Controller and System Monitor
 * Copyright (C) 2024, Topic Embedded Products
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/mod_devicetable.h>
#include <linux/math64.h>
#include <linux/power_supply.h>
#include <linux/property.h>
#include <linux/i2c.h>
#include <linux/regmap.h>

/* Registers (names based on what datasheet uses) */
#define LTC3350_REG_CLR_ALARMS		0x00
#define LTC3350_REG_MSK_ALARMS		0x01
#define LTC3350_REG_MSK_MON_STATUS	0x02
#define LTC3350_REG_CAP_ESR_PER		0x04
#define LTC3350_REG_VCAPFB_DAC		0x05
#define LTC3350_REG_VSHUNT		0x06
#define LTC3350_REG_CAP_UV_LVL		0x07
#define LTC3350_REG_CAP_OV_LVL		0x08
#define LTC3350_REG_GPI_UV_LVL		0x09
#define LTC3350_REG_GPI_OV_LVL		0x0A
#define LTC3350_REG_VIN_UV_LVL		0x0B
#define LTC3350_REG_VIN_OV_LVL		0x0C
#define LTC3350_REG_VCAP_UV_LVL		0x0D
#define LTC3350_REG_VCAP_OV_LVL		0x0E
#define LTC3350_REG_VOUT_UV_LVL		0x0F
#define LTC3350_REG_VOUT_OV_LVL		0x10
#define LTC3350_REG_IIN_OC_LVL		0x11
#define LTC3350_REG_ICHG_UC_LVL		0x12
#define LTC3350_REG_DTEMP_COLD_LVL	0x13
#define LTC3350_REG_DTEMP_HOT_LVL	0x14
#define LTC3350_REG_ESR_HI_LVL		0x15
#define LTC3350_REG_CAP_LO_LVL		0x16
#define LTC3350_REG_CTL_REG		0x17
#define LTC3350_REG_NUM_CAPS		0x1A
#define LTC3350_REG_CHRG_STATUS		0x1B
#define LTC3350_REG_MON_STATUS		0x1C
#define LTC3350_REG_ALARM_REG		0x1D
#define LTC3350_REG_MEAS_CAP		0x1E
#define LTC3350_REG_MEAS_ESR		0x1F
#define LTC3350_REG_MEAS_VCAP1		0x20
#define LTC3350_REG_MEAS_VCAP2		0x21
#define LTC3350_REG_MEAS_VCAP3		0x22
#define LTC3350_REG_MEAS_VCAP4		0x23
#define LTC3350_REG_MEAS_GPI		0x24
#define LTC3350_REG_MEAS_VIN		0x25
#define LTC3350_REG_MEAS_VCAP		0x26
#define LTC3350_REG_MEAS_VOUT		0x27
#define LTC3350_REG_MEAS_IIN		0x28
#define LTC3350_REG_MEAS_ICHG		0x29
#define LTC3350_REG_MEAS_DTEMP		0x2A

/* LTC3350_REG_CLR_ALARMS, LTC3350_REG_MASK_ALARMS, LTC3350_REG_ALARM_REG */
#define LTC3350_MSK_CAP_UV	BIT(0)	/* capacitor undervoltage alarm */
#define LTC3350_MSK_CAP_OV	BIT(1)	/* capacitor overvoltage alarm */
#define LTC3350_MSK_GPI_UV	BIT(2)	/* GPI undervoltage alarm */
#define LTC3350_MSK_GPI_OV	BIT(3)	/* GPI overvoltage alarm */
#define LTC3350_MSK_VIN_UV	BIT(4)	/* VIN undervoltage alarm */
#define LTC3350_MSK_VIN_OV	BIT(5)	/* VIN overvoltage alarm */
#define LTC3350_MSK_VCAP_UV	BIT(6)	/* VCAP undervoltage alarm */
#define LTC3350_MSK_VCAP_OV	BIT(7)	/* VCAP overvoltage alarm */
#define LTC3350_MSK_VOUT_UV	BIT(8)	/* VOUT undervoltage alarm */
#define LTC3350_MSK_VOUT_OV	BIT(9)	/* VOUT overvoltage alarm */
#define LTC3350_MSK_IIN_OC	BIT(10)	/* input overcurrent alarm */
#define LTC3350_MSK_ICHG_UC	BIT(11)	/* charge undercurrent alarm */
#define LTC3350_MSK_DTEMP_COLD	BIT(12)	/* die temperature cold alarm */
#define LTC3350_MSK_DTEMP_HOT	BIT(13)	/* die temperature hot alarm */
#define LTC3350_MSK_ESR_HI	BIT(14)	/* ESR high alarm */
#define LTC3350_MSK_CAP_LO	BIT(15)	/* capacitance low alarm */

/* LTC3350_REG_MSK_MON_STATUS masks */
#define LTC3350_MSK_MON_CAPESR_ACTIVE		BIT(0)
#define LTC3350_MSK_MON_CAPESR_SCHEDULED	BIT(1)
#define LTC3350_MSK_MON_CAPESR_PENDING		BIT(2)
#define LTC3350_MSK_MON_CAP_DONE		BIT(3)
#define LTC3350_MSK_MON_ESR_DONE		BIT(4)
#define LTC3350_MSK_MON_CAP_FAILED		BIT(5)
#define LTC3350_MSK_MON_ESR_FAILED		BIT(6)
#define LTC3350_MSK_MON_POWER_FAILED		BIT(8)
#define LTC3350_MSK_MON_POWER_RETURNED		BIT(9)

/* LTC3350_REG_CTL_REG */
/* Begin a capacitance and ESR measurement when possible */
#define LTC3350_CTL_STRT_CAPESR		BIT(0)
/* A one in this bit location enables the input buffer on the GPI pin */
#define LTC3350_CTL_GPI_BUFFER_EN	BIT(1)
/* Stops an active capacitance/ESR measurement */
#define LTC3350_CTL_STOP_CAPESR		BIT(2)
/* Increases capacitor measurement resolution by 100x for smaller capacitors */
#define LTC3350_CTL_CAP_SCALE		BIT(3)

/* LTC3350_REG_CHRG_STATUS */
#define LTC3350_CHRG_STEPDOWN	BIT(0)	/* Synchronous controller in step-down mode (charging) */
#define LTC3350_CHRG_STEPUP	BIT(1)	/* Synchronous controller in step-up mode (backup) */
#define LTC3350_CHRG_CV		BIT(2)	/* The charger is in constant voltage mode */
#define LTC3350_CHRG_UVLO	BIT(3)	/* The charger is in undervoltage lockout */
#define LTC3350_CHRG_INPUT_ILIM	BIT(4)	/* The charger is in input current limit */
#define LTC3350_CHRG_CAPPG	BIT(5)	/* The capacitor voltage is above power good threshold */
#define LTC3350_CHRG_SHNT	BIT(6)	/* The capacitor manager is shunting */
#define LTC3350_CHRG_BAL	BIT(7)	/* The capacitor manager is balancing */
#define LTC3350_CHRG_DIS	BIT(8)	/* Charger disabled for capacitance measurement */
#define LTC3350_CHRG_CI		BIT(9)	/* The charger is in constant current mode */
#define LTC3350_CHRG_PFO	BIT(11)	/* Input voltage is below PFI threshold */

/* LTC3350_REG_MON_STATUS */
#define LTC3350_MON_CAPESR_ACTIVE	BIT(0)	/* Capacitance/ESR measurement in progress */
#define LTC3350_MON_CAPESR_SCHEDULED	BIT(1)	/* Waiting programmed time */
#define LTC3350_MON_CAPESR_PENDING	BIT(2)	/* Waiting for satisfactory conditions */
#define LTC3350_MON_CAP_DONE		BIT(3)	/* Capacitance measurement has completed */
#define LTC3350_MON_ESR_DONE		BIT(4)	/* ESR Measurement has completed */
#define LTC3350_MON_CAP_FAILED		BIT(5)	/* Last capacitance measurement failed */
#define LTC3350_MON_ESR_FAILED		BIT(6)	/* Last ESR measurement failed */
#define LTC3350_MON_POWER_FAILED	BIT(8)	/* Unable to charge */
#define LTC3350_MON_POWER_RETURNED	BIT(9)	/* Able to charge */

struct ltc3350_info {
	struct i2c_client	*client;
	struct regmap		*regmap;
	struct power_supply	*charger;
	struct power_supply	*capacitor;
	u32 rsnsc;	/* Series resistor that sets charge current, microOhm */
	u32 rsnsi;	/* Series resistor to measure input current, microOhm */
};

/*
 * About LTC3350 "alarm" functions: Setting a bit in the LTC3350_REG_MSK_ALARMS
 * register enables the alarm. The alarm will trigger an SMBALERT only once.
 * To reset the alarm, write a "1" bit to LTC3350_REG_CLR_ALARMS. Then the alarm
 * will trigger another SMBALERT when conditions are met (may be immediately).
 * After writing to one of the corresponding level registers, enable the alarm,
 * so that a UEVENT triggers when the alarm goes off.
 */
static int ltc3350_enable_alarm(struct ltc3350_info *info, unsigned int reg)
{
	unsigned int mask;
	int ret;

	/* Register locations correspond to alarm mask bits */
	mask = BIT(reg - LTC3350_REG_CAP_UV_LVL);
	/* Clear the alarm bit so it may trigger again */
	ret = regmap_write(info->regmap, LTC3350_REG_CLR_ALARMS, mask);
	if (ret)
		return ret;
	/* Enable the alarm */
	return regmap_update_bits(info->regmap, LTC3350_REG_MSK_ALARMS, mask, mask);
}

/* Convert enum value to POWER_SUPPLY_STATUS value */
static int ltc3350_state_decode(unsigned int value)
{
	if (value & LTC3350_CHRG_STEPUP)
		return POWER_SUPPLY_STATUS_DISCHARGING; /* running on backup */

	if (value & LTC3350_CHRG_PFO)
		return POWER_SUPPLY_STATUS_NOT_CHARGING;

	if (value & LTC3350_CHRG_STEPDOWN) {
		if (value & LTC3350_CHRG_CAPPG)
			return POWER_SUPPLY_STATUS_FULL;

		return POWER_SUPPLY_STATUS_CHARGING;
	}

	/* Not in step down? Must be full then (never seen this) */
	return POWER_SUPPLY_STATUS_FULL;
}

static int ltc3350_get_status(struct ltc3350_info *info, union power_supply_propval *val)
{
	unsigned int regval;
	int ret;

	ret = regmap_read(info->regmap, LTC3350_REG_CHRG_STATUS, &regval);
	if (ret)
		return ret;

	val->intval = ltc3350_state_decode(regval);

	return 0;
}

static int ltc3350_charge_status_decode(unsigned int value)
{
	if (!(value & LTC3350_CHRG_STEPDOWN))
		return POWER_SUPPLY_CHARGE_TYPE_NONE;

	/* constant voltage is "topping off" */
	if (value & LTC3350_CHRG_CV)
		return POWER_SUPPLY_CHARGE_TYPE_TRICKLE;

	/* input limiter */
	if (value & LTC3350_CHRG_INPUT_ILIM)
		return POWER_SUPPLY_CHARGE_TYPE_ADAPTIVE;

	/* constant current is "fast" */
	if (value & LTC3350_CHRG_CI)
		return POWER_SUPPLY_CHARGE_TYPE_FAST;

	return POWER_SUPPLY_CHARGE_TYPE_UNKNOWN;
}

static int ltc3350_get_charge_type(struct ltc3350_info *info, union power_supply_propval *val)
{
	unsigned int regval;
	int ret;

	ret = regmap_read(info->regmap, LTC3350_REG_CHRG_STATUS, &regval);
	if (ret)
		return ret;

	val->intval = ltc3350_charge_status_decode(regval);

	return 0;
}

static int ltc3350_get_online(struct ltc3350_info *info, union power_supply_propval *val)
{
	unsigned int regval;
	int ret;

	ret = regmap_read(info->regmap, LTC3350_REG_MON_STATUS, &regval);
	if (ret)
		return ret;

	/* indicates if input voltage is sufficient to charge */
	val->intval = !!(regval & LTC3350_MON_POWER_RETURNED);

	return 0;
}

static int ltc3350_get_scaled(struct ltc3350_info *info, unsigned int reg,
			      int scale, int *value)
{
	unsigned int regval;
	int ret;

	ret = regmap_read(info->regmap, reg, &regval);
	if (ret)
		return ret;

	/*
	 * The "scale" here is 10 μV per LSB, this allows all calculations to
	 * be done in 32-bit integer without overflow. This converts the
	 * register value to μV.
	 */
	*value = sign_extend32(regval, 15) * scale / 10;

	return 0;
}

static int ltc3350_set_scaled(struct ltc3350_info *info, unsigned int reg, int scale, int value)
{
	u64 regval;
	int ret;

	if (value < 0)
		return -EINVAL;

	regval = DIV_ROUND_CLOSEST_ULL((u64)value * 10, scale);
	if (regval > U16_MAX)
		return -ERANGE;

	ret = regmap_write(info->regmap, reg, regval);
	if (ret)
		return ret;

	/* When writing to one of the LVL registers, update the alarm mask */
	if (reg >= LTC3350_REG_CAP_UV_LVL && reg <= LTC3350_REG_CAP_LO_LVL)
		return ltc3350_enable_alarm(info, reg);

	return 0;
}

static int ltc3350_get_input_current(struct ltc3350_info *info, union power_supply_propval *val)
{
	unsigned int regval;
	int ret;

	ret = regmap_read(info->regmap, LTC3350_REG_MEAS_IIN, &regval);
	if (ret)
		return ret;

	/* 1.983µV/RSNSI amperes per LSB */
	val->intval = div64_s64((s64)sign_extend32(regval, 15) * 1983000, info->rsnsi);

	return 0;
}

static int ltc3350_get_icharge(struct ltc3350_info *info, union power_supply_propval *val)
{
	unsigned int regval;
	int ret;

	ret = regmap_read(info->regmap, LTC3350_REG_MEAS_ICHG, &regval);
	if (ret)
		return ret;

	/* 1.983µV/RSNSC amperes per LSB */
	val->intval = div64_s64((s64)sign_extend32(regval, 15) * 1983000, info->rsnsc);

	return 0;
}

static int ltc3350_get_icharge_max(struct ltc3350_info *info, union power_supply_propval *val)
{
	/* I_CHG(MAX) = 32mV / RSNSC (Ampere) */
	val->intval = div_u64(32000000000ULL, info->rsnsc);

	return 0;
}

static int ltc3350_get_iin_max(struct ltc3350_info *info, union power_supply_propval *val)
{
	/* I_IN(MAX) = 32mV / RSNSI (Ampere) */
	val->intval = div_u64(32000000000ULL, info->rsnsi);

	return 0;
}

static int ltc3350_get_die_temp(struct ltc3350_info *info, unsigned int reg,
				union power_supply_propval *val)
{
	unsigned int regval;
	int ret;

	ret = regmap_read(info->regmap, reg, &regval);
	if (ret)
		return ret;

	/* 0.028°C per LSB – 251.4°C */
	ret = 28 * sign_extend32(regval, 15);
	ret /= 100; /* Tenths of a degree Celsius */
	ret -= 2514;
	val->intval = ret;

	return 0;
}

static int ltc3350_set_die_temp(struct ltc3350_info *info, unsigned int reg, int val)
{
	u64 regval;
	int ret;

	/* 0.028 degrees Celsius per LSB - 251.4 degrees Celsius */
	if (val < -2514)
		return -ERANGE;

	regval = DIV_ROUND_CLOSEST_ULL(((s64)val + 2514) * 100, 28);
	if (regval > U16_MAX)
		return -ERANGE;

	ret = regmap_write(info->regmap, reg, regval);
	if (ret)
		return ret;

	return ltc3350_enable_alarm(info, reg);
}

static int ltc3350_get_num_caps(struct ltc3350_info *info, union power_supply_propval *val)
{
	unsigned int regval;
	int ret;

	ret = regmap_read(info->regmap, LTC3350_REG_NUM_CAPS, &regval);
	if (ret)
		return ret;

	val->intval = regval + 1;

	return 0;
}

/* Custom properties */
static ssize_t ltc3350_attr_show(struct device *dev,
				 struct device_attribute *attr, char *buf,
				 unsigned int reg, int scale)
{
	struct power_supply *psy = to_power_supply(dev);
	struct ltc3350_info *info = power_supply_get_drvdata(psy);
	int value;
	int ret;

	ret = ltc3350_get_scaled(info, reg, scale, &value);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%d\n", value);
}

static ssize_t ltc3350_attr_store(struct device *dev, struct device_attribute *attr,
				  const char *buf, size_t count,
				  unsigned int reg, unsigned int scale)
{
	struct power_supply *psy = to_power_supply(dev);
	struct ltc3350_info *info = power_supply_get_drvdata(psy);
	int val;
	int ret;

	ret = kstrtoint(buf, 10, &val);
	if (ret)
		return ret;

	ret = ltc3350_set_scaled(info, reg, scale, val);
	if (ret)
		return ret;

	return count;
}

#define LTC3350_DEVICE_ATTR_RO(_name, _reg, _scale)				\
static ssize_t _name##_show(struct device *dev, struct device_attribute *attr,	\
			    char *buf)						\
{										\
	return ltc3350_attr_show(dev, attr, buf, _reg, _scale);			\
}										\
static DEVICE_ATTR_RO(_name)

#define LTC3350_DEVICE_ATTR_RW(_name, _reg, _scale)				\
static ssize_t _name##_show(struct device *dev, struct device_attribute *attr,	\
			    char *buf)						\
{										\
	return ltc3350_attr_show(dev, attr, buf, _reg, _scale);			\
}										\
static ssize_t _name##_store(struct device *dev, struct device_attribute *attr,\
			     const char *buf, size_t count)			\
{										\
	return ltc3350_attr_store(dev, attr, buf, count, _reg, _scale);		\
}										\
static DEVICE_ATTR_RW(_name)

/* Shunt voltage, 183.5μV per LSB */
LTC3350_DEVICE_ATTR_RW(vshunt, LTC3350_REG_VSHUNT, 1835);

/* General purpose input, 183.5μV per LSB */
LTC3350_DEVICE_ATTR_RO(gpi, LTC3350_REG_MEAS_GPI, 1835);
LTC3350_DEVICE_ATTR_RW(gpi_uv, LTC3350_REG_GPI_UV_LVL, 1835);
LTC3350_DEVICE_ATTR_RW(gpi_ov, LTC3350_REG_GPI_OV_LVL, 1835);

/* Input voltage, 2.21mV per LSB */
LTC3350_DEVICE_ATTR_RO(vin, LTC3350_REG_MEAS_VIN, 22100);
LTC3350_DEVICE_ATTR_RW(vin_uv, LTC3350_REG_VIN_UV_LVL, 22100);
LTC3350_DEVICE_ATTR_RW(vin_ov, LTC3350_REG_VIN_OV_LVL, 22100);

static ssize_t charge_status_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct power_supply *psy = to_power_supply(dev);
	struct ltc3350_info *info = power_supply_get_drvdata(psy);
	unsigned int regval;
	int ret;

	ret = regmap_read(info->regmap, LTC3350_REG_CHRG_STATUS, &regval);
	if (ret)
		return ret;

	return sysfs_emit(buf, "0x%x\n", regval);
}
static DEVICE_ATTR_RO(charge_status);

static struct attribute *ltc3350_sysfs_entries[] = {
	&dev_attr_vshunt.attr,
	&dev_attr_gpi.attr,
	&dev_attr_gpi_uv.attr,
	&dev_attr_gpi_ov.attr,
	&dev_attr_vin.attr,
	&dev_attr_vin_uv.attr,
	&dev_attr_vin_ov.attr,
	&dev_attr_charge_status.attr,
	NULL,
};

static const struct attribute_group ltc3350_attr_group = {
	.name	= NULL,	/* put in device directory */
	.attrs	= ltc3350_sysfs_entries,
};

static const struct attribute_group *ltc3350_attr_groups[] = {
	&ltc3350_attr_group,
	NULL,
};

static int ltc3350_get_property(struct power_supply *psy, enum power_supply_property psp,
				union power_supply_propval *val)
{
	struct ltc3350_info *info = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		return ltc3350_get_status(info, val);
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		return ltc3350_get_charge_type(info, val);
	case POWER_SUPPLY_PROP_ONLINE:
		return ltc3350_get_online(info, val);
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		return ltc3350_get_scaled(info, LTC3350_REG_MEAS_VOUT, 22100, &val->intval);
	case POWER_SUPPLY_PROP_VOLTAGE_MIN:
		return ltc3350_get_scaled(info, LTC3350_REG_VOUT_UV_LVL, 22100, &val->intval);
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		return ltc3350_get_scaled(info, LTC3350_REG_VOUT_OV_LVL, 22100, &val->intval);
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		return ltc3350_get_input_current(info, val);
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		return ltc3350_get_icharge_max(info, val);
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		return ltc3350_get_iin_max(info, val);
	case POWER_SUPPLY_PROP_TEMP:
		return ltc3350_get_die_temp(info, LTC3350_REG_MEAS_DTEMP, val);
	case POWER_SUPPLY_PROP_TEMP_ALERT_MIN:
		return ltc3350_get_die_temp(info, LTC3350_REG_DTEMP_COLD_LVL, val);
	case POWER_SUPPLY_PROP_TEMP_ALERT_MAX:
		return ltc3350_get_die_temp(info, LTC3350_REG_DTEMP_HOT_LVL, val);
	default:
		return -EINVAL;
	}
}

static int ltc3350_set_property(struct power_supply *psy, enum power_supply_property psp,
				const union power_supply_propval *val)
{
	struct ltc3350_info *info = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_TEMP_ALERT_MIN:
		return ltc3350_set_die_temp(info, LTC3350_REG_DTEMP_COLD_LVL, val->intval);
	case POWER_SUPPLY_PROP_TEMP_ALERT_MAX:
		return ltc3350_set_die_temp(info, LTC3350_REG_DTEMP_HOT_LVL, val->intval);
	default:
		return -EINVAL;
	}
}

static int ltc3350_property_is_writeable(struct power_supply *psy, enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_VOLTAGE_MIN:
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
	case POWER_SUPPLY_PROP_TEMP_ALERT_MIN:
	case POWER_SUPPLY_PROP_TEMP_ALERT_MAX:
	case POWER_SUPPLY_PROP_CELL_VOLTAGE_MIN:
	case POWER_SUPPLY_PROP_CELL_VOLTAGE_MAX:
		return 1;
	default:
		return 0;
	}
}

/* Charger power supply property routines */
static enum power_supply_property ltc3350_properties[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_TEMP_ALERT_MIN,
	POWER_SUPPLY_PROP_TEMP_ALERT_MAX,
};

static const struct power_supply_desc ltc3350_desc = {
	.name		= "ltc3350",
	.type		= POWER_SUPPLY_TYPE_MAINS,
	.properties	= ltc3350_properties,
	.num_properties	= ARRAY_SIZE(ltc3350_properties),
	.get_property	= ltc3350_get_property,
	.set_property	= ltc3350_set_property,
	.property_is_writeable = ltc3350_property_is_writeable,
};

static int ltc3350_capacitor_get_property(struct power_supply *psy, enum power_supply_property psp,
					  union power_supply_propval *val)
{
	struct ltc3350_info *info = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		return ltc3350_get_status(info, val);
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_CAPACITOR;
		return 0;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		/* Capacitor stack voltage, 1.476 mV per LSB */
		return ltc3350_get_scaled(info, LTC3350_REG_MEAS_VCAP, 14760, &val->intval);
	case POWER_SUPPLY_PROP_VOLTAGE_MIN:
		return ltc3350_get_scaled(info, LTC3350_REG_VCAP_UV_LVL, 14760, &val->intval);
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		return ltc3350_get_scaled(info, LTC3350_REG_VCAP_OV_LVL, 14760, &val->intval);
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		return ltc3350_get_icharge(info, val);
	case POWER_SUPPLY_PROP_NUMBER_OF_SERIAL_CELLS:
		return ltc3350_get_num_caps(info, val);
	case POWER_SUPPLY_PROP_CELL1_VOLTAGE_NOW:
		/* Single capacitor voltages, 183.5μV per LSB */
		return ltc3350_get_scaled(info, LTC3350_REG_MEAS_VCAP1, 1835, &val->intval);
	case POWER_SUPPLY_PROP_CELL2_VOLTAGE_NOW:
		return ltc3350_get_scaled(info, LTC3350_REG_MEAS_VCAP2, 1835, &val->intval);
	case POWER_SUPPLY_PROP_CELL3_VOLTAGE_NOW:
		return ltc3350_get_scaled(info, LTC3350_REG_MEAS_VCAP3, 1835, &val->intval);
	case POWER_SUPPLY_PROP_CELL4_VOLTAGE_NOW:
		return ltc3350_get_scaled(info, LTC3350_REG_MEAS_VCAP4, 1835, &val->intval);
	case POWER_SUPPLY_PROP_CELL_VOLTAGE_MIN:
		return ltc3350_get_scaled(info, LTC3350_REG_CAP_UV_LVL, 1835, &val->intval);
	case POWER_SUPPLY_PROP_CELL_VOLTAGE_MAX:
		return ltc3350_get_scaled(info, LTC3350_REG_CAP_OV_LVL, 1835, &val->intval);
	default:
		return -EINVAL;
	}
}

static int ltc3350_capacitor_set_property(struct power_supply *psy, enum power_supply_property psp,
					  const union power_supply_propval *val)
{
	struct ltc3350_info *info = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_VOLTAGE_MIN:
		return ltc3350_set_scaled(info, LTC3350_REG_VCAP_UV_LVL, 14760, val->intval);
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		return ltc3350_set_scaled(info, LTC3350_REG_VCAP_OV_LVL, 14760, val->intval);
	case POWER_SUPPLY_PROP_CELL_VOLTAGE_MIN:
		return ltc3350_set_scaled(info, LTC3350_REG_CAP_UV_LVL, 1835, val->intval);
	case POWER_SUPPLY_PROP_CELL_VOLTAGE_MAX:
		return ltc3350_set_scaled(info, LTC3350_REG_CAP_OV_LVL, 1835, val->intval);
	default:
		return -EINVAL;
	}
}

static enum power_supply_property ltc3350_capacitor_properties[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_MIN,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_NUMBER_OF_SERIAL_CELLS,
	POWER_SUPPLY_PROP_CELL1_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CELL2_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CELL3_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CELL4_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CELL_VOLTAGE_MIN,
	POWER_SUPPLY_PROP_CELL_VOLTAGE_MAX,
};

static const struct power_supply_desc ltc3350_capacitor_desc = {
	.name		= "ltc3350_capacitor",
	.type		= POWER_SUPPLY_TYPE_BATTERY,
	.properties	= ltc3350_capacitor_properties,
	.num_properties	= ARRAY_SIZE(ltc3350_capacitor_properties),
	.get_property	= ltc3350_capacitor_get_property,
	.set_property	= ltc3350_capacitor_set_property,
	.property_is_writeable = ltc3350_property_is_writeable,
};

static char *ltc3350_supply_interface_capacitor[] = { "ltc3350_capacitor" };

static bool ltc3350_is_writeable_reg(struct device *dev, unsigned int reg)
{
	/* all registers up to this one are writeable */
	return reg < LTC3350_REG_NUM_CAPS;
}

static bool ltc3350_is_volatile_reg(struct device *dev, unsigned int reg)
{
	/* read-only status registers and self-clearing register */
	return reg > LTC3350_REG_NUM_CAPS || reg == LTC3350_REG_CLR_ALARMS;
}

static const struct regmap_config ltc3350_regmap_config = {
	.reg_bits	= 8,
	.val_bits	= 16,
	.val_format_endian = REGMAP_ENDIAN_LITTLE,
	.writeable_reg	= ltc3350_is_writeable_reg,
	.volatile_reg	= ltc3350_is_volatile_reg,
	.max_register	= LTC3350_REG_MEAS_DTEMP,
	.cache_type	= REGCACHE_MAPLE,
};

static int ltc3350_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct ltc3350_info *info;
	struct power_supply_config ltc3350_config = {};
	int ret;

	info = devm_kzalloc(dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->client = client;
	i2c_set_clientdata(client, info);

	info->regmap = devm_regmap_init_i2c(client, &ltc3350_regmap_config);
	if (IS_ERR(info->regmap))
		return dev_err_probe(dev, PTR_ERR(info->regmap),
				     "Failed to initialize register map\n");

	ret = device_property_read_u32(dev, "lltc,rsnsc-micro-ohms",
				       &info->rsnsc);
	if (ret)
		return dev_err_probe(dev, ret, "Missing lltc,rsnsc-micro-ohms property\n");

	if (info->rsnsc < 1000)
		return -EINVAL;

	ret = device_property_read_u32(dev, "lltc,rsnsi-micro-ohms",
				       &info->rsnsi);
	if (ret)
		return dev_err_probe(dev, ret, "Missing lltc,rsnsi-micro-ohms property\n");

	if (info->rsnsi < 1000)
		return -EINVAL;

	/* Clear and disable all interrupt sources */
	ret = regmap_write(info->regmap, LTC3350_REG_CLR_ALARMS, 0xFFFF);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to write configuration\n");

	ret = regmap_write(info->regmap, LTC3350_REG_MSK_ALARMS, 0);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to disable alarms\n");

	ret = regmap_write(info->regmap, LTC3350_REG_MSK_MON_STATUS, 0);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to disable status interrupts\n");

	ltc3350_config.fwnode = dev_fwnode(dev);
	ltc3350_config.drv_data = info;
	info->capacitor = devm_power_supply_register(dev, &ltc3350_capacitor_desc, &ltc3350_config);
	if (IS_ERR(info->capacitor)) {
		dev_err(dev, "Failed to register capacitor\n");
		return PTR_ERR(info->capacitor);
	}

	ltc3350_config.supplied_to = ltc3350_supply_interface_capacitor;
	ltc3350_config.num_supplicants = ARRAY_SIZE(ltc3350_supply_interface_capacitor);
	ltc3350_config.attr_grp = ltc3350_attr_groups;
	info->charger = devm_power_supply_register(dev, &ltc3350_desc, &ltc3350_config);
	if (IS_ERR(info->charger)) {
		dev_err(dev, "Failed to register charger\n");
		return PTR_ERR(info->charger);
	}

	/* Enable interrupts on status changes */
	ret = regmap_write(info->regmap, LTC3350_REG_MSK_MON_STATUS,
			   LTC3350_MON_POWER_FAILED | LTC3350_MON_POWER_RETURNED);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to enable status interrupts\n");

	return 0;
}

static void ltc3350_alert(struct i2c_client *client, enum i2c_alert_protocol type,
			  unsigned int flag)
{
	struct ltc3350_info *info = i2c_get_clientdata(client);

	if (type != I2C_PROTOCOL_SMBUS_ALERT)
		return;

	power_supply_changed(info->charger);
	power_supply_changed(info->capacitor);
}

static const struct i2c_device_id ltc3350_i2c_id_table[] = {
	{ "ltc3350", 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, ltc3350_i2c_id_table);

static const struct of_device_id ltc3350_of_match[] = {
	{ .compatible = "lltc,ltc3350", },
	{ },
};
MODULE_DEVICE_TABLE(of, ltc3350_of_match);

static struct i2c_driver ltc3350_driver = {
	.probe		= ltc3350_probe,
	.alert		= ltc3350_alert,
	.id_table	= ltc3350_i2c_id_table,
	.driver = {
		.name		= "ltc3350-charger",
		.of_match_table	= ltc3350_of_match,
	},
};
module_i2c_driver(ltc3350_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mike Looijmans <mike.looijmans@topic.nl>");
MODULE_DESCRIPTION("LTC3350 charger driver");
