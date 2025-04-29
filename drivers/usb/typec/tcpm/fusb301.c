// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, LGE Inc. All rights reserved.
 *
 * SPDX-FileCopyrightText: Copyright (c) 2022-2023 NVIDIA CORPORATION & AFFILIATES.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */


#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_device.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/power_supply.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/pm_wakeup.h>
#include <linux/version.h>

#include <linux/workqueue.h>
#include <linux/usb/role.h>

#undef  __CONST_FFS
#define __CONST_FFS(_x) \
	((_x) & 0x0F ? ((_x) & 0x03 ? ((_x) & 0x01 ? 0 : 1) :\
					((_x) & 0x04 ? 2 : 3)) :\
			((_x) & 0x30 ? ((_x) & 0x10 ? 4 : 5) :\
					((_x) & 0x40 ? 6 : 7)))
#undef  FFS
#define FFS(_x) \
	((_x) ? __CONST_FFS(_x) : 0)
#undef  BITS
#define BITS(_end, _start) \
	((BIT(_end) - BIT(_start)) + BIT(_end))
#undef  __BITS_GET
#define __BITS_GET(_byte, _mask, _shift) \
	(((_byte) & (_mask)) >> (_shift))
#undef  BITS_GET
#define BITS_GET(_byte, _bit) \
	__BITS_GET(_byte, _bit, FFS(_bit))
#undef  __BITS_SET
#define __BITS_SET(_byte, _mask, _shift, _val) \
	(((_byte) & ~(_mask)) | (((_val) << (_shift)) & (_mask)))
#undef  BITS_SET
#define BITS_SET(_byte, _bit, _val) \
	__BITS_SET(_byte, _bit, FFS(_bit), _val)
#undef  BITS_MATCH
#define BITS_MATCH(_byte, _bit) \
	(((_byte) & (_bit)) == (_bit))
#define IS_ERR_VALUE_FUSB301(x) unlikely((unsigned long)(x) >= (unsigned long)-MAX_ERRNO)
/* Register Map */
#define FUSB301_REG_DEVICEID            0x01
#define FUSB301_REG_MODES               0x02
#define FUSB301_REG_CONTROL             0x03
#define FUSB301_REG_MANUAL              0x04
#define FUSB301_REG_RESET               0x05
#define FUSB301_REG_MASK                0x10
#define FUSB301_REG_STATUS              0x11
#define FUSB301_REG_TYPE                0x12
#define FUSB301_REG_INT                 0x13
/* Register Vaules */
#define FUSB301_DRP_ACC                 BIT(5)
#define FUSB301_DRP                     BIT(4)
#define FUSB301_SNK_ACC                 BIT(3)
#define FUSB301_SNK                     BIT(2)
#define FUSB301_SRC_ACC                 BIT(1)
#define FUSB301_SRC                     BIT(0)
#define FUSB301_TGL_35MS                0
#define FUSB301_TGL_30MS                1
#define FUSB301_TGL_25MS                2
#define FUSB301_TGL_20MS                3
#define FUSB301_HOST_0MA                0
#define FUSB301_HOST_DEFAULT            1
#define FUSB301_HOST_1500MA             2
#define FUSB301_HOST_3000MA             3
#define FUSB301_INT_ENABLE              0x00
#define FUSB301_INT_DISABLE             0x01
#define FUSB301_UNATT_SNK               BIT(3)
#define FUSB301_UNATT_SRC               BIT(2)
#define FUSB301_DISABLED                BIT(1)
#define FUSB301_ERR_REC                 BIT(0)
#define FUSB301_DISABLED_CLEAR          0x00
#define FUSB301_SW_RESET                BIT(0)
#define FUSB301_M_ACC_CH                BIT(3)
#define FUSB301_M_BCLVL                 BIT(2)
#define FUSB301_M_DETACH                BIT(1)
#define FUSB301_M_ATTACH                BIT(0)
#define FUSB301_FAULT_CC                0x30
#define FUSB301_CC2                     0x20
#define FUSB301_CC1                     0x10
#define FUSB301_NO_CONN                 0x00
#define FUSB301_VBUS_OK                 0x08
#define FUSB301_SNK_0MA                 0x00
#define FUSB301_SNK_DEFAULT             0x02
#define FUSB301_SNK_1500MA              0x04
#define FUSB301_SNK_3000MA              0x06
#define FUSB301_ATTACH                  0x01
#define FUSB301_TYPE_SNK                BIT(4)
#define FUSB301_TYPE_SRC                BIT(3)
#define FUSB301_TYPE_PWR_ACC            BIT(2)
#define FUSB301_TYPE_DBG_ACC            BIT(1)
#define FUSB301_TYPE_AUD_ACC            BIT(0)
#define FUSB301_TYPE_PWR_DBG_ACC       (FUSB301_TYPE_PWR_ACC|\
					FUSB301_TYPE_DBG_ACC)
#define FUSB301_TYPE_PWR_AUD_ACC       (FUSB301_TYPE_PWR_ACC|\
					FUSB301_TYPE_AUD_ACC)
#define FUSB301_TYPE_INVALID            0x00
#define FUSB301_INT_ACC_CH              BIT(3)
#define FUSB301_INT_BCLVL               BIT(2)
#define FUSB301_INT_DETACH              BIT(1)
#define FUSB301_INT_ATTACH              BIT(0)
#define FUSB301_REV10                   0x10
#define FUSB301_REV11                   0x11
#define FUSB301_REV12                   0x12
/* Mask */
#define FUSB301_TGL_MASK                0x30
#define FUSB301_HOST_CUR_MASK           0x06
#define FUSB301_INT_MASK                0x01
#define FUSB301_BCLVL_MASK              0x06
#define FUSB301_TYPE_MASK               0x1F
#define FUSB301_MODE_MASK               0x3F
#define FUSB301_INT_STS_MASK            0x0F
#define FUSB301_MAX_TRY_COUNT           10
/* FUSB STATES */
#define FUSB_STATE_DISABLED             0x00
#define FUSB_STATE_ERROR_RECOVERY       0x01
#define FUSB_STATE_UNATTACHED_SNK       0x02
#define FUSB_STATE_UNATTACHED_SRC       0x03
#define FUSB_STATE_ATTACHWAIT_SNK       0x04
#define FUSB_STATE_ATTACHWAIT_SRC       0x05
#define FUSB_STATE_ATTACHED_SNK         0x06
#define FUSB_STATE_ATTACHED_SRC         0x07
#define FUSB_STATE_AUDIO_ACCESSORY      0x08
#define FUSB_STATE_DEBUG_ACCESSORY      0x09
#define FUSB_STATE_TRY_SNK              0x0A
#define FUSB_STATE_TRYWAIT_SRC          0x0B
#define FUSB_STATE_TRY_SRC              0x0C
#define FUSB_STATE_TRYWAIT_SNK          0x0D
/* wake lock timeout in ms */
#define FUSB301_WAKE_LOCK_TIMEOUT       1000
#define ROLE_SWITCH_TIMEOUT		1500
#define FUSB301_TRY_TIMEOUT		600
#define FUSB301_CC_DEBOUNCE_TIMEOUT	200

struct fusb301_data {
	u32 init_mode;
	u32 dfp_power;
	u32 dttime;
	bool try_snk_emulation;
	u32 ttry_timeout;
	u32 ccdebounce_timeout;
};

struct fusb301_chip {
	struct i2c_client *client;
	struct fusb301_data *pdata;
	struct workqueue_struct  *cc_wq;
	int ufp_power;
	u8 mode;
	u8 dev_id;
	u8 type;
	u8 state;
	u8 orient;
	u8 bc_lvl;
	u8 dfp_power;
	u8 dttime;
	bool triedsnk;
	int try_attcnt;
	struct work_struct dwork;
	struct delayed_work twork;
	struct wakeup_source *wlock;
	struct mutex mlock;

	struct usb_role_switch *role_sw;
};

#define STR(s)    #s
#define STRV(s)   STR(s)

static void fusb301_detach(struct fusb301_chip *chip);
DECLARE_WAIT_QUEUE_HEAD(mode_switch);

static void fusb_update_state(struct fusb301_chip *chip, u8 state)
{
	if (chip && state < FUSB_STATE_TRY_SRC) {
		chip->state = state;
		dev_info(&chip->client->dev, "%s: %x\n", __func__, state);
		wake_up_interruptible(&mode_switch);
	}
}

static int fusb301_write_masked_byte(struct i2c_client *client,
					u8 addr, u8 mask, u8 val)
{
	int rc;

	if (!mask) {
		/* no actual access */
		rc = -EINVAL;
		goto out;
	}
	rc = i2c_smbus_read_byte_data(client, addr);
	if (!IS_ERR_VALUE_FUSB301(rc)) {
		rc = i2c_smbus_write_byte_data(client,
			addr, BITS_SET((u8)rc, mask, val));
		if (IS_ERR_VALUE_FUSB301(rc))
			pr_err("%s : write iic failed.\n", __func__);
	} else {
		pr_err("%s : read iic failed.\n", __func__);
		return rc;
	}
out:
	return rc;
}

static int fusb301_read_device_id(struct i2c_client *client)
{
	struct device *cdev = &client->dev;
	int rc = 0;

	rc = i2c_smbus_read_byte_data(client, FUSB301_REG_DEVICEID);

	if ((IS_ERR_VALUE_FUSB301(rc)) || (rc != 0x12)) {
		dev_err(cdev, "failed to read device id, err : 0x%2x\n", rc);
		return (rc != 0x12) ? (-1) : (-2);
	}
	dev_info(cdev, "device id: 0x%2x\n", rc);
	return rc;
}

static int fusb301_get_current_setting(struct fusb301_chip *chip)
{
	struct device *cdev = &chip->client->dev;
	int rc;
	u16 control_now;
	/* read mode & control register */
	rc = i2c_smbus_read_word_data(chip->client, FUSB301_REG_MODES);
	if (IS_ERR_VALUE_FUSB301(rc)) {
		dev_err(cdev, "%s: failed to read mode\n", __func__);
		return rc;
	}
	chip->mode = rc & FUSB301_MODE_MASK;
	control_now = (rc >> 8) & 0xFF;
	chip->dfp_power = BITS_GET(control_now, FUSB301_HOST_CUR_MASK);
	chip->dttime = BITS_GET(control_now, FUSB301_TGL_MASK);
	return 0;
}

/*
 * spec lets transitioning to below states from any state
 *  FUSB_STATE_DISABLED
 *  FUSB_STATE_ERROR_RECOVERY
 *  FUSB_STATE_UNATTACHED_SNK
 *  FUSB_STATE_UNATTACHED_SRC
 */
static int fusb301_set_chip_state(struct fusb301_chip *chip, u8 state)
{
	struct device *cdev = &chip->client->dev;
	int rc = 0;

	if (state > FUSB_STATE_UNATTACHED_SRC)
		return -EINVAL;
	rc = i2c_smbus_write_byte_data(chip->client, FUSB301_REG_MANUAL,
				state == FUSB_STATE_DISABLED ? FUSB301_DISABLED :
				state == FUSB_STATE_ERROR_RECOVERY ? FUSB301_ERR_REC :
				state == FUSB_STATE_UNATTACHED_SNK ? FUSB301_UNATT_SNK :
				FUSB301_UNATT_SRC);
	if (IS_ERR_VALUE_FUSB301(rc))
		dev_err(cdev, "failed to write manual(%d)\n", rc);
	return rc;
}

static int fusb301_set_mode(struct fusb301_chip *chip, u8 mode)
{
	struct device *cdev = &chip->client->dev;
	int rc = 0;

	if (mode > FUSB301_DRP_ACC) {
		dev_err(cdev, "mode(%d) is unavailable\n", mode);
		return -EINVAL;
	}
	if (mode != chip->mode) {
		rc = i2c_smbus_write_byte_data(chip->client,
				FUSB301_REG_MODES, mode);
		if (IS_ERR_VALUE_FUSB301(rc)) {
			dev_err(cdev, "%s: failed to write mode\n", __func__);
			return rc;
		}
		chip->mode = mode;
	}
	dev_info(cdev, "%s: mode (%d)(%d)\n", __func__, chip->mode, mode);
	return rc;
}

/* Set output current indicator */
static int fusb301_set_dfp_power(struct fusb301_chip *chip, u8 hcurrent)
{
	struct device *cdev = &chip->client->dev;
	int rc = 0;

	if (hcurrent > FUSB301_HOST_3000MA) {
		dev_err(cdev, "hcurrent(%d) is unavailable\n", hcurrent);
		return -EINVAL;
	}
	if (hcurrent == chip->dfp_power) {
		dev_err(cdev, "hcurrent(%d) is not updated\n", hcurrent);
		return rc;
	}
	rc = fusb301_write_masked_byte(chip->client,
					FUSB301_REG_CONTROL,
					FUSB301_HOST_CUR_MASK,
					hcurrent);
	if (IS_ERR_VALUE_FUSB301(rc)) {
		dev_err(cdev, "failed to write current(%d)\n", rc);
		return rc;
	}
	chip->dfp_power = hcurrent;
	dev_info(cdev, "%s: host current(%d)\n", __func__, hcurrent);

	return rc;
}

/*
 * When 3A capable DRP device is connected without VBUS,
 * DRP always detect it as SINK device erroneously.
 * Since USB Type-C specification 1.0 and 1.1 doesn't
 * consider this corner case, apply workaround for this case.
 * Set host mode current to 1.5A initially, and then change
 * it to default USB current right after detection SINK port.
 */
static int fusb301_init_force_dfp_power(struct fusb301_chip *chip)
{
	struct device *cdev = &chip->client->dev;
	int rc = 0;

	rc = fusb301_write_masked_byte(chip->client,
					FUSB301_REG_CONTROL,
					FUSB301_HOST_CUR_MASK,
					FUSB301_HOST_1500MA);
	if (IS_ERR_VALUE_FUSB301(rc)) {
		dev_err(cdev, "failed to write current\n");
		return rc;
	}
	chip->dfp_power = FUSB301_HOST_1500MA;
	return rc;
}

static int fusb301_set_toggle_time(struct fusb301_chip *chip, u8 toggle_time)
{
	struct device *cdev = &chip->client->dev;
	int rc = 0;

	if (toggle_time > FUSB301_TGL_20MS) {
		dev_err(cdev, "toggle_time(%d) is unavailable\n", toggle_time);
		return -EINVAL;
	}
	if (toggle_time == chip->dttime) {
		dev_info(cdev, "toggle_time(%d) is not updated\n", toggle_time);
		return rc;
	}
	rc = fusb301_write_masked_byte(chip->client,
					FUSB301_REG_CONTROL,
					FUSB301_TGL_MASK,
					toggle_time);
	if (IS_ERR_VALUE_FUSB301(rc)) {
		dev_err(cdev, "failed to write toggle time\n");
		return rc;
	}
	chip->dttime = toggle_time;
	return rc;
}

static int fusb301_init_reg(struct fusb301_chip *chip)
{
	struct device *cdev = &chip->client->dev;
	int rc = 0;

	/* change current */
	rc = fusb301_init_force_dfp_power(chip);
	if (IS_ERR_VALUE_FUSB301(rc))
		dev_err(cdev, "%s: failed to force dfp power\n",
				__func__);

	/* change toggle time */
	rc = fusb301_set_toggle_time(chip, chip->pdata->dttime);
	if (IS_ERR_VALUE_FUSB301(rc))
		dev_err(cdev, "%s: failed to set toggle time\n",
				__func__);
	/* change mode */
	/*
	 * force to DRP+ACC,chip->pdata->init_mode
	 */
	rc = fusb301_set_mode(chip, FUSB301_DRP_ACC);
	if (IS_ERR_VALUE_FUSB301(rc))
		dev_err(cdev, "%s: failed to set mode\n",
				__func__);
	/* set error recovery state */
	rc = fusb301_set_chip_state(chip,
				FUSB_STATE_ERROR_RECOVERY);
	if (IS_ERR_VALUE_FUSB301(rc))
		dev_err(cdev, "%s: failed to set error recovery state\n",
				__func__);
	return rc;
}

static int fusb301_reset_device(struct fusb301_chip *chip)
{
	struct device *cdev = &chip->client->dev;
	int rc = 0;

	rc = i2c_smbus_write_byte_data(chip->client,
					FUSB301_REG_RESET,
					FUSB301_SW_RESET);
	if (IS_ERR_VALUE_FUSB301(rc)) {
		dev_err(cdev, "reset fails\n");
		return rc;
	}

	msleep(20);

	rc = fusb301_get_current_setting(chip);
	if (IS_ERR_VALUE_FUSB301(rc))
		dev_err(cdev, "failed to read settings\n");
	rc = fusb301_init_reg(chip);
	if (IS_ERR_VALUE_FUSB301(rc))
		dev_err(cdev, "failed to init reg\n");

	fusb301_detach(chip);
	/* clear global interrupt mask */
	rc = fusb301_write_masked_byte(chip->client,
				FUSB301_REG_CONTROL,
				FUSB301_INT_MASK,
				FUSB301_INT_ENABLE);
	if (IS_ERR_VALUE_FUSB301(rc)) {
		dev_err(cdev, "%s: failed to init\n", __func__);
		return rc;
	}
	dev_info(cdev, "mode[0x%02x], host_cur[0x%02x], dttime[0x%02x]\n",
			chip->mode, chip->dfp_power, chip->dttime);
	return rc;
}

static ssize_t fregdump_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct fusb301_chip *chip = i2c_get_clientdata(client);
	u8 start_reg[] = {0x01, 0x02,
			  0x03, 0x04,
			  0x05, 0x06,
			  0x10, 0x11,
			  0x12, 0x13}; /* reserved 0x06 */
	int i = 0;
	int rc = 0;
	int ret = 0;

	mutex_lock(&chip->mlock);
	for (i = 0 ; i < 5; i++) {
		rc = i2c_smbus_read_word_data(chip->client, start_reg[(i*2)]);
		if (IS_ERR_VALUE_FUSB301(rc)) {
			pr_err("cannot read 0x%02x\n", start_reg[(i*2)]);
			rc = 0;
			goto dump_unlock;
		}
		ret += snprintf(buf + ret, 1024, "from 0x%02x read 0x%02x\n"
						"from 0x%02x read 0x%02x\n",
							start_reg[(i*2)],
							(rc & 0xFF),
							start_reg[(i*2)+1],
							((rc >> 8) & 0xFF));
	}
dump_unlock:
	mutex_unlock(&chip->mlock);
	return ret;
}
DEVICE_ATTR_RO(fregdump);

static ssize_t ftype_show(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct fusb301_chip *chip = i2c_get_clientdata(client);
	int ret;

	mutex_lock(&chip->mlock);
	switch (chip->type) {
	case FUSB301_TYPE_SNK:
		ret = snprintf(buf, PAGE_SIZE, "SINK(%d)\n", chip->type);
		break;
	case FUSB301_TYPE_SRC:
		ret = snprintf(buf, PAGE_SIZE, "SOURCE(%d)\n", chip->type);
		break;
	case FUSB301_TYPE_PWR_ACC:
		ret = snprintf(buf, PAGE_SIZE, "PWRACC(%d)\n", chip->type);
		break;
	case FUSB301_TYPE_DBG_ACC:
		ret = snprintf(buf, PAGE_SIZE, "DEBUGACC(%d)\n", chip->type);
		break;
	case FUSB301_TYPE_PWR_DBG_ACC:
		ret = snprintf(buf, PAGE_SIZE, "POWEREDDEBUGACC(%d)\n", chip->type);
		break;
	case FUSB301_TYPE_AUD_ACC:
		ret = snprintf(buf, PAGE_SIZE, "AUDIOACC(%d)\n", chip->type);
		break;
	case FUSB301_TYPE_PWR_AUD_ACC:
		ret = snprintf(buf, PAGE_SIZE, "POWEREDAUDIOACC(%d)\n", chip->type);
		break;
	default:
		ret = snprintf(buf, PAGE_SIZE, "NOTYPE(%d)\n", chip->type);
		break;
	}
	mutex_unlock(&chip->mlock);
	return ret;
}
DEVICE_ATTR_RO(ftype);

static ssize_t fchip_state_show(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	return snprintf(buf, PAGE_SIZE,
			STRV(FUSB_STATE_DISABLED) " - FUSB_STATE_DISABLED\n"
			STRV(FUSB_STATE_ERROR_RECOVERY) " - FUSB_STATE_ERROR_RECOVERY\n"
			STRV(FUSB_STATE_UNATTACHED_SNK) " - FUSB_STATE_UNATTACHED_SNK\n"
			STRV(FUSB_STATE_UNATTACHED_SRC) " - FUSB_STATE_UNATTACHED_SRC\n");
}

static ssize_t fchip_state_store(struct device *dev,
				struct device_attribute *attr,
				const char *buff, size_t size)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct fusb301_chip *chip = i2c_get_clientdata(client);
	int state = 0;
	int rc = 0;

	if (!kstrtoint(buff, 0, &state)) {
		mutex_lock(&chip->mlock);
		if (((state == FUSB_STATE_UNATTACHED_SNK) &&
			(chip->mode & (FUSB301_SRC | FUSB301_SRC_ACC))) ||
			((state == FUSB_STATE_UNATTACHED_SRC) &&
			(chip->mode & (FUSB301_SNK | FUSB301_SNK_ACC)))) {
			mutex_unlock(&chip->mlock);
			return -EINVAL;
		}
		rc = fusb301_set_chip_state(chip, (u8)state);
		if (IS_ERR_VALUE_FUSB301(rc)) {
			mutex_unlock(&chip->mlock);
			return rc;
		}
		fusb301_detach(chip);
		mutex_unlock(&chip->mlock);
		return size;
	}
	return -EINVAL;
}
DEVICE_ATTR_RW(fchip_state);

static ssize_t fmode_show(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct fusb301_chip *chip = i2c_get_clientdata(client);
	int ret;

	mutex_lock(&chip->mlock);
	switch (chip->mode) {
	case FUSB301_DRP_ACC:
		ret = snprintf(buf, PAGE_SIZE, "DRP+ACC(%d)\n", chip->mode);
		break;
	case FUSB301_DRP:
		ret = snprintf(buf, PAGE_SIZE, "DRP(%d)\n", chip->mode);
		break;
	case FUSB301_SNK_ACC:
		ret = snprintf(buf, PAGE_SIZE, "SNK+ACC(%d)\n", chip->mode);
		break;
	case FUSB301_SNK:
		ret = snprintf(buf, PAGE_SIZE, "SNK(%d)\n", chip->mode);
		break;
	case FUSB301_SRC_ACC:
		ret = snprintf(buf, PAGE_SIZE, "SRC+ACC(%d)\n", chip->mode);
		break;
	case FUSB301_SRC:
		ret = snprintf(buf, PAGE_SIZE, "SRC(%d)\n", chip->mode);
		break;
	default:
		ret = snprintf(buf, PAGE_SIZE, "UNKNOWN(%d)\n", chip->mode);
		break;
	}
	mutex_unlock(&chip->mlock);
	return ret;
}

static ssize_t fmode_store(struct device *dev,
				struct device_attribute *attr,
				const char *buff, size_t size)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct fusb301_chip *chip = i2c_get_clientdata(client);
	int mode = 0;
	int rc = 0;

	if (!kstrtoint(buff, 0, &mode)) {
		mutex_lock(&chip->mlock);
		/*
		 * since device trigger to usb happens independent
		 * from charger based on vbus, setting SRC modes
		 * doesn't prevent usb enumeration as device
		 * KNOWN LIMITATION
		 */
		rc = fusb301_set_mode(chip, (u8)mode);
		if (IS_ERR_VALUE_FUSB301(rc)) {
			mutex_unlock(&chip->mlock);
			return rc;
		}
		rc = fusb301_set_chip_state(chip,
					FUSB_STATE_ERROR_RECOVERY);
		if (IS_ERR_VALUE_FUSB301(rc)) {
			mutex_unlock(&chip->mlock);
			return rc;
		}
		fusb301_detach(chip);
		mutex_unlock(&chip->mlock);
		return size;
	}
	return -EINVAL;
}
DEVICE_ATTR_RW(fmode);

static ssize_t fdttime_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct fusb301_chip *chip = i2c_get_clientdata(client);
	int ret;

	mutex_lock(&chip->mlock);
	ret = snprintf(buf, PAGE_SIZE, "%u\n", chip->dttime);
	mutex_unlock(&chip->mlock);
	return ret;
}

static ssize_t fdttime_store(struct device *dev,
				struct device_attribute *attr,
				const char *buff, size_t size)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct fusb301_chip *chip = i2c_get_clientdata(client);
	int dttime = 0;
	int rc = 0;

	if (!kstrtoint(buff, 0, &dttime)) {
		mutex_lock(&chip->mlock);
		rc = fusb301_set_toggle_time(chip, (u8)dttime);
		mutex_unlock(&chip->mlock);
		if (IS_ERR_VALUE_FUSB301(rc))
			return rc;
		return size;
	}
	return -EINVAL;
}
DEVICE_ATTR_RW(fdttime);

static ssize_t fhostcur_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct fusb301_chip *chip = i2c_get_clientdata(client);
	int ret;

	mutex_lock(&chip->mlock);
	ret = snprintf(buf, PAGE_SIZE, "%u\n", chip->dfp_power);
	mutex_unlock(&chip->mlock);
	return ret;
}

static ssize_t fhostcur_store(struct device *dev,
				struct device_attribute *attr,
				const char *buff, size_t size)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct fusb301_chip *chip = i2c_get_clientdata(client);
	int buf = 0;
	int rc = 0;

	if (!kstrtoint(buff, 0, &buf)) {
		mutex_lock(&chip->mlock);
		rc = fusb301_set_dfp_power(chip, (u8)buf);
		mutex_unlock(&chip->mlock);
		if (IS_ERR_VALUE_FUSB301(rc))
			return rc;
		return size;
	}
	return -EINVAL;
}
DEVICE_ATTR_RW(fhostcur);

static ssize_t fclientcur_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct fusb301_chip *chip = i2c_get_clientdata(client);
	int ret;

	mutex_lock(&chip->mlock);
	ret = snprintf(buf, PAGE_SIZE, "%d\n", chip->ufp_power);
	mutex_unlock(&chip->mlock);
	return ret;
}
DEVICE_ATTR_RO(fclientcur);

static ssize_t freset_store(struct device *dev,
				struct device_attribute *attr,
				const char *buff, size_t size)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct fusb301_chip *chip = i2c_get_clientdata(client);
	u32 reset = 0;
	int rc = 0;

	if (!kstrtou32(buff, 0, &reset)) {
		mutex_lock(&chip->mlock);
		rc = fusb301_reset_device(chip);
		mutex_unlock(&chip->mlock);
		if (IS_ERR_VALUE_FUSB301(rc))
			return rc;
		return size;
	}
	return -EINVAL;
}
DEVICE_ATTR_WO(freset);

static ssize_t fsw_trysnk_show(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct fusb301_chip *chip = i2c_get_clientdata(client);
	int ret;

	mutex_lock(&chip->mlock);
	ret = snprintf(buf, PAGE_SIZE, "%u\n", chip->pdata->try_snk_emulation);
	mutex_unlock(&chip->mlock);
	return ret;
}

static ssize_t fsw_trysnk_store(struct device *dev,
			struct device_attribute *attr,
			const char *buff, size_t size)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct fusb301_chip *chip = i2c_get_clientdata(client);
	int buf = 0;

	if (!kstrtoint(buff, 0, &buf) && (buf == 0 || buf == 1)) {
		mutex_lock(&chip->mlock);
		chip->pdata->try_snk_emulation = buf;
		if (chip->state == FUSB_STATE_ERROR_RECOVERY)
			chip->triedsnk = !chip->pdata->try_snk_emulation;
		mutex_unlock(&chip->mlock);
		return size;
	}
	return -EINVAL;
}
DEVICE_ATTR_RW(fsw_trysnk);

static ssize_t ftry_timeout_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct fusb301_chip *chip = i2c_get_clientdata(client);
	int ret;

	mutex_lock(&chip->mlock);
	ret = snprintf(buf, PAGE_SIZE, "%u\n", chip->pdata->ttry_timeout);
	mutex_unlock(&chip->mlock);
	return ret;
}

static ssize_t ftry_timeout_store(struct device *dev,
			struct device_attribute *attr,
			const char *buff, size_t size)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct fusb301_chip *chip = i2c_get_clientdata(client);
	int buf = 0;

	if (!kstrtoint(buff, 0, &buf) && (buf >= 0)) {
		mutex_lock(&chip->mlock);
		chip->pdata->ttry_timeout = buf;
		mutex_unlock(&chip->mlock);
		return size;
	}
	return -EINVAL;
}
DEVICE_ATTR_RW(ftry_timeout);

static ssize_t fccdebounce_timeout_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct fusb301_chip *chip = i2c_get_clientdata(client);
	int ret;

	mutex_lock(&chip->mlock);
	ret = snprintf(buf, PAGE_SIZE, "%u\n",
			chip->pdata->ccdebounce_timeout);
	mutex_unlock(&chip->mlock);
	return ret;
}

static ssize_t fccdebounce_timeout_store(struct device *dev,
				struct device_attribute *attr,
				const char *buff, size_t size)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct fusb301_chip *chip = i2c_get_clientdata(client);
	int buf = 0;

	if (!kstrtoint(buff, 0, &buf) && (buf >= 0)) {
		mutex_lock(&chip->mlock);
		chip->pdata->ccdebounce_timeout = buf;
		mutex_unlock(&chip->mlock);
		return size;
	}
	return -EINVAL;
}
DEVICE_ATTR_RW(fccdebounce_timeout);

static ssize_t ftypec_cc_orientation_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct fusb301_chip *chip = i2c_get_clientdata(client);
	int typec_cc_orientation = -1;
	int rc, ret = 0;

	rc = i2c_smbus_read_byte_data(chip->client, FUSB301_REG_STATUS);

	if (rc < 0) {
		pr_err("cannot read FUSB301_REG_STATUS\n");
		rc = 0xFF;
		ret = snprintf(buf, PAGE_SIZE, "%d\n", rc);
	} else {
		chip->orient = (rc & 0x30) >> 4;
		typec_cc_orientation = chip->orient;
		ret = snprintf(buf, PAGE_SIZE, "%d\n", typec_cc_orientation);
	}
	return ret;
}
DEVICE_ATTR_RO(ftypec_cc_orientation);

static struct attribute *fusb_sysfs_entries[] = {
	&dev_attr_fchip_state.attr,
	&dev_attr_ftype.attr,
	&dev_attr_fmode.attr,
	&dev_attr_freset.attr,
	&dev_attr_fdttime.attr,
	&dev_attr_fhostcur.attr,
	&dev_attr_fclientcur.attr,
	&dev_attr_fsw_trysnk.attr,
	&dev_attr_ftry_timeout.attr,
	&dev_attr_fccdebounce_timeout.attr,
	&dev_attr_fregdump.attr,
	&dev_attr_ftypec_cc_orientation.attr,
	NULL,
};

static struct attribute_group fusb_sysfs_group = {
	.name = "fusb301",
	.attrs = fusb_sysfs_entries,
};

static void fusb301_bclvl_changed(struct fusb301_chip *chip)
{
	struct device *cdev = &chip->client->dev;
	int  rc = 0;
	u8  status, type;

	rc = i2c_smbus_read_word_data(chip->client,
				FUSB301_REG_STATUS);
	if (IS_ERR_VALUE_FUSB301(rc)) {
		dev_err(cdev, "%s: failed to read\n", __func__);
		if (IS_ERR_VALUE_FUSB301(fusb301_reset_device(chip)))
			dev_err(cdev, "%s: failed to reset\n", __func__);
		return;
	}
	status = rc & 0xFF;
	type = (status & FUSB301_ATTACH) ?
			(rc >> 8) & FUSB301_TYPE_MASK : FUSB301_TYPE_INVALID;

	dev_dbg(cdev, "sts[0x%02x], type[0x%02x]\n", status, type);
	if (type == FUSB301_TYPE_SRC ||
			type == FUSB301_TYPE_PWR_AUD_ACC ||
			type == FUSB301_TYPE_PWR_DBG_ACC ||
			type == FUSB301_TYPE_PWR_ACC) {
		chip->orient = (status & 0x30) >> 4;
		if (chip->orient == 0x01)
			chip->orient = 0;
		else if (chip->orient == 0x02)
			chip->orient = 1;
		else
			chip->orient = 2;
		chip->bc_lvl = status & 0x06;
		chip->bc_lvl = (status & 0x06) >> 1;
	}
}

static void fusb301_acc_changed(struct fusb301_chip *chip)
{
	/* TODO */
	/* implement acc changed work */
}

static void fusb301_src_detected(struct fusb301_chip *chip)
{
	struct device *cdev = &chip->client->dev;

	if (chip->mode & (FUSB301_SRC | FUSB301_SRC_ACC)) {
		dev_err(cdev, "not support in source mode\n");
		if (IS_ERR_VALUE_FUSB301(fusb301_reset_device(chip)))
			dev_err(cdev, "%s: failed to reset\n", __func__);
		return;
	}
	if (chip->state == FUSB_STATE_TRY_SNK)
		cancel_delayed_work(&chip->twork);
	fusb_update_state(chip, FUSB_STATE_ATTACHED_SNK);
	chip->type = FUSB301_TYPE_SRC;

	usb_role_switch_set_role(chip->role_sw, USB_ROLE_DEVICE);
}

static void fusb301_snk_detected(struct fusb301_chip *chip)
{
	struct device *cdev = &chip->client->dev;

	if (chip->mode & (FUSB301_SNK | FUSB301_SNK_ACC)) {
		dev_err(cdev, "not support in sink mode\n");
		if (IS_ERR_VALUE_FUSB301(fusb301_reset_device(chip)))
			dev_err(cdev, "%s: failed to reset\n", __func__);
		return;
	}

	/* SW Try.SNK Workaround below Rev 1.2 */
	if ((!chip->triedsnk) && (chip->mode & (FUSB301_DRP | FUSB301_DRP_ACC))) {
		if (IS_ERR_VALUE_FUSB301(fusb301_set_mode(chip, FUSB301_SNK)) ||
			IS_ERR_VALUE_FUSB301(fusb301_set_chip_state(chip,
						FUSB_STATE_UNATTACHED_SNK))) {
			dev_err(cdev, "%s: failed to config trySnk\n", __func__);
			if (IS_ERR_VALUE_FUSB301(fusb301_reset_device(chip)))
				dev_err(cdev, "%s: failed to reset\n", __func__);
		} else {
			fusb_update_state(chip, FUSB_STATE_TRY_SNK);
			chip->triedsnk = true;
			queue_delayed_work(chip->cc_wq, &chip->twork,
					msecs_to_jiffies(chip->pdata->ttry_timeout));
		}
	} else {
		/*
		 * chip->triedsnk == true
		 * or
		 * mode == FUSB301_SRC/FUSB301_SRC_ACC
		 */
		fusb301_set_dfp_power(chip, chip->pdata->dfp_power);
		if (chip->state == FUSB_STATE_TRYWAIT_SRC)
			cancel_delayed_work(&chip->twork);
		fusb_update_state(chip, FUSB_STATE_ATTACHED_SRC);
		chip->type = FUSB301_TYPE_SNK;

		usb_role_switch_set_role(chip->role_sw, USB_ROLE_HOST);
	}
}

static void fusb301_dbg_acc_detected(struct fusb301_chip *chip)
{
	struct device *cdev = &chip->client->dev;

	if (chip->mode & (FUSB301_SRC | FUSB301_SNK | FUSB301_DRP)) {
		dev_err(cdev, "not support accessory mode\n");
		if (IS_ERR_VALUE_FUSB301(fusb301_reset_device(chip)))
			dev_err(cdev, "%s: failed to reset\n", __func__);
		return;
	}
	/*
	 * TODO
	 * need to implement
	 */
	fusb_update_state(chip, FUSB_STATE_DEBUG_ACCESSORY);
}

static void fusb301_aud_acc_detected(struct fusb301_chip *chip)
{
	struct device *cdev = &chip->client->dev;

	if (chip->mode & (FUSB301_SRC | FUSB301_SNK | FUSB301_DRP)) {
		dev_err(cdev, "not support accessory mode\n");
		if (IS_ERR_VALUE_FUSB301(fusb301_reset_device(chip)))
			dev_err(cdev, "%s: failed to reset\n", __func__);
		return;
	}
	/*
	 * TODO
	 * need to implement
	 */
	fusb_update_state(chip, FUSB_STATE_AUDIO_ACCESSORY);
}

static void fusb301_timer_try_expired(struct fusb301_chip *chip)
{
	struct device *cdev = &chip->client->dev;

	if (IS_ERR_VALUE_FUSB301(fusb301_set_mode(chip, FUSB301_SRC)) ||
		IS_ERR_VALUE_FUSB301(fusb301_set_chip_state(chip,
					FUSB_STATE_UNATTACHED_SRC))) {
		dev_err(cdev, "%s: failed to config tryWaitSrc\n", __func__);
		if (IS_ERR_VALUE_FUSB301(fusb301_reset_device(chip)))
			dev_err(cdev, "%s: failed to reset\n", __func__);
	} else {
		fusb_update_state(chip, FUSB_STATE_TRYWAIT_SRC);
		queue_delayed_work(chip->cc_wq, &chip->twork,
			msecs_to_jiffies(chip->pdata->ccdebounce_timeout));
	}
}

static void fusb301_detach(struct fusb301_chip *chip)
{
	struct device *cdev = &chip->client->dev;

	dev_info(cdev, "%s: type[0x%02x] chipstate[0x%02x]\n",
			__func__, chip->type, chip->state);
	switch (chip->state) {
	case FUSB_STATE_ATTACHED_SRC:
		fusb301_init_force_dfp_power(chip);
		usb_role_switch_set_role(chip->role_sw, USB_ROLE_NONE);
		break;
	case FUSB_STATE_ATTACHED_SNK:
		usb_role_switch_set_role(chip->role_sw, USB_ROLE_NONE);
		break;
	case FUSB_STATE_DEBUG_ACCESSORY:
	case FUSB_STATE_AUDIO_ACCESSORY:
		break;
	case FUSB_STATE_TRY_SNK:
	case FUSB_STATE_TRYWAIT_SRC:
		cancel_delayed_work(&chip->twork);
		break;
	case FUSB_STATE_DISABLED:
	case FUSB_STATE_ERROR_RECOVERY:
		break;
	case FUSB_STATE_TRY_SRC:
	case FUSB_STATE_TRYWAIT_SNK:
	default:
		dev_err(cdev, "%s: Invalid chipstate[0x%02x]\n",
				__func__, chip->state);
		break;
	}

	if (chip->triedsnk && chip->pdata->try_snk_emulation) {
		if (IS_ERR_VALUE_FUSB301(fusb301_set_mode(chip,
						chip->pdata->init_mode)) ||
			IS_ERR_VALUE_FUSB301(fusb301_set_chip_state(chip,
						FUSB_STATE_ERROR_RECOVERY))) {
			dev_err(cdev, "%s: failed to set init mode\n", __func__);
		}
	}
	chip->type = FUSB301_TYPE_INVALID;
	chip->bc_lvl = FUSB301_SNK_0MA;
	chip->ufp_power = 0;
	chip->triedsnk = !chip->pdata->try_snk_emulation;
	chip->try_attcnt = 0;
	fusb_update_state(chip, FUSB_STATE_ERROR_RECOVERY);
}

static bool fusb301_is_vbus_off(struct fusb301_chip *chip)
{
	struct device *cdev = &chip->client->dev;
	int rc;

	rc = i2c_smbus_read_byte_data(chip->client,
				FUSB301_REG_STATUS);
	if (IS_ERR_VALUE_FUSB301(rc)) {
		dev_err(cdev, "%s: failed to read status\n", __func__);
		return false;
	}

	return !((rc & FUSB301_ATTACH) && (rc & FUSB301_VBUS_OK));
}

static bool fusb301_is_vbus_on(struct fusb301_chip *chip)
{
	struct device *cdev = &chip->client->dev;
	int rc;

	rc = i2c_smbus_read_byte_data(chip->client, FUSB301_REG_STATUS);
	if (IS_ERR_VALUE_FUSB301(rc)) {
		dev_err(cdev, "%s: failed to read status\n", __func__);
		return false;
	}
	return !!(rc & FUSB301_VBUS_OK);
}

/* workaround BC Level detection plugging slowly with C ot A on Rev1.0 */
static bool fusb301_bclvl_detect_wa(struct fusb301_chip *chip,
							u8 status, u8 type)
{
	struct device *cdev = &chip->client->dev;
	int rc = 0;

	if (((type == FUSB301_TYPE_SRC) ||
		((type == FUSB301_TYPE_INVALID) && (status & FUSB301_VBUS_OK))) &&
		!(status & FUSB301_BCLVL_MASK) &&
		(chip->try_attcnt < FUSB301_MAX_TRY_COUNT)) {
		rc = fusb301_set_chip_state(chip,
					FUSB_STATE_ERROR_RECOVERY);
		if (IS_ERR_VALUE_FUSB301(rc)) {
			dev_err(cdev, "%s: failed to set error recovery state\n",
					__func__);
			goto err;
		}
		chip->try_attcnt++;
		msleep(100);
		/*
		 * when cable is unplug during bc level workaournd,
		 * detach interrupt does not occur
		 */
		if (fusb301_is_vbus_off(chip)) {
			chip->try_attcnt = 0;
			dev_info(cdev, "%s: vbus is off\n", __func__);
		}
		return true;
	}
err:
	chip->try_attcnt = 0;
	return false;
}

static void fusb301_attach(struct fusb301_chip *chip)
{
	struct device *cdev = &chip->client->dev;
	int rc;
	u8 status, type;

	/* get status and type */
	rc = i2c_smbus_read_word_data(chip->client, FUSB301_REG_STATUS);
	if (IS_ERR_VALUE_FUSB301(rc)) {
		dev_err(cdev, "%s: failed to read status\n", __func__);
		return;
	}
	status = rc & 0xFF;
	type = (status & FUSB301_ATTACH) ?
			(rc >> 8) & FUSB301_TYPE_MASK : FUSB301_TYPE_INVALID;
	dev_info(cdev, "sts[0x%02x], type[0x%02x]\n", status, type);

	if ((chip->state != FUSB_STATE_ERROR_RECOVERY) &&
		(chip->state != FUSB_STATE_TRY_SNK) &&
		(chip->state != FUSB_STATE_TRYWAIT_SRC)) {
		rc = fusb301_set_chip_state(chip, FUSB_STATE_ERROR_RECOVERY);
		if (IS_ERR_VALUE_FUSB301(rc))
			dev_err(cdev, "%s: failed to set error recovery\n",
					__func__);
		fusb301_detach(chip);
		dev_err(cdev, "%s: Invalid chipstate[0x%02x]\n",
				__func__, chip->state);
		return;
	}

	chip->orient = (status & 0x30) >> 4;
	if (chip->orient == 0x01)
		chip->orient = 0;
	else if (chip->orient == 0x02)
		chip->orient = 1;
	else
		chip->orient = 2;

	if ((chip->dev_id == FUSB301_REV10) &&
		fusb301_bclvl_detect_wa(chip, status, type)) {
		return;
	}

	switch (type) {
	case FUSB301_TYPE_SRC:  //The partner is a source
		fusb301_src_detected(chip);
		break;
	case FUSB301_TYPE_SNK: //The partner is a sink
		fusb301_snk_detected(chip);
		break;

	case FUSB301_TYPE_PWR_ACC:
		chip->type = type;
		break;

	case FUSB301_TYPE_DBG_ACC:
	case FUSB301_TYPE_PWR_DBG_ACC:
		fusb301_dbg_acc_detected(chip);
		chip->type = type;
		if (fusb301_is_vbus_on(chip)) {
			dev_err(cdev, "%s: vbus voltage was high\n", __func__);
			break;
		}
		break;
	case FUSB301_TYPE_AUD_ACC:
	case FUSB301_TYPE_PWR_AUD_ACC:
		fusb301_aud_acc_detected(chip);
		chip->type = type;
		break;
	case FUSB301_TYPE_INVALID:
		fusb301_detach(chip);
		dev_err(cdev, "%s: Invalid type[0x%02x]\n", __func__, type);
		break;
	default:
		rc = fusb301_set_chip_state(chip,
				FUSB_STATE_ERROR_RECOVERY);
		if (IS_ERR_VALUE_FUSB301(rc))
			dev_err(cdev, "%s: failed to set error recovery\n",
					__func__);
		fusb301_detach(chip);
		dev_err(cdev, "%s: Unknwon type[0x%02x]\n", __func__, type);
		break;
	}
}

static void fusb301_timer_work_handler(struct work_struct *work)
{
	struct fusb301_chip *chip =
			container_of(work, struct fusb301_chip, twork.work);
	struct device *cdev = &chip->client->dev;

	mutex_lock(&chip->mlock);
	if (chip->state == FUSB_STATE_TRY_SNK) {
		if (fusb301_is_vbus_on(chip)) {
			if (IS_ERR_VALUE_FUSB301(fusb301_set_mode(chip,	chip->pdata->init_mode)))
				dev_err(cdev, "%s: failed to set init mode\n", __func__);
			chip->triedsnk = !chip->pdata->try_snk_emulation;
			mutex_unlock(&chip->mlock);
			return;
		}
		fusb301_timer_try_expired(chip);
	} else if (chip->state == FUSB_STATE_TRYWAIT_SRC) {
		fusb301_detach(chip);
	}
	mutex_unlock(&chip->mlock);
}

static void fusb301_work_handler(struct work_struct *work)
{
	struct fusb301_chip *chip =
			container_of(work, struct fusb301_chip, dwork);
	struct i2c_client *client = chip->client;
	struct device *cdev = &client->dev;
	int rc;
	u8 int_sts;

	__pm_stay_awake(chip->wlock);
	mutex_lock(&chip->mlock);

	rc = i2c_smbus_read_byte_data(chip->client, FUSB301_REG_INT);
	if (IS_ERR_VALUE_FUSB301(rc)) {
		dev_err(cdev, "%s: fusb301 failed to read REG_INT\n", __func__);
		goto unlock;
	}
	int_sts = rc & FUSB301_INT_STS_MASK;
	dev_info(cdev, "%s: int_sts[0x%02x]\n", __func__, int_sts);
	if (int_sts & FUSB301_INT_DETACH) {
		fusb301_detach(chip);
	} else {
		if (int_sts & FUSB301_INT_ATTACH)
			fusb301_attach(chip);
		if (int_sts & FUSB301_INT_BCLVL)
			fusb301_bclvl_changed(chip);
		if (int_sts & FUSB301_INT_ACC_CH)
			fusb301_acc_changed(chip);
	}
unlock:
	mutex_unlock(&chip->mlock);
	__pm_relax(chip->wlock);
	enable_irq(client->irq);
}

static irqreturn_t fusb301_irq_handler(int irq, void *data)
{
	struct fusb301_chip *chip = (struct fusb301_chip *)data;
	struct i2c_client *client = chip->client;

	if (!chip)
		return IRQ_HANDLED;

	disable_irq_nosync(client->irq);
	/*
	 * wake_lock_timeout, prevents multiple suspend entries
	 * before charger gets chance to trigger usb core for device
	 */
	__pm_wakeup_event(chip->wlock, jiffies_to_msecs(FUSB301_WAKE_LOCK_TIMEOUT));
	queue_work(chip->cc_wq, &chip->dwork);
	return IRQ_HANDLED;
}

static int fusb_role_sw_get(struct fusb301_chip *chip)
{
	int err;
	struct fwnode_handle *typec;
	struct fwnode_handle *child_node = NULL;

	chip->role_sw = devm_kzalloc(&chip->client->dev, sizeof(chip->role_sw), GFP_KERNEL);

	if (!chip->role_sw)
		return -ENOMEM;

	typec = dev_fwnode(&chip->client->dev);
	if (!typec)
		return -ENODEV;

	child_node = fwnode_get_named_child_node(typec, "connector");

	chip->role_sw = fwnode_usb_role_switch_get(child_node);
	if (IS_ERR_OR_NULL(chip->role_sw)) {
		err = PTR_ERR(chip->role_sw);
		chip->role_sw = NULL;
		if (err == -EPROBE_DEFER) {
			fwnode_handle_put(child_node);
			return err;
		}
		dev_err(&chip->client->dev, "no role switch found\n");
	}

	fwnode_handle_put(child_node);
	return 0;

}

static int fusb301_pm_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct fusb301_chip *chip = i2c_get_clientdata(client);

	if (device_may_wakeup(&client->dev))
		enable_irq_wake(client->irq);
	disable_irq(client->irq);
	fusb301_set_mode(chip, FUSB301_SNK);
	return 0;
}

static int fusb301_pm_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct fusb301_chip *chip = i2c_get_clientdata(client);

	enable_irq(client->irq);
	if (device_may_wakeup(&client->dev))
		disable_irq_wake(client->irq);
	fusb301_set_mode(chip, FUSB301_DRP_ACC);
	return 0;
}

static SIMPLE_DEV_PM_OPS(fusb301_dev_pm_ops,
			fusb301_pm_suspend, fusb301_pm_resume);

#if defined(NV_I2C_DRIVER_STRUCT_PROBE_WITHOUT_I2C_DEVICE_ID_ARG) /* Linux 6.3 */
static int fusb301_probe(struct i2c_client *client)
#else
static int fusb301_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
#endif
{
	struct fusb301_chip *chip = NULL;
	struct device *cdev = &client->dev;
	struct fusb301_data *data;
	int ret, chip_vid;

	if (!i2c_check_functionality(client->adapter,
				I2C_FUNC_SMBUS_BYTE_DATA |
				I2C_FUNC_SMBUS_WORD_DATA)) {
		dev_err(cdev, "smbus data not supported!\n");
		return -EIO;
	}

	chip_vid = fusb301_read_device_id(client);
	if (chip_vid < 0) {
		dev_err(cdev, "fusb301 not support\n");
		return -EINVAL;
	}

	chip = devm_kzalloc(cdev, sizeof(struct fusb301_chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	data = devm_kzalloc(cdev, sizeof(struct fusb301_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	chip->dev_id = chip_vid;
	chip->client = client;

	i2c_set_clientdata(client, chip);

	ret = fusb_role_sw_get(chip);
	if (ret) {
		if (ret != -EPROBE_DEFER)
			dev_err(&chip->client->dev, "fusb_role_sw_get failed - %d\n", ret);
		return ret;
	}

	data->init_mode = FUSB301_DRP_ACC;
	data->dfp_power = FUSB301_HOST_DEFAULT;
	data->dttime = FUSB301_TGL_35MS;
	data->try_snk_emulation = true;
	data->ttry_timeout = FUSB301_TRY_TIMEOUT;
	data->ccdebounce_timeout = FUSB301_CC_DEBOUNCE_TIMEOUT;

	chip->pdata = data;
	chip->type = FUSB301_TYPE_INVALID;
	chip->state = FUSB_STATE_ERROR_RECOVERY;
	chip->bc_lvl = FUSB301_SNK_0MA;
	chip->ufp_power = 0;
	chip->triedsnk = !chip->pdata->try_snk_emulation;
	chip->try_attcnt = 0;

	chip->cc_wq = alloc_ordered_workqueue("fusb301-wq", WQ_HIGHPRI);
	if (!chip->cc_wq)
		return -ENOMEM;

	INIT_WORK(&chip->dwork, fusb301_work_handler);
	INIT_DELAYED_WORK(&chip->twork, fusb301_timer_work_handler);
	chip->wlock = wakeup_source_register(cdev, "fusb301-wake");
	mutex_init(&chip->mlock);

	ret = devm_request_threaded_irq(cdev, client->irq,
			  fusb301_irq_handler, NULL,
			  IRQF_ONESHOT, dev_name(cdev),
			  chip);
	if (ret)
		goto destroy_workqueue;

	ret = sysfs_create_group(&cdev->kobj, &fusb_sysfs_group);
	if (ret) {
		dev_err(cdev, "could not create devices\n");
		goto destroy_workqueue;
	}

	enable_irq_wake(client->irq);

	fusb301_reset_device(chip);

	return 0;

destroy_workqueue:
	destroy_workqueue(chip->cc_wq);
	mutex_destroy(&chip->mlock);
	wakeup_source_unregister(chip->wlock);
	return ret;
}

#if defined(NV_I2C_DRIVER_STRUCT_REMOVE_RETURN_TYPE_INT) /* Linux 6.1 */
static int fusb301_remove(struct i2c_client *client)
#else
static void fusb301_remove(struct i2c_client *client)
#endif
{
	struct fusb301_chip *chip = i2c_get_clientdata(client);
	struct device *cdev = &client->dev;

	if (!chip)
#if defined(NV_I2C_DRIVER_STRUCT_REMOVE_RETURN_TYPE_INT) /* Linux 6.1 */
		return -ENODEV;
#else
		return;
#endif

	sysfs_remove_group(&cdev->kobj, &fusb_sysfs_group);
	destroy_workqueue(chip->cc_wq);
	mutex_destroy(&chip->mlock);
	wakeup_source_unregister(chip->wlock);
#if defined(NV_I2C_DRIVER_STRUCT_REMOVE_RETURN_TYPE_INT) /* Linux 6.1 */
	return 0;
#endif
}

static void fusb301_shutdown(struct i2c_client *client)
{
	struct fusb301_chip *chip = i2c_get_clientdata(client);
	struct device *cdev = &client->dev;

	disable_irq(client->irq);
	if (IS_ERR_VALUE_FUSB301(fusb301_set_mode(chip, FUSB301_SNK)) ||
			IS_ERR_VALUE_FUSB301(fusb301_set_chip_state(chip,
					FUSB_STATE_ERROR_RECOVERY)))
		dev_err(cdev, "%s: failed to set sink mode\n", __func__);
}

static const struct i2c_device_id fusb301_id_table[] = {
	{"fusb301", 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, fusb301_id_table);

static const struct of_device_id fusb301_match_table[] = {
	{ .compatible = "onsemi,fusb301", },
	{ },
};
MODULE_DEVICE_TABLE(of, fusb301_match_table);

static struct i2c_driver fusb301_i2c_driver = {

	.driver = {
		.name = "fusb301",
		.owner = THIS_MODULE,
		.of_match_table = fusb301_match_table,
		.pm = &fusb301_dev_pm_ops,
	},
	.probe = fusb301_probe,
	.remove = fusb301_remove,
	.shutdown = fusb301_shutdown,
	.id_table = fusb301_id_table,
};

module_i2c_driver(fusb301_i2c_driver)

MODULE_AUTHOR("jude84.kim@lge.com");
MODULE_DESCRIPTION("I2C bus driver for fusb301 USB Type-C");
MODULE_LICENSE("GPL v2");
