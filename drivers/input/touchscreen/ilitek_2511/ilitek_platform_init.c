/*
 * ILITEK Touch IC driver
 *
 * Copyright (C) 2011 ILI Technology Corporation.
 *
 * Author: Luca Hsu <luca_hsu@ilitek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301 USA.
 *
 */

#include "ilitek_ts.h"
#include "ilitek_common.h"

#if ILITEK_PLAT == ILITEK_PLAT_MTK

#define TPD_OK (0)

#ifdef MTK_UNDTS
static int tpd_keys_local[TPD_KEY_COUNT] = TPD_KEYS;
static int tpd_keys_dim_local[TPD_KEY_COUNT][4] = TPD_KEYS_DIM;
struct touch_vitual_key_map_t touch_key_point_maping_array[] = { {key_1}, {key_2}, {key_3}, {key_4} };

static struct i2c_board_info __initdata ilitek_i2c_tpd = {
	I2C_BOARD_INFO(ILITEK_TS_NAME, 0x41)
};
#endif

/* probe function is used for matching and initializing input device */
static int /*__devinit*/ tpd_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int ret = 0;

	TP_MSG(NULL, "TPD probe\n");

	if (!client) {
		TP_ERR(NULL, "i2c client is NULL\n");
		return -1;
	}

	ret = ilitek_main_probe(client, &client->dev);
	if (ret == 0)	// If probe is success, then enable the below flag.
		tpd_load_status = 1;

	TP_MSG(NULL, "TPD probe done\n");
	return TPD_OK;
}

static int tpd_detect(struct i2c_client *client, struct i2c_board_info *info)
{
	strcpy(info->type, TPD_DEVICE);
	return TPD_OK;
}

static int /*__devexit*/ tpd_remove(struct i2c_client *client)
{
	TP_MSG(NULL, "TPD removed\n");
	return ilitek_main_remove(client);
}

/* The I2C device list is used for matching I2C device and I2C device driver. */
static const struct i2c_device_id tpd_device_id[] = {
	{ILITEK_TS_NAME, 0},
	{},			/* should not omitted */
};

MODULE_DEVICE_TABLE(i2c, tpd_device_id);

const struct of_device_id touch_dt_match_table[] = {
	{ .compatible = "mediatek,cap_touch",},
	{},
};

MODULE_DEVICE_TABLE(of, touch_dt_match_table);

static struct i2c_driver tpd_i2c_driver = {

	.driver = {
		.name = ILITEK_TS_NAME,
		.of_match_table = of_match_ptr(touch_dt_match_table),
	},
	.probe = tpd_probe,
	.remove = tpd_remove,
	.id_table = tpd_device_id,
	.detect = tpd_detect,
};

static int tpd_local_init(void)
{
	TP_MSG(NULL, "TPD init device driver\n");

	if (i2c_add_driver(&tpd_i2c_driver) != 0) {
		TP_ERR(NULL, "Unable to add i2c driver.\n");
		return -1;
	}
	if (tpd_load_status == 0) {
		TP_ERR(NULL, "Add error touch panel driver.\n");
		i2c_del_driver(&tpd_i2c_driver);
		return -1;
	}
#ifndef MTK_UNDTS
	if (tpd_dts_data.use_tpd_button)
		tpd_button_setting(tpd_dts_data.tpd_key_num, tpd_dts_data.tpd_key_local, tpd_dts_data.tpd_key_dim_local);

#else
	tpd_button_setting(TPD_KEY_COUNT, tpd_keys_local, tpd_keys_dim_local);	// initialize tpd button data
#endif
	tpd_type_cap = 1;
	TP_MSG(NULL, "TPD init done\n");
	return TPD_OK;
}

#ifdef MTK_UNDTS
static void tpd_resume(struct early_suspend *h) {
#else
static void tpd_resume(struct device *h) {
#endif
	TP_MSG(NULL, "TPD wake up\n");
	ilitek_resume();
	TP_MSG(NULL, "TPD wake up done\n");
}

#ifdef MTK_UNDTS
static void tpd_suspend(struct early_suspend *h) {
#else
static void tpd_suspend(struct device *h) {
#endif
	TP_MSG(NULL, "TPD enter sleep\n");
	ilitek_suspend();
	TP_MSG(NULL, "TPD enter sleep done\n");
}

static struct tpd_driver_t tpd_device_driver = {
	.tpd_device_name = ILITEK_TS_NAME,
	.tpd_local_init = tpd_local_init,
	.suspend = tpd_suspend,
	.resume = tpd_resume,
	.tpd_have_button = 1,
};

static int __init ilitek_touch_driver_init(void)
{
	TP_MSG(NULL, "touch panel driver init\n");

#ifdef MTK_UNDTS
	i2c_register_board_info(2, &ilitek_i2c_tpd, 1);
#else
	tpd_get_dts_info();
#endif
	if (tpd_driver_add(&tpd_device_driver) < 0)
		TP_ERR(NULL, "TPD add TP driver failed\n");

	return 0;

}

static void __exit ilitek_touch_driver_exit(void)
{
	TP_MSG(NULL, "touch panel driver exit\n");
	tpd_driver_remove(&tpd_device_driver);
}

#else

#ifdef ILITEK_SPI_INTERFACE
static int ilitek_touch_driver_probe(struct spi_device *spi)
{
	int error;

	spi->bits_per_word = 8;
	spi->mode = SPI_MODE_0;
	error = spi_setup(spi);
	if (error < 0) {
		TP_ERR(NULL, "SPI setup failed, err: %d\n", error);
		return error;
	}

	TP_MSG(NULL, "SPI start probe, max_speed_hz: %d\n", spi->max_speed_hz);

	return ilitek_main_probe(spi, &spi->dev);
}

static int ilitek_touch_driver_remove(struct spi_device *spi)
{
	return ilitek_main_remove(spi);
}
#else
static int ilitek_touch_driver_probe(struct i2c_client *client,
				     const struct i2c_device_id *id)
{
	if (!client) {
		TP_ERR(NULL, "i2c client is NULL\n");
		return -ENODEV;
	}

	TP_MSG(NULL, "ILITEK client->addr: 0x%x, client->irq: %d\n",
		client->addr, client->irq);

	return ilitek_main_probe(client, &client->dev);
}

static int ilitek_touch_driver_remove(struct i2c_client *client)
{
	return ilitek_main_remove(client);
}
#endif

#ifdef CONFIG_OF
static struct of_device_id ilitek_touch_match_table[] = {
	{.compatible = "tchip,ilitek",},
	{},
};
#endif

static const struct i2c_device_id ilitek_touch_device_id[] = {
	{ILITEK_TS_NAME, 0},
	{},			/* should not omitted */
};
MODULE_DEVICE_TABLE(i2c, ilitek_touch_device_id);

#ifdef CONFIG_ACPI
static const struct acpi_device_id ilitekts_acpi_id[] = {
	{"ILIT2901", 0},
	{}
};
MODULE_DEVICE_TABLE(acpi, ilitekts_acpi_id);
#endif


#ifndef ILITEK_REGISTER_SUSPEND_RESUME
static int __maybe_unused ilitek_ts_suspend(struct device *dev)
{
	ilitek_suspend();
	return 0;
}

static int __maybe_unused ilitek_ts_resume(struct device *dev)
{
	ilitek_resume();
	return 0;
}

static SIMPLE_DEV_PM_OPS(ilitek_pm_ops, ilitek_ts_suspend, ilitek_ts_resume);
#endif

#ifdef ILITEK_SPI_INTERFACE
static struct spi_driver ilitek_touch_device_driver = {
#else
static struct i2c_driver ilitek_touch_device_driver = {
#endif
	.driver = {
		.name = ILITEK_TS_NAME,
		.owner = THIS_MODULE,
#ifdef CONFIG_OF
		.of_match_table = ilitek_touch_match_table,
#endif
#ifdef CONFIG_ACPI
		.acpi_match_table = ACPI_PTR(ilitekts_acpi_id),
#endif

#ifndef ILITEK_REGISTER_SUSPEND_RESUME
		.pm = &ilitek_pm_ops,
#endif
	},

	.probe = ilitek_touch_driver_probe,
	.remove = ilitek_touch_driver_remove,

#if !defined(ILITEK_SPI_INTERFACE)
	.id_table = ilitek_touch_device_id,
#endif
};

#if ILITEK_PLAT == ILITEK_PLAT_ALLWIN
static const unsigned short normal_i2c[2] = { 0x41, I2C_CLIENT_END };
struct ctp_config_info config_info = {
	.input_type = CTP_TYPE,
	.name = NULL,
	.int_number = 0,
};

static int twi_id;
static int screen_max_x;
static int screen_max_y;
static int revert_x_flag;
static int revert_y_flag;
static int exchange_x_y_flag;
static int ctp_get_system_config(void)
{
	twi_id = config_info.twi_id;
	screen_max_x = config_info.screen_max_x;
	screen_max_y = config_info.screen_max_y;
	TP_MSG(NULL, "Ilitek: screen_max_x = %d\n", screen_max_x);
	revert_x_flag = config_info.revert_x_flag;
	revert_y_flag = config_info.revert_y_flag;
	exchange_x_y_flag = config_info.exchange_x_y_flag;
	if ((screen_max_x == 0) || (screen_max_y == 0)) {
		TP_ERR(NULL, "%s:read config error!\n", __func__);
		return -1;
	}
	return 0;
}

int ctp_ts_detect(struct i2c_client *client, struct i2c_board_info *info)
{
	struct i2c_adapter *adapter = client->adapter;

	if (twi_id == adapter->nr) {
		strlcpy(info->type, ILITEK_TS_NAME, I2C_NAME_SIZE);
		return 0;
	} else {
		return -ENODEV;
	}
}

static struct i2c_board_info i2c_info_dev = {
	I2C_BOARD_INFO(ILITEK_TS_NAME, 0x41),
	.platform_data = NULL,
};

static int add_ctp_device(void)
{
	struct i2c_adapter *adap;
	//script_parser_fetch("ctp_para", "ctp_twi_id", &twi_id, 1);
	adap = i2c_get_adapter(twi_id);
	i2c_new_device(adap, &i2c_info_dev);
	return 0;
}

static int ilitek_init_allwin(void)
{

	int ret = 0;

	if (input_fetch_sysconfig_para(&(config_info.input_type))) {
		TP_ERR(NULL, "Ilitek:  ctp_fetch_sysconfig_para err.\n");
		return -1;
	} else {
		ret = input_init_platform_resource(&(config_info.input_type));
		if (0 != ret)
			TP_ERR(NULL, "Ilitek: ctp_ops.init_platform_resource err.\n");
	}

	if (config_info.ctp_used == 0) {
		TP_ERR(NULL, "Ilitek: *** if use ctp,please put the sys_config.fex ctp_used set to 1.\n");
		return -1;
	}

	if (ctp_get_system_config() < 0)
		TP_ERR(NULL, "Ilitek: %s:read config fail!\n", __func__);

	add_ctp_device();
	ilitek_touch_device_driver.address_list = normal_i2c;
	ilitek_touch_device_driver.detect = ctp_ts_detect;
	return 0;

}

#endif
static int __init ilitek_touch_driver_init(void)
{
#if ILITEK_PLAT == ILITEK_PLAT_ALLWIN
	if (ilitek_init_allwin() < 0) {
		TP_ERR(NULL, "ilitek_init_allwin failed.\n");
		return -ENODEV;
	}
#endif

	TP_MSG(NULL, "add ILITEK touch device driver\n");

#ifdef ILITEK_SPI_INTERFACE
	return spi_register_driver(&ilitek_touch_device_driver);
#else
	return i2c_add_driver(&ilitek_touch_device_driver);
#endif
}

static void __exit ilitek_touch_driver_exit(void)
{
	TP_MSG(NULL, "remove touch device driver i2c driver.\n");

#ifdef ILITEK_SPI_INTERFACE
	spi_unregister_driver(&ilitek_touch_device_driver);
#else
	i2c_del_driver(&ilitek_touch_device_driver);
#endif
}
#endif

module_init(ilitek_touch_driver_init);
module_exit(ilitek_touch_driver_exit);
MODULE_AUTHOR("ILITEK");
MODULE_LICENSE("GPL");
