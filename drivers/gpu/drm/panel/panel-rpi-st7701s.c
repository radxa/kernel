#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_print.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/regulator/consumer.h>
#include <video/mipi_display.h>

#define DRV_NAME		"st7701s_mipi"

struct st7701s_panel_desc {
	const struct drm_display_mode *mode;
	unsigned int lanes;
	unsigned long flags;
	enum mipi_dsi_pixel_format format;
	int (*init_sequence)(struct mipi_dsi_device *dsi);
};

struct st7701s_panel {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	const struct st7701s_panel_desc *desc;
	struct gpio_desc *reset;
};

static int st7701s_dcs_write(struct mipi_dsi_device *dsi,
					const void *seq,
					size_t len)
{
	return mipi_dsi_dcs_write_buffer(dsi, seq, len);
}

#define st7701s_dcs_write_cmd(dsi, seq...) \
	({\
		static const u8 d[] = { seq };\
		st7701s_dcs_write(dsi, d, ARRAY_SIZE(d));\
	})

static int w280bf036i_init_sequence(struct mipi_dsi_device *dsi)
{
	int ret = 0;

	// Command2 BK3 Selection: Enable the BK function of Command2
	ret += st7701s_dcs_write_cmd(dsi, 0xFF, 0x77, 0x01, 0x00, 0x00, 0x13);
	// Unknown
	ret += st7701s_dcs_write_cmd(dsi, 0xEF, 0x08);
	// Command2 BK0 Selection: Disable the BK function of Command2
	ret += st7701s_dcs_write_cmd(dsi, 0xFF, 0x77, 0x01, 0x00, 0x00, 0x10);
	// Display Line Setting
	ret += st7701s_dcs_write_cmd(dsi, 0xC0, 0x4f, 0x00);
	// Porch Control
	ret += st7701s_dcs_write_cmd(dsi, 0xC1, 0x10, 0x0c);
	// Inversion selection & Frame Rate Control
	ret += st7701s_dcs_write_cmd(dsi, 0xC2, 0x07, 0x14);
	// Unknown
	ret += st7701s_dcs_write_cmd(dsi, 0xCC, 0x10);
	// Positive Voltage Gamma Control
	ret += st7701s_dcs_write_cmd(dsi, 0xB0, 0x0a, 0x18, 0x1e, 0x12, 0x16,
					0x0c, 0x0e, 0x0d, 0x0c, 0x29, 0x06,
					0x14, 0x13, 0x29, 0x33, 0x1c);
	// Negative Voltage Gamma Control
	ret += st7701s_dcs_write_cmd(dsi, 0xB1, 0x0a, 0x19, 0x21, 0x0a, 0x0c,
					0x00, 0x0c, 0x03, 0x03, 0x23, 0x01,
					0x0e, 0x0c, 0x27, 0x2b, 0x1c);
	// Command2 BK1 Selection: Enable the BK function of Command2
	ret += st7701s_dcs_write_cmd(dsi, 0xFF, 0x77, 0x01, 0x00, 0x00, 0x11);
	// Vop Amplitude setting
	ret += st7701s_dcs_write_cmd(dsi, 0xB0, 0x5d);
	// VCOM amplitude setting
	ret += st7701s_dcs_write_cmd(dsi, 0xB1, 0x61);
	// VGH Voltage setting
	ret += st7701s_dcs_write_cmd(dsi, 0xB2, 0x84);
	// TEST Command Setting
	ret += st7701s_dcs_write_cmd(dsi, 0xB3, 0x80);
	// VGL Voltage setting
	ret += st7701s_dcs_write_cmd(dsi, 0xB5, 0x4d);
	// Power Control 1
	ret += st7701s_dcs_write_cmd(dsi, 0xB7, 0x85);
	// Power Control 2
	ret += st7701s_dcs_write_cmd(dsi, 0xB8, 0x20);
	// Source pre_drive timing set1
	ret += st7701s_dcs_write_cmd(dsi, 0xC1, 0x78);
	// Source EQ2 Setting
	ret += st7701s_dcs_write_cmd(dsi, 0xC2, 0x78);
	// MIPI Setting 1
	ret += st7701s_dcs_write_cmd(dsi, 0xD0, 0x88);
	// GIP Code
	ret += st7701s_dcs_write_cmd(dsi, 0xE0, 0x00, 0x00, 0x02);
	ret += st7701s_dcs_write_cmd(dsi, 0xE1, 0x06, 0xa0, 0x08, 0xa0, 0x05,
					0xa0, 0x07, 0xa0, 0x00, 0x44, 0x44);
	ret += st7701s_dcs_write_cmd(dsi, 0xE2, 0x20, 0x20, 0x44, 0x44, 0x96,
					0xa0, 0x00, 0x00, 0x96, 0xa0, 0x00,
					0x00);
	ret += st7701s_dcs_write_cmd(dsi, 0xE3, 0x00, 0x00, 0x22, 0x22);
	ret += st7701s_dcs_write_cmd(dsi, 0xE4, 0x44, 0x44);
	ret += st7701s_dcs_write_cmd(dsi, 0xE5, 0x0d, 0x91, 0xa0, 0xa0, 0x0f,
					0x93, 0xa0, 0xa0, 0x09, 0x8d, 0xa0,
					0xa0, 0x0b, 0x8f, 0xa0, 0xa0);
	ret += st7701s_dcs_write_cmd(dsi, 0xE6, 0x00, 0x00, 0x22, 0x22);
	ret += st7701s_dcs_write_cmd(dsi, 0xE7, 0x44, 0x44);
	ret += st7701s_dcs_write_cmd(dsi, 0xE8, 0x0c, 0x90, 0xa0, 0xa0, 0x0e,
					0x92, 0xa0, 0xa0, 0x08, 0x8c, 0xa0,
					0xa0, 0x0a, 0x8e, 0xa0, 0xa0);
	ret += st7701s_dcs_write_cmd(dsi, 0xE9, 0x36, 0x00);
	ret += st7701s_dcs_write_cmd(dsi, 0xEB, 0x00, 0x01, 0xe4, 0xe4, 0x44,
					0x88, 0x40);
	ret += st7701s_dcs_write_cmd(dsi, 0xED, 0xff, 0x45, 0x67, 0xfa, 0x01,
					0x2b, 0xcf, 0xff, 0xff, 0xfc, 0xb2,
					0x10, 0xaf, 0x76, 0x54, 0xff);
	ret += st7701s_dcs_write_cmd(dsi, 0xEF, 0x10, 0x0d, 0x04, 0x08, 0x3f,
					0x1f);
	// disable Command2
	ret += st7701s_dcs_write_cmd(dsi, 0xFF, 0x77, 0x01, 0x00, 0x00, 0x00);
	return ret;
}

static int st7701s_panel_prepare(struct drm_panel *panel)
{
	struct st7701s_panel *data =
		container_of(panel, struct st7701s_panel, panel);

	gpiod_set_value_cansleep(data->reset, 0);
	msleep(30);
	gpiod_set_value_cansleep(data->reset, 1);
	msleep(150);

	mipi_dsi_dcs_soft_reset(data->dsi);

	return 0;
}

static int st7701s_panel_enable(struct drm_panel *panel)
{
	struct st7701s_panel *data =
		container_of(panel, struct st7701s_panel, panel);
	struct device *dev = panel->dev;
	int err = 0;

	msleep(30);
	err = data->desc->init_sequence(data->dsi);
	if (err < 0) {
		dev_err(dev, "%s: init_sequence failed\n", __func__);
		return err;
	}

	mipi_dsi_dcs_set_tear_on(data->dsi, MIPI_DSI_DCS_TEAR_MODE_VBLANK);
	mipi_dsi_dcs_exit_sleep_mode(data->dsi);
	msleep(120);
	err =  mipi_dsi_dcs_set_display_on(data->dsi);
	if (err < 0) {
		dev_err(dev,
			"mipi_dsi_dcs_set_display_on failed, ret = %d\n",
			err);
		return err;
	}

	return 0;
}

static int st7701s_panel_disable(struct drm_panel *panel)
{
	struct device *dev = panel->dev;
	int ret = 0;
	struct st7701s_panel *data =
		container_of(panel, struct st7701s_panel, panel);

	if (data == NULL) {
		dev_err(dev, "%s: get struct data failed\n", __func__);
		return -EINVAL;
	}
	ret = mipi_dsi_dcs_set_display_off(data->dsi);
	if (ret < 0) {
		dev_err(dev, "%s: mipi_dsi_dcs_set_display_off failed\n",
			__func__);
		return -EINVAL;
	}
	ret = mipi_dsi_dcs_enter_sleep_mode(data->dsi);
	if (ret < 0) {
		dev_err(dev, "%s: mipi_dsi_dcs_enter_sleep_mode failed\n",
			__func__);
		return -EINVAL;
	}

	return 0;
}

static int st7701s_panel_unprepare(struct drm_panel *panel)
{
	struct st7701s_panel *data =
		container_of(panel, struct st7701s_panel, panel);

	mipi_dsi_dcs_enter_sleep_mode(data->dsi);

	gpiod_set_value_cansleep(data->reset, 0);

	return 0;
}

static int dsi_panel_get_modes(struct drm_panel *panel,
				struct drm_connector *connector)
{
	struct st7701s_panel *data =
		container_of(panel, struct st7701s_panel, panel);
	const struct drm_display_mode *desc_mode = data->desc->mode;
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, desc_mode);
	if (!mode) {
		dev_err(&data->dsi->dev,
			"failed to add mode %ux%u@%u\n",
			desc_mode->hdisplay, desc_mode->vdisplay,
			drm_mode_vrefresh(desc_mode));
		return -ENOMEM;
	}

	drm_mode_set_name(mode);
	drm_mode_probed_add(connector, mode);
	connector->display_info.width_mm = desc_mode->width_mm;
	connector->display_info.height_mm = desc_mode->height_mm;
	if (data->desc->format)
		drm_display_info_set_bus_formats(&connector->display_info,
						 &data->desc->format, 1);
	return 1;
}

static const struct drm_panel_funcs st7701s_funcs = {
	.prepare = st7701s_panel_prepare,
	.enable = st7701s_panel_enable,
	.disable = st7701s_panel_disable,
	.unprepare = st7701s_panel_unprepare,
	.get_modes = dsi_panel_get_modes,
};

static const struct drm_display_mode w280bf036i_mode = {
	.clock = 22572,
	.hdisplay = 480,
	.hsync_start = 480 + /* HFP */ 30,
	.hsync_end = 480 + 30 + /* HSync */ 10,
	.htotal = 480 + 30 + 10 + /* HBP */ 30,
	.vdisplay = 640,
	.vsync_start = 640 + /* VFP */ 20,
	.vsync_end = 640 + 20 + /* VSync */ 4,
	.vtotal = 640 + 20 + 4 + /* VBP */ 20,
	.width_mm = 43,
	.height_mm = 57,
	.type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
};

static const struct st7701s_panel_desc w280bf036i_desc = {
	.mode = &w280bf036i_mode,
	.lanes = 1,
	.flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST
		| MIPI_DSI_MODE_LPM,
	.format = MIPI_DSI_FMT_RGB888,
	.init_sequence = w280bf036i_init_sequence
};

static const struct of_device_id st7701s_of_match[] = {
	{
	.compatible = "st7701s,480x640",
	.data = &w280bf036i_desc
	},
	{}
};

MODULE_DEVICE_TABLE(of, st7701s_of_match);

static int st7701s_probe(struct mipi_dsi_device *dsi)
{
	struct st7701s_panel *data =
				devm_kzalloc(&dsi->dev,
				sizeof(*data), GFP_KERNEL);
	const struct st7701s_panel_desc *desc =
			of_device_get_match_data(&dsi->dev);
	int ret = 0;

	if (!data)
		return -ENOMEM;

	data->dsi = dsi;
	data->desc = desc;
	dsi->mode_flags = desc->flags;
	dsi->format = desc->format;
	dsi->lanes = desc->lanes;

	data->reset = devm_gpiod_get(&dsi->dev,
					"reset", GPIOD_OUT_LOW);
	if (IS_ERR(data->reset)) {
		dev_err(&dsi->dev,
			"%s-->[%d]: get reset failed\n",
			__func__, __LINE__);
		return PTR_ERR(data->reset);
	}
	drm_panel_init(&data->panel, &dsi->dev,
			&st7701s_funcs, DRM_MODE_CONNECTOR_DSI);


	ret = drm_panel_of_backlight(&data->panel);
	if (ret)
		return ret;

	drm_panel_add(&data->panel);

	mipi_dsi_set_drvdata(dsi, data);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		drm_panel_remove(&data->panel);
		return ret;
	}

	dev_dbg(&dsi->dev, "%s: successfully.\n", __func__);
	return 0;
}

static int st7701s_remove(struct mipi_dsi_device *dsi)
{
	struct st7701s_panel *data = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(dsi);
	drm_panel_remove(&data->panel);
	return 0;
}

static struct mipi_dsi_driver st7701s_driver = {
	.probe = st7701s_probe,
	.remove = st7701s_remove,
	.driver = {
		.name = DRV_NAME,
		.of_match_table = st7701s_of_match,
	},
};

module_mipi_dsi_driver(st7701s_driver);

MODULE_AUTHOR("CNflysky");
MODULE_AUTHOR("sora1874");
MODULE_DESCRIPTION("Mipi Driver for st7701s lcd");
MODULE_LICENSE("GPL");
