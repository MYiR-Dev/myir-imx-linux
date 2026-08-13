// SPDX-License-Identifier: GPL-2.0
/*
 * MYIR MIPI101C MIPI-DSI panel driver
 *
 * Copyright 2024 MYIR
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/media-bus-format.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>
#include <video/of_videomode.h>
#include <video/videomode.h>

#include <drm/drm_crtc.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

#define COL_FMT_16BPP 0x55
#define COL_FMT_18BPP 0x66
#define COL_FMT_24BPP 0x77

struct cmd_set_entry {
	u8 cmd;
	u8 param;
};

static const struct cmd_set_entry mcs_mipi101c[] = {
	{0xB0, 0x5A}, {0xB1, 0x00}, {0x89, 0x01}, {0x2C, 0x28}, {0x00, 0xF1},
	{0x11, 0x00}, {0x29, 0x00},
};

static const u32 rad_bus_formats[] = {
	MEDIA_BUS_FMT_RGB888_1X24,
	MEDIA_BUS_FMT_RGB666_1X18,
	MEDIA_BUS_FMT_RGB565_1X16,
};

static const u32 rad_bus_flags = DRM_BUS_FLAG_DE_LOW |
				 DRM_BUS_FLAG_PIXDATA_SAMPLE_POSEDGE;

struct rad_panel {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;

	struct backlight_device *backlight;

	struct regulator_bulk_data *supplies;
	unsigned int num_supplies;

	bool prepared;
	bool enabled;

	/* Optional display mode parsed from a DT "panel-timing" node; lets us
	 * tune pixel clock / porches / resolution by rebuilding only the dtb
	 * instead of the kernel.  Falls back to default_mode when absent. */
	struct drm_display_mode dt_mode;
	bool dt_mode_valid;

	const struct rad_platform_data *pdata;
};

struct rad_platform_data {
	int (*enable)(struct rad_panel *panel);
	const struct drm_display_mode *default_mode;
	bool parse_dt_mode;
	bool use_dt_backlight;
	bool stop_on_dcs_error;
	bool recover_disable_state;
};

static const struct drm_display_mode mipi101c_default_mode = {
	.clock          = 165200,
	.hdisplay       = 1200,
	.hsync_start    = 1200 + 255,
	.hsync_end      = 1200 + 255 + 1,
	.htotal         = 1200 + 255 + 1 + 4,
	.vdisplay       = 1920,
	.vsync_start    = 1920 + 3,
	.vsync_end      = 1920 + 3 + 1,
	.vtotal         = 1920 + 3 + 1 + 5,
	.width_mm = 135,
	.height_mm = 216,
	.flags = DRM_MODE_FLAG_NHSYNC |
		 DRM_MODE_FLAG_NVSYNC,
};

static const struct drm_display_mode mipi101c_imx8mp_default_mode = {
	/* Stable fallback when the JS8MP DT does not provide panel-timing. */
	.clock          = 173250,
	.hdisplay       = 1200,
	.hsync_start    = 1200 + 255,
	.hsync_end      = 1200 + 255 + 1,
	.htotal         = 1200 + 255 + 1 + 4,
	.vdisplay       = 1920,
	.vsync_start    = 1920 + 3,
	.vsync_end      = 1920 + 3 + 1,
	.vtotal         = 1920 + 3 + 1 + 5,
	.width_mm = 135,
	.height_mm = 216,
	.flags = DRM_MODE_FLAG_NHSYNC |
		 DRM_MODE_FLAG_NVSYNC,
};

static inline struct rad_panel *to_rad_panel(struct drm_panel *panel)
{
	return container_of(panel, struct rad_panel, panel);
}

static int rad_panel_push_cmd_list(struct mipi_dsi_device *dsi,
				   struct cmd_set_entry const *cmd_set,
				   size_t count)
{
	size_t i;
	int ret = 0;

	for (i = 0; i < count; i++) {
		const struct cmd_set_entry *entry = cmd_set++;
		u8 buffer[2] = { entry->cmd, entry->param };

		usleep_range(50, 100);
		ret = mipi_dsi_generic_write(dsi, &buffer, sizeof(buffer));
		if (ret < 0)
			return ret;
	}

	return ret;
};

static int color_format_from_dsi_format(enum mipi_dsi_pixel_format format)
{
	switch (format) {
	case MIPI_DSI_FMT_RGB565:
		return COL_FMT_16BPP;
	case MIPI_DSI_FMT_RGB666:
	case MIPI_DSI_FMT_RGB666_PACKED:
		return COL_FMT_18BPP;
	case MIPI_DSI_FMT_RGB888:
		return COL_FMT_24BPP;
	default:
		return COL_FMT_24BPP;
	}
};

static int rad_panel_prepare(struct drm_panel *panel)
{
	struct rad_panel *rad = to_rad_panel(panel);
	int ret;

	if (rad->prepared)
		return 0;

	ret = regulator_bulk_enable(rad->num_supplies, rad->supplies);
	if (ret)
		return ret;

	usleep_range(10000, 12000);

	rad->prepared = true;

	return 0;
}

static int rad_panel_unprepare(struct drm_panel *panel)
{
	struct rad_panel *rad = to_rad_panel(panel);
	int ret;

	if (!rad->prepared)
		return 0;

	ret = regulator_bulk_disable(rad->num_supplies, rad->supplies);
	if (ret)
		return ret;

	rad->prepared = false;

	return 0;
}

static int mipi101c_enable(struct rad_panel *panel)
{
	struct mipi_dsi_device *dsi = panel->dsi;
	struct device *dev = &dsi->dev;
	int color_format = color_format_from_dsi_format(dsi->format);
	int ret;

	if (panel->enabled)
		return 0;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	ret = rad_panel_push_cmd_list(dsi,
				      &mcs_mipi101c[0],
				      ARRAY_SIZE(mcs_mipi101c));
	if (ret < 0) {
		dev_err(dev, "Failed to send MCS (%d)\n", ret);
	}

	usleep_range(15000, 17000);

	ret = mipi_dsi_dcs_set_tear_on(dsi, MIPI_DSI_DCS_TEAR_MODE_VBLANK);
	if (ret < 0) {
		/*
		 * On a dead link (no panel attached) the very first DCS write
		 * times out. Bail immediately instead of pushing the rest of the
		 * init sequence: each further command would stall on the DSI host
		 * for ~0.1-2s, delaying boot enough that the external SGM820B
		 * watchdog is not fed in time and resets the board.
		 */
		dev_err(dev, "Failed to set tear ON (%d)\n", ret);
		if (panel->pdata->stop_on_dcs_error)
			return ret;
	}

	ret = mipi_dsi_dcs_set_tear_scanline(dsi, 0x00);
	if (ret < 0) {
		dev_err(dev, "Failed to set tear scanline (%d)\n", ret);
		if (panel->pdata->stop_on_dcs_error)
			return ret;
	}

	usleep_range(50, 100);
	ret = mipi_dsi_dcs_set_pixel_format(dsi, color_format);
	dev_dbg(dev, "Interface color format set to 0x%x\n", color_format);
	if (ret < 0) {
		dev_err(dev, "Failed to set pixel format (%d)\n", ret);
		if (panel->pdata->stop_on_dcs_error)
			return ret;
	}

	ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to exit sleep mode (%d)\n", ret);
		if (panel->pdata->stop_on_dcs_error)
			return ret;
	}

	usleep_range(5000, 7000);

	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to set display ON (%d)\n", ret);
		if (panel->pdata->stop_on_dcs_error)
			return ret;
	}

	backlight_enable(panel->backlight);

	panel->enabled = true;

	return 0;
}

static int rad_panel_enable(struct drm_panel *panel)
{
	struct rad_panel *rad = to_rad_panel(panel);

	return rad->pdata->enable(rad);
}

static int rad_panel_disable(struct drm_panel *panel)
{
	struct rad_panel *rad = to_rad_panel(panel);
	struct mipi_dsi_device *dsi = rad->dsi;
	struct device *dev = &dsi->dev;
	int ret, err = 0;

	if (!rad->enabled)
		return 0;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	backlight_disable(rad->backlight);

	usleep_range(10000, 12000);

	ret = mipi_dsi_dcs_set_display_off(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to set display OFF (%d)\n", ret);
		if (!rad->pdata->recover_disable_state)
			return ret;
		err = ret;
	}

	usleep_range(5000, 10000);

	ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to enter sleep mode (%d)\n", ret);
		if (!rad->pdata->recover_disable_state)
			return ret;
		if (!err)
			err = ret;
	}

	rad->enabled = false;

	return err;
}

static int rad_panel_get_modes(struct drm_panel *panel,
			       struct drm_connector *connector)
{
	struct rad_panel *rad = to_rad_panel(panel);
	const struct drm_display_mode *src = rad->dt_mode_valid ?
					     &rad->dt_mode :
					     rad->pdata->default_mode;
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, src);
	if (!mode) {
		dev_err(panel->dev, "failed to add mode %ux%u@%u\n",
			src->hdisplay, src->vdisplay,
			drm_mode_vrefresh(src));
		return -ENOMEM;
	}

	drm_mode_set_name(mode);
	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode);

	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;
	connector->display_info.bus_flags = rad_bus_flags;

	drm_display_info_set_bus_formats(&connector->display_info,
					 rad_bus_formats,
					 ARRAY_SIZE(rad_bus_formats));
	return 1;
}

static int rad_bl_get_brightness(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	struct rad_panel *rad = mipi_dsi_get_drvdata(dsi);
	u16 brightness;
	int ret;

	if (!rad->prepared)
		return 0;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_get_display_brightness(dsi, &brightness);
	if (ret < 0)
		return ret;

	bl->props.brightness = brightness;

	return brightness & 0xff;
}

static int rad_bl_update_status(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	struct rad_panel *rad = mipi_dsi_get_drvdata(dsi);
	int ret = 0;

	if (!rad->prepared)
		return 0;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_set_display_brightness(dsi, bl->props.brightness);
	if (ret < 0)
		return ret;

	return 0;
}

static const struct backlight_ops rad_bl_ops = {
	.update_status = rad_bl_update_status,
	.get_brightness = rad_bl_get_brightness,
};

static const struct drm_panel_funcs rad_panel_funcs = {
	.prepare = rad_panel_prepare,
	.unprepare = rad_panel_unprepare,
	.enable = rad_panel_enable,
	.disable = rad_panel_disable,
	.get_modes = rad_panel_get_modes,
};

static const char * const rad_supply_names[] = {
	"v3p3",
	"v1p8",
};

static int rad_init_regulators(struct rad_panel *rad)
{
	struct device *dev = &rad->dsi->dev;
	int i;

	rad->num_supplies = ARRAY_SIZE(rad_supply_names);
	rad->supplies = devm_kcalloc(dev, rad->num_supplies,
				     sizeof(*rad->supplies), GFP_KERNEL);
	if (!rad->supplies)
		return -ENOMEM;

	for (i = 0; i < rad->num_supplies; i++)
		rad->supplies[i].supply = rad_supply_names[i];

	return devm_regulator_bulk_get(dev, rad->num_supplies, rad->supplies);
};

static const struct rad_platform_data rad_mipi101c = {
	.enable = &mipi101c_enable,
	.default_mode = &mipi101c_default_mode,
};

static const struct rad_platform_data rad_mipi101c_imx8mp = {
	.enable = &mipi101c_enable,
	.default_mode = &mipi101c_imx8mp_default_mode,
	.parse_dt_mode = true,
	.use_dt_backlight = true,
	.stop_on_dcs_error = true,
	.recover_disable_state = true,
};

static const struct of_device_id rad_of_match[] = {
	{ .compatible = "myir,mipi101c", .data = &rad_mipi101c },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, rad_of_match);

static const struct of_device_id rad_imx8mp_of_match[] = {
	{ .compatible = "myir,mipi101c-imx8mp",
	  .data = &rad_mipi101c_imx8mp },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, rad_imx8mp_of_match);

static int rad_panel_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct rad_platform_data *pdata = of_device_get_match_data(dev);
	struct device_node *np = dev->of_node;
	struct rad_panel *panel;
	struct backlight_properties bl_props;
	int ret;
	u32 video_mode;

	if (!pdata)
		return -ENODEV;

	panel = devm_kzalloc(&dsi->dev, sizeof(*panel), GFP_KERNEL);
	if (!panel)
		return -ENOMEM;

	mipi_dsi_set_drvdata(dsi, panel);

	panel->dsi = dsi;
	panel->pdata = pdata;

	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags =  MIPI_DSI_MODE_VIDEO_HSE | MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_NO_EOT_PACKET;

	ret = of_property_read_u32(np, "video-mode", &video_mode);
	if (!ret) {
		switch (video_mode) {
		case 0:
			/* burst mode */
			dsi->mode_flags |= MIPI_DSI_MODE_VIDEO_BURST;
			break;
		case 1:
			/* non-burst mode with sync event */
			break;
		case 2:
			/* non-burst mode with sync pulse */
			dsi->mode_flags |= MIPI_DSI_MODE_VIDEO_SYNC_PULSE;
			break;
		default:
			dev_warn(dev, "invalid video mode %d\n", video_mode);
			break;
		}
	}

	ret = of_property_read_u32(np, "dsi-lanes", &dsi->lanes);
	if (ret) {
		dev_err(dev, "Failed to get dsi-lanes property (%d)\n", ret);
		return ret;
	}

	/* Optional: override the hard-coded default_mode with a DT panel-timing
	 * node, so pixel clock / porches / resolution can be tuned via the dtb
	 * alone (no kernel rebuild).  Absent -> keep default_mode. */
	if (pdata->parse_dt_mode &&
	    !of_get_drm_panel_display_mode(np, &panel->dt_mode, NULL)) {
		panel->dt_mode_valid = true;
		dev_info(dev, "using DT panel-timing: " DRM_MODE_FMT "\n",
			 DRM_MODE_ARG(&panel->dt_mode));
	}

	if (!pdata->use_dt_backlight) {
		memset(&bl_props, 0, sizeof(bl_props));
		bl_props.type = BACKLIGHT_RAW;
		bl_props.brightness = 255;
		bl_props.max_brightness = 255;

		panel->backlight = devm_backlight_device_register(dev, dev_name(dev),
								  dev, dsi, &rad_bl_ops,
								  &bl_props);
		if (IS_ERR(panel->backlight)) {
			ret = PTR_ERR(panel->backlight);
			dev_err(dev, "Failed to register backlight (%d)\n", ret);
			return ret;
		}
	}

	ret = rad_init_regulators(panel);
	if (ret)
		return ret;

	drm_panel_init(&panel->panel, dev, &rad_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);

	if (pdata->use_dt_backlight) {
		ret = drm_panel_of_backlight(&panel->panel);
		if (ret)
			return ret;
		panel->backlight = panel->panel.backlight;
		if (!panel->backlight) {
			memset(&bl_props, 0, sizeof(bl_props));
			bl_props.type = BACKLIGHT_RAW;
			bl_props.brightness = 255;
			bl_props.max_brightness = 255;

			panel->backlight = devm_backlight_device_register(dev,
								  dev_name(dev), dev, dsi,
								  &rad_bl_ops, &bl_props);
			if (IS_ERR(panel->backlight))
				return PTR_ERR(panel->backlight);
			panel->panel.backlight = panel->backlight;
		}
	}
	dev_set_drvdata(dev, panel);

	drm_panel_add(&panel->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret)
		drm_panel_remove(&panel->panel);

	return ret;
}

static void rad_panel_remove(struct mipi_dsi_device *dsi)
{
	struct rad_panel *rad = mipi_dsi_get_drvdata(dsi);
	struct device *dev = &dsi->dev;
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret)
		dev_err(dev, "Failed to detach from host (%d)\n", ret);

	drm_panel_remove(&rad->panel);
}

static void rad_panel_shutdown(struct mipi_dsi_device *dsi)
{
	struct rad_panel *rad = mipi_dsi_get_drvdata(dsi);

	rad_panel_disable(&rad->panel);
	rad_panel_unprepare(&rad->panel);
}

static struct mipi_dsi_driver rad_panel_driver = {
	.driver = {
		.name = "panel-myir-mipi101c",
		.of_match_table = rad_of_match,
	},
	.probe = rad_panel_probe,
	.remove = rad_panel_remove,
	.shutdown = rad_panel_shutdown,
};

static struct mipi_dsi_driver rad_panel_imx8mp_driver = {
	.driver = {
		.name = "panel-myir-mipi101c-imx8mp",
		.of_match_table = rad_imx8mp_of_match,
	},
	.probe = rad_panel_probe,
	.remove = rad_panel_remove,
	.shutdown = rad_panel_shutdown,
};

#ifndef MODULE
static int __init rad_panel_imx8mp_driver_init(void)
{
	return mipi_dsi_driver_register(&rad_panel_imx8mp_driver);
}
subsys_initcall(rad_panel_imx8mp_driver_init);

/* Keep the legacy i.MX95-compatible driver at the source initcall level. */
module_mipi_dsi_driver(rad_panel_driver);
#else
static int __init rad_panel_drivers_init(void)
{
	int ret;

	ret = mipi_dsi_driver_register(&rad_panel_imx8mp_driver);
	if (ret)
		return ret;

	ret = mipi_dsi_driver_register(&rad_panel_driver);
	if (ret)
		mipi_dsi_driver_unregister(&rad_panel_imx8mp_driver);

	return ret;
}
module_init(rad_panel_drivers_init);

static void __exit rad_panel_drivers_exit(void)
{
	mipi_dsi_driver_unregister(&rad_panel_driver);
	mipi_dsi_driver_unregister(&rad_panel_imx8mp_driver);
}
module_exit(rad_panel_drivers_exit);
#endif

MODULE_DESCRIPTION("DRM Driver for MYIR MIPI101C MIPI DSI panel");
MODULE_LICENSE("GPL v2");
