// SPDX-License-Identifier: GPL-2.0
/*
 * i.MX drm driver - Raydium MIPI-DSI panel driver
 *
 * Copyright (C) 2017 NXP
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>
#include <video/mipi_display.h>
#include <video/of_videomode.h>
#include <video/videomode.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/regmap.h>


/* Write Manufacture Command Set Control */
#define WRMAUCCTR 0xFE

#define UPDATE(x, h, l) (((x) << (l)) & GENMASK((h), (l)))

/* DSI PHY Layer Registers */
#define D0W_DPHYCONTTX 0x0004
#define CLW_DPHYCONTRX 0x0020
#define D0W_DPHYCONTRX 0x0024
#define D1W_DPHYCONTRX 0x0028
#define D2W_DPHYCONTRX 0x002C
#define D3W_DPHYCONTRX 0x0030
#define COM_DPHYCONTRX 0x0038
#define CLW_CNTRL 0x0040
#define D0W_CNTRL 0x0044
#define D1W_CNTRL 0x0048
#define D2W_CNTRL 0x004C
#define D3W_CNTRL 0x0050
#define DFTMODE_CNTRL 0x0054
/* DSI PPI Layer Registers */
#define PPI_STARTPPI 0x0104
#define STARTPPI BIT(0)
#define PPI_BUSYPPI 0x0108
#define PPI_LINEINITCNT 0x0110
#define PPI_LPTXTIMECNT 0x0114
#define LPTXCNT(x) UPDATE(x, 10, 0)
#define PPI_LANEENABLE 0x0134
#define L3EN BIT(4)
#define L2EN BIT(3)
#define L1EN BIT(2)
#define L0EN BIT(1)
#define CLEN BIT(0)
#define PPI_TX_RX_TA 0x013C
#define TXTAGOCNT(x) UPDATE(x, 26, 16)
#define TXTASURECNT(x) UPDATE(x, 10, 0)
#define PPI_CLS_ATMR 0x0140
#define PPI_D0S_ATMR 0x0144
#define PPI_D1S_ATMR 0x0148
#define PPI_D2S_ATMR 0x014C
#define PPI_D3S_ATMR 0x0150
#define PPI_D0S_CLRSIPOCOUNT 0x0164
#define D0S_CLRSIPOCOUNT(x) UPDATE(x, 5, 0)
#define PPI_D1S_CLRSIPOCOUNT 0x0168
#define D1S_CLRSIPOCOUNT(x) UPDATE(x, 5, 0)
#define PPI_D2S_CLRSIPOCOUNT 0x016C
#define D2S_CLRSIPOCOUNT(x) UPDATE(x, 5, 0)
#define PPI_D3S_CLRSIPOCOUNT 0x0170
#define D3S_CLRSIPOCOUNT(x) UPDATE(x, 5, 0)
#define CLS_PRE 0x0180
#define D0S_PRE 0x0184
#define D1S_PRE 0x0188
#define D2S_PRE 0x018C
#define D3S_PRE 0x0190
#define CLS_PREP 0x01A0
#define D0S_PREP 0x01A4
#define D1S_PREP 0x01A8
#define D2S_PREP 0x01AC
#define D3S_PREP 0x01B0
#define CLS_ZERO 0x01C0
#define D0S_ZERO 0x01C4
#define D1S_ZERO 0x01C8
#define D2S_ZERO 0x01CC
#define D3S_ZERO 0x01D0
#define PPI_CLRFLG 0x01E0
#define PPI_CLRSIPO 0x01E4
#define HSTIMEOUT 0x01F0
#define HSTIMEOUTENABLE 0x01F4
/* DSI Protocol Layer Registers */
#define DSI_STARTDSI 0x0204
#define STARTDSI BIT(0)
#define DSI_LANEENABLE 0x0210
#define DSI_LANESTATUS0 0x0214
#define DSI_LANESTATUS1 0x0218
#define DSI_INTSTATUS 0x0220
#define DSI_INTMASK 0x0224
#define DSI_INTCLR 0x0228
#define DSI_LPTXTO 0x0230
/* DSI General Registers */
#define DSIERRCNT 0x0300
/* DSI Application Layer Registers */
#define APLCTRL 0x0400
#define RDPKTLN 0x0404
/* Video Path Configuration Registers */
#define VPCTRL 0x0450
#define VSDELAY(x) UPDATE(x, 29, 20)
#define VSPOL_ACTIVE_HIGH BIT(19)
#define VSPOL_ACTIVE_LOW 0
#define DEPOL_ACTIVE_HIGH BIT(18)
#define DEPOL_ACTIVE_LOW 0
#define HSPOL_ACTIVE_HIGH BIT(17)
#define HSPOL_ACTIVE_LOW 0
#define OPXLFMT_MASK BIT(8)
#define OPXLFMT_RGB888 BIT(8)
#define OPXLFMT_RGB666 0
#define HTIM1 0x0454
#define HBPR(x) UPDATE(x, 24, 16)
#define HPW(x) UPDATE(x, 8, 0)
#define HTIM2 0x0458
#define HFPR(x) UPDATE(x, 24, 16)
#define HACT(x) UPDATE(x, 10, 0)
#define VTIM1 0x045C
#define VBPR(x) UPDATE(x, 23, 16)
#define VPW(x) UPDATE(x, 7, 0)
#define VTIM2 0x0460
#define VFPR(x) UPDATE(x, 23, 16)
#define VACT(x) UPDATE(x, 10, 0)
#define VFUEN 0x0464
#define UPLOAD_ENABLE BIT(0)
#define LVMX0003 0x0480
#define LVMX0407 0x0484
#define LVMX0811 0x0488
#define LVMX1215 0x048C
#define LVMX1619 0x0490
#define LVMX2023 0x0494
#define LVMX2427 0x0498
/* LVDS Configuration Registers */
#define LVCFG 0x049C
#define PCLKSEL(x) UPDATE(x, 11, 10)
#define PCLKDIV(x) UPDATE(x, 7, 4)
#define LVDILINK_DUAL_LINK BIT(1)
#define LVDILINK_SINGLE_LINK 0
#define LVEN_ENABLE BIT(0)
#define LVDU_ENABLE(x) UPDATE(x, 1, 1)
#define LVEN_DISABLE 0
#define LVPHY0 0x04A0
#define LV_RST_MASK BIT(22)
#define LV_RST_RESET BIT(22)
#define LV_RST_NORMAL 0
#define LV_PRBS_ON(x) UPDATE(x, 20, 16)
#define LV_IS_MASK GENMASK(15, 14)
#define LV_IS(x) UPDATE(x, 15, 14)
#define LV_FS_MASK GENMASK(6, 5)
#define LV_FS(x) UPDATE(x, 6, 5)
#define LV_ND_MASK GENMASK(4, 0)
#define LV_ND(x) UPDATE(x, 4, 0)
#define LVPHY1 0x04A4
/* System Registers */
#define SYSSTAT 0x0500
#define SYSRST 0x0504
#define RSTLCD BIT(2)
/* GPIO Registers */
#define GPIOC 0x0520
#define GPIOO 0x0524
#define GPIOI 0x0528
/* I2C Registers */
#define I2CTIMCTRL 0x0540
#define I2CMEN_ENABLE BIT(24)
#define I2CMEN_DISABLE 0
#define I2CMADDR 0x0544
#define WDATAQ 0x0548
#define RDATAQ 0x054A
/* Chip ID/Revision Registers */
#define IDREG 0x0580
/* Debug Registers */
#define DEBUG00 0x05A0
#define DEBUG01 0x05A4
#define DEBUG02 0x05A8
#define TC358775_MAX_REGISTER DEBUG02

/* Input muxing for registers LVMX0003...LVMX2427 */
enum
{
	INPUT_R0,
	INPUT_R1,
	INPUT_R2,
	INPUT_R3,
	INPUT_R4,
	INPUT_R5,
	INPUT_R6,
	INPUT_R7,
	INPUT_G0,
	INPUT_G1,
	INPUT_G2,
	INPUT_G3,
	INPUT_G4,
	INPUT_G5,
	INPUT_G6,
	INPUT_G7,
	INPUT_B0,
	INPUT_B1,
	INPUT_B2,
	INPUT_B3,
	INPUT_B4,
	INPUT_B5,
	INPUT_B6,
	INPUT_B7,
	INPUT_HSYNC,
	INPUT_VSYNC,
	INPUT_DE,
	LOGIC_0,
};

typedef struct tc358775_configure
{
	u32 ppi_tx_rx_ta;
	u32 ppi_lptxtimecnt;
	u32 ppi_d0s_clrsipocount;
	u32 ppi_d1s_clrsipocount;
	u32 ppi_d2s_clrsipocount;
	u32 ppi_d3s_clrsipocount;
	u32 ppi_laneenable;
	u32 dsi_laneenable;
	u32 ppi_sartppi;
	u32 dsi_sartppi;

	u32 vpctrl;
	u32 htim1;
	u32 htim2;
	u32 vtim1;
	u32 vtim2;
	u32 vfuen;
	u32 lvphy0;
	u32 lvphy0_1;
	u32 sysrst;

	u32 lvmx0003;
	u32 lvmx0407;
	u32 lvmx0811;
	u32 lvmx1215;
	u32 lvmx1619;
	u32 lvmx2023;
	u32 lvmx2427;

	u32 lvcfg;
} tc358775_configure_st;

#define INPUT_MUX(lvmx03, lvmx02, lvmx01, lvmx00)      \
	(UPDATE(lvmx03, 29, 24) | UPDATE(lvmx02, 20, 16) | \
	 UPDATE(lvmx01, 12, 8) | UPDATE(lvmx00, 4, 0))

#if 1
static const struct display_timing tc358775_default_timing = {
	.pixelclock = {68000000, 70000000, 80000000},
	.hactive = {1280, 1280, 1280},
	.hfront_porch = {60, 60, 60},
	.hsync_len = {17, 17, 17},
	.hback_porch = {60, 60, 60},
	.vactive = {800, 800, 800},
	.vfront_porch = {10, 10, 10},
	.vsync_len = {3, 3, 3},
	.vback_porch = {10, 10, 10},

	.flags = DISPLAY_FLAGS_HSYNC_LOW |
			 DISPLAY_FLAGS_VSYNC_LOW |
			 DISPLAY_FLAGS_DE_LOW |
			 DISPLAY_FLAGS_PIXDATA_NEGEDGE,
};

#else
static const struct display_timing tc358775_default_timing = {
	//.pixelclock = { 66000000, 120000000, 132000000 },
	.pixelclock = {66000000, 160000000, 182000000},
	//.pixelclock = { 80000000, 148500000, 158500000 },
	.hactive = {1920, 1920, 1920},
	.hsync_len = {10, 10, 10},
	.hback_porch = {30, 30, 30},
	.hfront_porch = {230, 230, 230},

	.vactive = {1080, 1080, 1080},
	.vsync_len = {10, 10, 10},
	.vback_porch = {96, 96, 96},
	.vfront_porch = {50, 50, 50},

	.flags = DISPLAY_FLAGS_HSYNC_LOW |
			 DISPLAY_FLAGS_VSYNC_LOW |
			 DISPLAY_FLAGS_DE_LOW |
			 DISPLAY_FLAGS_PIXDATA_NEGEDGE,
};

#endif

static const u32 tc358775_bus_formats[] = {
	MEDIA_BUS_FMT_RGB888_1X24,
	MEDIA_BUS_FMT_RGB666_1X18,
	MEDIA_BUS_FMT_RGB565_1X16,
};

struct tc358775_panel
{
	struct drm_panel base;
	struct mipi_dsi_device *dsi;

	struct gpio_desc *stby;
	struct gpio_desc *reset;
	struct gpio_desc *power_gpio;
	struct backlight_device *backlight;

	bool prepared;
	bool enabled;

	struct videomode vm;
	u32 width_mm;
	u32 height_mm;
	struct device *dev;
	struct i2c_client *client;
	struct regmap *regmap;
	tc358775_configure_st tc35775_cfg;
};

static bool tc358775_volatile_reg(struct device *dev, unsigned int reg);
static bool tc358775_readable_reg(struct device *dev, unsigned int reg);
static bool tc358775_writeable_reg(struct device *dev, unsigned int reg);
// static void tc358775_configure(struct tc358775_panel *tc);
static int tc358775_configure(struct tc358775_panel *tc, struct drm_panel *panel);

struct regmap_config tc358775_regmap_config = {
	.name = "tc358775",
	.reg_bits = 16,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = TC358775_MAX_REGISTER,
	.writeable_reg = tc358775_writeable_reg,
	.readable_reg = tc358775_readable_reg,
	.volatile_reg = tc358775_volatile_reg,
	.cache_type = REGCACHE_RBTREE,
	.reg_format_endian = REGMAP_ENDIAN_BIG,
	.val_format_endian = REGMAP_ENDIAN_LITTLE,
};

static inline struct tc358775_panel *to_tc358775_panel(struct drm_panel *panel)
{
	return container_of(panel, struct tc358775_panel, base);
}

static bool tc358775_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg)
	{
	case PPI_BUSYPPI:
	case DSI_LANESTATUS0:
	case DSI_LANESTATUS1:
	case DSI_INTSTATUS:
	case SYSSTAT:
		return true;
	default:
		return false;
	}
}

static bool tc358775_readable_reg(struct device *dev, unsigned int reg)
{
	switch (reg)
	{
	case DSI_INTCLR:
	case SYSRST:
	case WDATAQ:
	case RDATAQ:
		return false;
	default:
		return true;
	}
}

static bool tc358775_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg)
	{
	case DSI_LPTXTO:
	case GPIOI:
	case IDREG:
		return false;
	default:
		return true;
	}
}

static int color_format_from_dsi_format(enum mipi_dsi_pixel_format format)
{
	switch (format) {
	case MIPI_DSI_FMT_RGB565:
		return 0x55;
	case MIPI_DSI_FMT_RGB666:
	case MIPI_DSI_FMT_RGB666_PACKED:
		return 0x66;
	case MIPI_DSI_FMT_RGB888:
		return 0x77;
	default:
		return 0x77; /* for backward compatibility */
	}
};

static int tc358775_configure(struct tc358775_panel *tc, struct drm_panel *panel)
{
//	struct tc358775_panel *tc358775 = to_tc358775_panel(panel);
	struct mipi_dsi_device *dsi = tc->dsi;
	u32 idreg;
	//	u32 idreg, pclksel = 0, pclkdiv = 0;
	//	u32 lvis = 1, lvfs = 0, lvnd = 6, vsdelay = 5, lv_prbs_on = 4,dual_link = 0,debug = 0;
	u32 debug = 0;
	u32 set_color_key = 1;
	//u32 ppi_tx_rx_ta,ppi_lptxtimecnt,ppi_d0s_clrsipocount,ppi_d1s_clrsipocount,ppi_d2s_clrsipocount,ppi_d3s_clrsipocount;
	u8 chipid, revid;
	int ret=0;

	/* make sure in LP mode */
	ret = mipi_dsi_dcs_nop(dsi);
	if (ret)
	{
		return ret;
	}

	/* Disable I2C Master function */
	regmap_write(tc->regmap, I2CTIMCTRL, I2CMEN_DISABLE);

	/* Chip ID and Revision */
	regmap_read(tc->regmap, IDREG, &idreg);
	chipid = (idreg >> 8) & 0xff;
	revid = idreg & 0xff;
	printk("Chip ID: %02x, Revision ID: %02x\n", chipid, revid);

	if(chipid == 0x80)
		ret=-1;

	memset(&tc->tc35775_cfg, 0, sizeof(tc358775_configure_st));
	of_property_read_u32(tc->dev->of_node, "toshiba,ppi_tx_rx_ta", &tc->tc35775_cfg.ppi_tx_rx_ta);
	of_property_read_u32(tc->dev->of_node, "toshiba,ppi_lptxtimecnt", &tc->tc35775_cfg.ppi_lptxtimecnt);
	of_property_read_u32(tc->dev->of_node, "toshiba,ppi_d0s_clrsipocount", &tc->tc35775_cfg.ppi_d0s_clrsipocount);
	of_property_read_u32(tc->dev->of_node, "toshiba,ppi_d1s_clrsipocount", &tc->tc35775_cfg.ppi_d1s_clrsipocount);
	of_property_read_u32(tc->dev->of_node, "toshiba,ppi_d2s_clrsipocount", &tc->tc35775_cfg.ppi_d2s_clrsipocount);
	of_property_read_u32(tc->dev->of_node, "toshiba,ppi_d3s_clrsipocount", &tc->tc35775_cfg.ppi_d3s_clrsipocount);
	of_property_read_u32(tc->dev->of_node, "toshiba,ppi_laneenable", &tc->tc35775_cfg.ppi_laneenable);
	of_property_read_u32(tc->dev->of_node, "toshiba,dsi_laneenable", &tc->tc35775_cfg.dsi_laneenable);
	of_property_read_u32(tc->dev->of_node, "toshiba,ppi_sartppi", &tc->tc35775_cfg.ppi_sartppi);
	of_property_read_u32(tc->dev->of_node, "toshiba,dsi_sartppi", &tc->tc35775_cfg.dsi_sartppi);

	of_property_read_u32(tc->dev->of_node, "toshiba,vpctrl", &tc->tc35775_cfg.vpctrl);
	of_property_read_u32(tc->dev->of_node, "toshiba,htim1", &tc->tc35775_cfg.htim1);
	of_property_read_u32(tc->dev->of_node, "toshiba,htim2", &tc->tc35775_cfg.htim2);
	of_property_read_u32(tc->dev->of_node, "toshiba,vtim1", &tc->tc35775_cfg.vtim1);
	of_property_read_u32(tc->dev->of_node, "toshiba,vtim2", &tc->tc35775_cfg.vtim2);
	of_property_read_u32(tc->dev->of_node, "toshiba,vfuen", &tc->tc35775_cfg.vfuen);
	of_property_read_u32(tc->dev->of_node, "toshiba,lvphy0", &tc->tc35775_cfg.lvphy0);
	of_property_read_u32(tc->dev->of_node, "toshiba,lvphy0_1", &tc->tc35775_cfg.lvphy0_1);
	of_property_read_u32(tc->dev->of_node, "toshiba,sysrst", &tc->tc35775_cfg.sysrst);

	of_property_read_u32(tc->dev->of_node, "toshiba,lvmx0003", &tc->tc35775_cfg.lvmx0003);
	of_property_read_u32(tc->dev->of_node, "toshiba,lvmx0407", &tc->tc35775_cfg.lvmx0407);
	of_property_read_u32(tc->dev->of_node, "toshiba,lvmx0811", &tc->tc35775_cfg.lvmx0811);
	of_property_read_u32(tc->dev->of_node, "toshiba,lvmx1215", &tc->tc35775_cfg.lvmx1215);
	of_property_read_u32(tc->dev->of_node, "toshiba,lvmx1619", &tc->tc35775_cfg.lvmx1619);
	of_property_read_u32(tc->dev->of_node, "toshiba,lvmx2023", &tc->tc35775_cfg.lvmx2023);
	of_property_read_u32(tc->dev->of_node, "toshiba,lvmx2427", &tc->tc35775_cfg.lvmx2427);

	of_property_read_u32(tc->dev->of_node, "toshiba,lvcfg", &tc->tc35775_cfg.lvcfg);

	/* tc3587755 base parameter */
	regmap_write(tc->regmap, PPI_TX_RX_TA, tc->tc35775_cfg.ppi_tx_rx_ta);
	regmap_write(tc->regmap, PPI_LPTXTIMECNT, tc->tc35775_cfg.ppi_lptxtimecnt);
	regmap_write(tc->regmap, PPI_D0S_CLRSIPOCOUNT, tc->tc35775_cfg.ppi_d0s_clrsipocount);
	regmap_write(tc->regmap, PPI_D1S_CLRSIPOCOUNT, tc->tc35775_cfg.ppi_d1s_clrsipocount);
	regmap_write(tc->regmap, PPI_D2S_CLRSIPOCOUNT, tc->tc35775_cfg.ppi_d2s_clrsipocount);
	regmap_write(tc->regmap, PPI_D3S_CLRSIPOCOUNT, tc->tc35775_cfg.ppi_d3s_clrsipocount);
	regmap_write(tc->regmap, PPI_LANEENABLE, tc->tc35775_cfg.ppi_laneenable);
	regmap_write(tc->regmap, DSI_LANEENABLE, tc->tc35775_cfg.dsi_laneenable);
	regmap_write(tc->regmap, PPI_STARTPPI, tc->tc35775_cfg.ppi_sartppi);
	regmap_write(tc->regmap, DSI_STARTDSI, tc->tc35775_cfg.dsi_sartppi);

	/* tc358775 time and mode setting */
	regmap_write(tc->regmap, VPCTRL, tc->tc35775_cfg.vpctrl);
#if 0
	regmap_write(tc->regmap, HTIM1, HBPR(tc->vm.hback_porch) | HPW(tc->vm.hsync_len));
	regmap_write(tc->regmap, HTIM2, HFPR(tc->vm.hfront_porch) | HACT(tc->vm.hactive));
	regmap_write(tc->regmap, VTIM1, VBPR(tc->vm.vback_porch) | VPW(tc->vm.vsync_len));
	regmap_write(tc->regmap, VTIM2, VFPR(tc->vm.vfront_porch) | VACT(tc->vm.vactive));
#else
	regmap_write(tc->regmap, HTIM1, tc->tc35775_cfg.htim1);
	regmap_write(tc->regmap, HTIM2, tc->tc35775_cfg.htim2);
	regmap_write(tc->regmap, VTIM1, tc->tc35775_cfg.vtim1);
	regmap_write(tc->regmap, VTIM2, tc->tc35775_cfg.vtim2);

#endif
	regmap_write(tc->regmap, VFUEN, tc->tc35775_cfg.vfuen);
	regmap_write(tc->regmap, LVPHY0, tc->tc35775_cfg.lvphy0);
	usleep_range(100, 1000);
	regmap_write(tc->regmap, LVPHY0, tc->tc35775_cfg.lvphy0_1);

	/* Software reset for LCD controller */
	regmap_write(tc->regmap, SYSRST, tc->tc35775_cfg.sysrst);

	/* LVDS-TX Mux Input Select Control */
	of_property_read_u32(tc->dev->of_node, "toshiba,set_color_key", &set_color_key);
	if (set_color_key)
	{
		regmap_write(tc->regmap, LVMX0003, tc->tc35775_cfg.lvmx0003);
		regmap_write(tc->regmap, LVMX0407, tc->tc35775_cfg.lvmx0407);
		regmap_write(tc->regmap, LVMX0811, tc->tc35775_cfg.lvmx0811);
		regmap_write(tc->regmap, LVMX1215, tc->tc35775_cfg.lvmx1215);
		regmap_write(tc->regmap, LVMX1619, tc->tc35775_cfg.lvmx1619);
		regmap_write(tc->regmap, LVMX2023, tc->tc35775_cfg.lvmx2023);
		regmap_write(tc->regmap, LVMX2427, tc->tc35775_cfg.lvmx2427);
	}
	regmap_write(tc->regmap, LVCFG, tc->tc35775_cfg.lvcfg);
#if 0
	/* tc3587755 base parameter */
	/* 10.1 */
	regmap_write(tc->regmap, 0x013c, 0x00030005);
	regmap_write(tc->regmap, 0x0114, 0x00000003);
	regmap_write(tc->regmap, 0x0164, 0x00000004);
	regmap_write(tc->regmap, 0x0168, 0x00000004);
	regmap_write(tc->regmap, 0x016c, 0x00000004);
	regmap_write(tc->regmap, 0x0170, 0x00000004);
	regmap_write(tc->regmap, 0x0134, 0x0000001f);
	regmap_write(tc->regmap, 0x0210, 0x0000001f);
	regmap_write(tc->regmap, 0x0104, 0x00000001);
	regmap_write(tc->regmap, 0x0204, 0x00000001);


	/* tc358775 time and mode setting */
	regmap_write(tc->regmap, 0x0450, 0x03F00100);
	regmap_write(tc->regmap, 0x0454, 0x0050000A);
	regmap_write(tc->regmap, 0x0458, 0x00460500);
	regmap_write(tc->regmap, 0x045c, 0x000A0003);
	regmap_write(tc->regmap, 0x0460, 0x000A0320);
	regmap_write(tc->regmap, 0x0464, 0x00000001);
	regmap_write(tc->regmap, 0x04a0, 0x00448006);
	usleep_range(100,1000);
	regmap_write(tc->regmap, 0x04a0, 0x00048006);
	regmap_write(tc->regmap, 0x0504, 0x00000004);


	/* tc358775 lvds color mapping setting */
	regmap_write(tc->regmap, 0x0480, 0x03020100);
	regmap_write(tc->regmap, 0x0484, 0x08050704);
	regmap_write(tc->regmap, 0x0488, 0x0f0e0a09);
	regmap_write(tc->regmap, 0x048c, 0x100d0c0b);
	regmap_write(tc->regmap, 0x0490, 0x12111716);
	regmap_write(tc->regmap, 0x0494, 0x1b151413);
	regmap_write(tc->regmap, 0x0498, 0x061a1918);

	/* tc358775 lvds enabel */
	regmap_write(tc->regmap, 0x049c, 0x00000031);
#endif
	of_property_read_u32(tc->dev->of_node, "toshiba,debug", &debug);
	if (debug)
	{
		printk("[%x] [%x]\n", PPI_TX_RX_TA, tc->tc35775_cfg.ppi_tx_rx_ta);
		printk("[%x] [%x]\n", PPI_LPTXTIMECNT, tc->tc35775_cfg.ppi_lptxtimecnt);
		printk("[%x] [%x]\n", PPI_D0S_CLRSIPOCOUNT, tc->tc35775_cfg.ppi_d0s_clrsipocount);
		printk("[%x] [%x]\n", PPI_D1S_CLRSIPOCOUNT, tc->tc35775_cfg.ppi_d1s_clrsipocount);
		printk("[%x] [%x]\n", PPI_D2S_CLRSIPOCOUNT, tc->tc35775_cfg.ppi_d2s_clrsipocount);
		printk("[%x] [%x]\n", PPI_D3S_CLRSIPOCOUNT, tc->tc35775_cfg.ppi_d3s_clrsipocount);
		printk("[%x] [%x]\n", PPI_LANEENABLE, tc->tc35775_cfg.ppi_laneenable);
		printk("[%x] [%x]\n", DSI_LANEENABLE, tc->tc35775_cfg.dsi_laneenable);
		printk("[%x] [%x]\n", PPI_STARTPPI, tc->tc35775_cfg.ppi_sartppi);
		printk("[%x] [%x]\n", DSI_STARTDSI, tc->tc35775_cfg.dsi_sartppi);

		printk("[%x] [%x]\n", VPCTRL, tc->tc35775_cfg.vpctrl);
		printk("[%x] [%x]\n", HTIM1, tc->tc35775_cfg.htim1);
		printk("[%x] [%x]\n", HTIM2, tc->tc35775_cfg.htim2);
		printk("[%x] [%x]\n", VTIM1, tc->tc35775_cfg.vtim1);
		printk("[%x] [%x]\n", VTIM2, tc->tc35775_cfg.vtim2);
		printk("[%x] [%x]\n", VFUEN, tc->tc35775_cfg.vfuen);
		printk("[%x] [%x]\n", LVPHY0, tc->tc35775_cfg.lvphy0);
		printk("[%x] [%x]\n", LVPHY0, tc->tc35775_cfg.lvphy0_1);
		printk("[%x] [%x]\n", LVCFG, tc->tc35775_cfg.lvcfg);
	}

	return ret;
}
static int tc358775_panel_prepare(struct drm_panel *panel)
{
	struct tc358775_panel *tc358775 = to_tc358775_panel(panel);

	if (tc358775->prepared)
		return 0;
	printk("[%s]\n",__FUNCTION__);
	#if 0
	if (tc358775->reset != NULL && tc358775->stby !=NULL /* && tc358775->power_gpio !=NULL */) {
		printk("duxy test %s %d\n",__FUNCTION__,__LINE__);
		/* Power-On  Sequence */
		/*
		 gpiod_set_value(tc358775->power_gpio, 0);
		 */
		 gpiod_set_value(tc358775->reset, 0);
		 gpiod_set_value(tc358775->stby, 0);
		 msleep(5);
		 /*
		 gpiod_set_value(tc358775->power_gpio, 1);
		 msleep(10);
		 */
		 gpiod_set_value(tc358775->stby, 1);
		 msleep(10);
		 gpiod_set_value(tc358775->reset, 1);
		 

	}
	#endif
	tc358775->prepared = true;

	return 0;
}

static int tc358775_panel_unprepare(struct drm_panel *panel)
{
	
	struct tc358775_panel *tc358775 = to_tc358775_panel(panel);
	struct mipi_dsi_device *dsi = tc358775->dsi;
	//	struct mipi_dsi_device *dsi = tc->dsi;
	struct device *dev = &dsi->dev;
	int ret = 0;
	//	int ret = 0,idreg=0,cnt=0;
	//	u8 chipid, revid;

	if (!tc358775->prepared)
		return 0;

	DRM_DEV_DEBUG_DRIVER(dev, "\n");
#if 0

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_set_display_off(dsi);
	if (ret < 0)
		DRM_DEV_ERROR(dev, "Failed to set display OFF (%d)\n", ret);

	usleep_range(5000, 10000);

	ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (ret < 0)
		DRM_DEV_ERROR(dev, "Failed to enter sleep mode (%d)\n", ret);

	usleep_range(10000, 15000);

	if (tc358775->reset != NULL)
	{
		gpiod_set_value(tc358775->reset, 1);
		usleep_range(10000, 15000);
	}
#endif

	if (tc358775->reset != NULL && tc358775->stby !=NULL && tc358775->power_gpio !=NULL) {
		/* Power-Off Sequence */
		
		gpiod_set_value(tc358775->reset, 0);
		msleep(2);
		gpiod_set_value(tc358775->stby, 0);
		msleep(3);
		gpiod_set_value(tc358775->power_gpio, 0);
		msleep(2);
		
	}

	tc358775->prepared = false;

	return 0;
}

static int tc358775_panel_enable(struct drm_panel *panel)
{
	struct tc358775_panel *tc358775 = to_tc358775_panel(panel);
	struct mipi_dsi_device *dsi = tc358775->dsi;
	struct device *dev = &dsi->dev;
	int color_format = color_format_from_dsi_format(dsi->format);
	u16 brightness;
	int ret;

	if (tc358775->enabled)
		return 0;

	if (!tc358775->prepared) {
		DRM_DEV_ERROR(dev, "Panel not prepared!\n");
		return -EPERM;
	}
	printk("[%s]\n",__FUNCTION__);

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	ret=tc358775_configure(tc358775, panel);
	if (ret < 0) {
		DRM_DEV_ERROR(dev, "Failed to send MCS (%d)\n", ret);
		goto fail;
	}


	/* Software reset */
		ret = mipi_dsi_dcs_soft_reset(dsi);
		if (ret < 0) {
			DRM_DEV_ERROR(dev, "Failed to do Software Reset (%d)\n", ret);
			goto fail;
		}


	/* Set DSI mode */
	ret = mipi_dsi_generic_write(dsi, (u8[]){ 0xC2, 0x0B }, 2);
	if (ret < 0) {
		DRM_DEV_ERROR(dev, "Failed to set DSI mode (%d)\n", ret);
		goto fail;
	}
	/* Set tear ON */
	ret = mipi_dsi_dcs_set_tear_on(dsi, MIPI_DSI_DCS_TEAR_MODE_VBLANK);
	if (ret < 0) {
		DRM_DEV_ERROR(dev, "Failed to set tear ON (%d)\n", ret);
		goto fail;
	}
	/* Set tear scanline */
	ret = mipi_dsi_dcs_set_tear_scanline(dsi, 0x380);
	if (ret < 0) {
		DRM_DEV_ERROR(dev, "Failed to set tear scanline (%d)\n", ret);
		goto fail;
	}
	/* Set pixel format */
	ret = mipi_dsi_dcs_set_pixel_format(dsi, color_format);
	DRM_DEV_DEBUG_DRIVER(dev, "Interface color format set to 0x%x\n",
				color_format);
	if (ret < 0) {
		DRM_DEV_ERROR(dev, "Failed to set pixel format (%d)\n", ret);
		goto fail;
	}
	/* Set display brightness */
	ret = mipi_dsi_dcs_set_display_brightness(dsi, 0x20);
	if (ret < 0) {
		DRM_DEV_ERROR(dev, "Failed to set display brightness (%d)\n",
			      ret);
		goto fail;
	}
	/* Exit sleep mode */
	ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (ret < 0) {
		DRM_DEV_ERROR(dev, "Failed to exit sleep mode (%d)\n", ret);
		goto fail;
	}

	usleep_range(5000, 10000);

	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret < 0) {
		DRM_DEV_ERROR(dev, "Failed to set display ON (%d)\n", ret);
		goto fail;
	}

	DRM_DEV_DEBUG_DRIVER(dev, "\n");
	tc358775->backlight->props.power = FB_BLANK_UNBLANK;
	backlight_update_status(tc358775->backlight);

	tc358775->enabled = true;

	return 0;

fail:
	if (tc358775->reset != NULL)
		gpiod_set_value(tc358775->reset, 0);

	return ret;
}

static int tc358775_panel_disable(struct drm_panel *panel)
{
	struct tc358775_panel *tc358775 = to_tc358775_panel(panel);
	struct mipi_dsi_device *dsi = tc358775->dsi;
	struct device *dev = &dsi->dev;
	int ret;

	if (!tc358775->enabled)
		return 0;

	DRM_DEV_DEBUG_DRIVER(dev, "\n");
	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	tc358775->backlight->props.power = FB_BLANK_POWERDOWN;
	backlight_update_status(tc358775->backlight);


	ret = mipi_dsi_dcs_set_display_off(dsi);
	if (ret < 0) {
		DRM_DEV_ERROR(dev, "Failed to set display OFF (%d)\n", ret);
		return ret;
	}
	usleep_range(5000, 10000);

	ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (ret < 0) {
		DRM_DEV_ERROR(dev, "Failed to enter sleep mode (%d)\n", ret);
		return ret;
	}

	tc358775->enabled = false;

	return 0;
}

static int tc358775_panel_get_modes(struct drm_panel *panel)
{
	struct tc358775_panel *tc358775 = to_tc358775_panel(panel);
	struct device *dev = &tc358775->dsi->dev;
	struct drm_connector *connector = panel->connector;
	struct drm_display_mode *mode;
	u32 bus_format = MEDIA_BUS_FMT_RGB888_1X24;
	u32 *bus_flags = &connector->display_info.bus_flags;
	int ret;

	mode = drm_mode_create(connector->dev);
	if (!mode) {
		DRM_DEV_ERROR(dev, "Failed to create display mode!\n");
		return 0;
	}

	drm_display_mode_from_videomode(&tc358775->vm, mode);
	mode->width_mm = tc358775->width_mm;
	mode->height_mm = tc358775->height_mm;
	connector->display_info.width_mm = tc358775->width_mm;
	connector->display_info.height_mm = tc358775->height_mm;
	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;

	if (tc358775->vm.flags & DISPLAY_FLAGS_DE_HIGH)
		*bus_flags |= DRM_BUS_FLAG_DE_HIGH;
	if (tc358775->vm.flags & DISPLAY_FLAGS_DE_LOW)
		*bus_flags |= DRM_BUS_FLAG_DE_LOW;
	if (tc358775->vm.flags & DISPLAY_FLAGS_PIXDATA_NEGEDGE)
		*bus_flags |= DRM_BUS_FLAG_PIXDATA_NEGEDGE;
	if (tc358775->vm.flags & DISPLAY_FLAGS_PIXDATA_POSEDGE)
		*bus_flags |= DRM_BUS_FLAG_PIXDATA_POSEDGE;

	ret = drm_display_info_set_bus_formats(&connector->display_info,
										   &bus_format, 1);
	if (ret)
		return ret;

	drm_mode_probed_add(panel->connector, mode);

	return 1;
}

static int tc358775_bl_get_brightness(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	struct tc358775_panel *tc358775 = mipi_dsi_get_drvdata(dsi);
	struct device *dev = &dsi->dev;
	u16 brightness;
	int ret;

	if (!tc358775->prepared)
		return 0;

	DRM_DEV_DEBUG_DRIVER(dev, "\n");

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_get_display_brightness(dsi, &brightness);
	if (ret < 0)
		return ret;

	bl->props.brightness = brightness;

	return brightness & 0xff;
}

static int tc358775_bl_update_status(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	struct tc358775_panel *tc358775 = mipi_dsi_get_drvdata(dsi);
	struct device *dev = &dsi->dev;
	int ret = 0;

	if (!tc358775->prepared)
		return 0;

	DRM_DEV_DEBUG_DRIVER(dev, "New brightness: %d\n", bl->props.brightness);

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_set_display_brightness(dsi, bl->props.brightness);
	if (ret < 0)
		return ret;

	return 0;
}

static const struct backlight_ops tc358775_bl_ops = {
	.update_status = tc358775_bl_update_status,
	.get_brightness = tc358775_bl_get_brightness,
};

static const struct drm_panel_funcs tc358775_panel_funcs = {
	.prepare = tc358775_panel_prepare,
	.unprepare = tc358775_panel_unprepare,
	.enable = tc358775_panel_enable,
	.disable = tc358775_panel_disable,
	.get_modes = tc358775_panel_get_modes,
};



static int tc358775_panel_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct device_node *np = dev->of_node;
	struct device_node *timings;
	struct device_node *client_node = NULL;
	struct i2c_client *client_dev;
	struct tc358775_panel *panel;
	struct device_node *backlight;
	int ret;
	u32 video_mode;

	panel = devm_kzalloc(&dsi->dev, sizeof(*panel), GFP_KERNEL);
	if (!panel)
		return -ENOMEM;

	mipi_dsi_set_drvdata(dsi, panel);

	panel->dsi = dsi;
	panel->dev = dev;

	dsi->format = MIPI_DSI_FMT_RGB888;
//	dsi->mode_flags =  MIPI_DSI_MODE_VIDEO_HSE | MIPI_DSI_MODE_VIDEO |
//			   MIPI_DSI_CLOCK_NON_CONTINUOUS;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO_HSE | MIPI_DSI_MODE_VIDEO ;

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
	if (ret < 0) {
		dev_err(dev, "Failed to get dsi-lanes property (%d)\n", ret);
		return ret;
	}

	/*
	 * 'display-timings' is optional, so verify if the node is present
	 * before calling of_get_videomode so we won't get console error
	 * messages
	 */
	timings = of_get_child_by_name(np, "display-timings");
	if (timings) {
		of_node_put(timings);
		ret = of_get_videomode(np, &panel->vm, 0);
	} else {
		videomode_from_timing(&tc358775_default_timing, &panel->vm);
	}
	if (ret < 0)
		return ret;

	of_property_read_u32(np, "panel-width-mm", &panel->width_mm);
	of_property_read_u32(np, "panel-height-mm", &panel->height_mm);



	panel->power_gpio= devm_gpiod_get(dev, "powergpio", GPIOD_OUT_LOW);
	if (IS_ERR(panel->power_gpio))
		panel->power_gpio = NULL;
	
	panel->stby = devm_gpiod_get(dev, "stby", GPIOD_OUT_HIGH);
	if (IS_ERR(panel->stby))
		panel->stby = NULL;

	panel->reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(panel->reset))
		panel->reset = NULL;

	client_node = of_parse_phandle(dev->of_node, "client-device", 0);
	if (!client_node)
	{
		dev_err(dev, "phandle missing or invalid\n");
	}
	else
	{
		client_dev = of_find_i2c_device_by_node(client_node);
		if (!client_dev || !client_dev->dev.driver)
		{
			dev_err(dev, "failed to find client platform device\n");
		}
		else
		{
			panel->client = client_dev;
			panel->regmap = devm_regmap_init_i2c(panel->client, &tc358775_regmap_config);
			if (IS_ERR(panel->regmap))
			{
				ret = PTR_ERR(panel->regmap);
				printk("Failed to initialize regmap: %d\n", ret);
				dev_err(dev, "Failed to initialize regmap: %d\n", ret);
				return ret;
			}
		}
	}

	backlight = of_parse_phandle(dev->of_node, "backlight", 0);
	if (backlight)
	{
		panel->backlight = of_find_backlight_by_node(backlight);
		of_node_put(backlight);
	}

	drm_panel_init(&panel->base);
	panel->base.funcs = &tc358775_panel_funcs;
	panel->base.dev = dev;

	ret = drm_panel_add(&panel->base);

	if (ret < 0)
		return ret;

	ret = mipi_dsi_attach(dsi);
	if (ret < 0)
		drm_panel_remove(&panel->base);

	return ret;
}

static int tc358775_panel_remove(struct mipi_dsi_device *dsi)
{
	struct tc358775_panel *tc358775 = mipi_dsi_get_drvdata(dsi);
	struct device *dev = &dsi->dev;
	int ret;
	ret = tc358775_panel_unprepare(&tc358775->base);
	if (ret < 0)
		DRM_DEV_ERROR(dev, "Failed to unprepare panel (%d)\n", ret);
	ret = tc358775_panel_disable(&tc358775->base);
	if (ret < 0)
		DRM_DEV_ERROR(dev, "Failed to disable panel (%d)\n", ret);

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		DRM_DEV_ERROR(dev, "Failed to detach from host (%d)\n",
					  ret);

	drm_panel_detach(&tc358775->base);

	if (tc358775->base.dev)
		drm_panel_remove(&tc358775->base);

	return 0;
}

static void tc358775_panel_shutdown(struct mipi_dsi_device *dsi)
{
	struct tc358775_panel *tc358775 = mipi_dsi_get_drvdata(dsi);
	tc358775_panel_unprepare(&tc358775->base);
	tc358775_panel_disable(&tc358775->base);
}

static const struct of_device_id tc358775_of_match[] = {
	{
		.compatible = "toshiba,panel-tc358775",
	},
	{}
};
MODULE_DEVICE_TABLE(of, tc358775_of_match);

static struct mipi_dsi_driver tc358775_panel_driver = {
	.driver = {
		.name = "panel-toshiba-tc358775",
		.of_match_table = tc358775_of_match,
	},
	.probe = tc358775_panel_probe,
	.remove = tc358775_panel_remove,
	.shutdown = tc358775_panel_shutdown,
};
module_mipi_dsi_driver(tc358775_panel_driver);

MODULE_AUTHOR("Robert Chiras <robert.chiras@nxp.com>");
MODULE_DESCRIPTION("DRM Driver for Raydium RM67191 MIPI DSI panel");
MODULE_LICENSE("GPL v2");
