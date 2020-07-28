// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018 Rockchip Electronics Co. Ltd.
 * Copyright 2019 Toradex AG
 */

#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_graph.h>
#include <linux/regmap.h>
#include <video/of_display_timing.h>
#include <video/videomode.h>
#include <linux/regulator/consumer.h>
#include <linux/backlight.h>

#include <drm/drmP.h>
#include <drm/drm_of.h>
#include <drm/drm_atomic.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_panel.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_probe_helper.h>

struct lt8912 {
	struct drm_bridge bridge;
	struct drm_connector connector;
	struct drm_display_mode mode;
	struct device *dev;
	struct backlight_device *backlight;
	struct mipi_dsi_device *dsi;
	struct device_node *host_node;
	u8 num_dsi_lanes;
	u8 channel_id;
	unsigned int irq;
	u8 sink_is_hdmi;
	struct regmap *regmap[3];
	struct gpio_desc *hpd_gpio;
	struct gpio_desc *reset_n;
	struct i2c_adapter *ddc;        /* optional regular DDC I2C bus */
	u32 lvds_ctr;
};

/* 0x55 - 1280x720@60Hz */
//#define DRM_MODE(nm, t, c, hd, hss, hse, ht, hsk, vd, vss, vse, vt, vs, f) \
//	.name = nm, .status = 0, .type = (t), .clock = (c), \
//	.hdisplay = (hd), .hsync_start = (hss), .hsync_end = (hse), \
//	.htotal = (ht), .hskew = (hsk), .vdisplay = (vd), \
//	.vsync_start = (vss), .vsync_end = (vse), .vtotal = (vt), \
//	.vscan = (vs), .flags = (f), \
//	.base.type = DRM_MODE_OBJECT_MODE
//{ DRM_MODE("1280x720", DRM_MODE_TYPE_DRIVER, 74250, 1280, 1390,
//	   1430, 1650, 0, 720, 725, 730, 750, 0,
//	   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
//	    /* 60 - 1280x720@24Hz */
//	        { DRM_MODE("1280x720", DRM_MODE_TYPE_DRIVER, 59400, 1280, 3040,
//	                   3080, 3300, 0, 720, 725, 730, 750, 0,
//	                           DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) }


#define _LVDS_Output_  1 

#ifdef _LVDS_Output_

// 设置LVDS屏的CLK
#define Panel_Pixel_CLK  7000   //6500    // 65MHz * 100
//#define Panel_Pixel_CLK  7425   //6500    // 65MHz * 100
//#define Panel_Pixel_CLK  7425


//--------------------------------------------//
// 设置LVDS屏色深
#define _8_Bit_Color_           // 24 bit
//  #define _6_Bit_Color_ // 18 bit

// 定义LVDS输出模式
#define _VESA_
//#define _JEIDA_

//#define _De_mode_
#define _Sync_Mode_

//--------------------------------------------//

#ifdef _VESA_
#define _VesaJeidaMode 0x00
#else
#define _VesaJeidaMode 0x20
#endif

#ifdef _De_mode_
#define _DE_Sync_mode 0x08
#else
#define _DE_Sync_mode 0x00
#endif

#ifdef _8_Bit_Color_
#define _ColorDeepth 0x13
#else
#define _ColorDeepth 0x17
#endif

union Temp
{
	u8  Temp8[4];
	u16 Temp16[2];
	u32 Temp32;
}; 

enum                                                                                                                                         
{                                                                                                                                            
    H_act = 0,                                                                                                                               
    V_act,                                                                                                                                   
    H_tol,                                                                                                                                   
    V_tol,                                                                                                                                   
    H_bp,                                                                                                                                    
    H_sync,                                                                                                                                  
    V_sync,                                                                                                                                  
    V_bp                                                                                                                                     
};  


/**
 *  Horizontal display area +  HS Blanking  = HS period time
 *  Vertical display area  +   HS Blanking  =  VS period time
*/

/*
 * test demo
 *  htotal: hactive + hback-porch + hfront-porch + hsync-len
 *           1024      1344   
      1447=       1280       80          70               17      
 *  vtotal: vactive + vback-porch + vfront-porch + vsync-len
 *   823=        800         10           10           3
 *  htotal * vtotal * fps = clock-frequency
 * 
 */

#if 0
static int LVDS_Panel_Timing[] =                                                                                                             
//  H_act    V_act   H_total V_total H_BP    H_sync  V_sync  V_BP                                                                            
// { 1024, 600, 1344, 635, 140, 20, 3, 20 };       // 1024x600 Timing                                                                           
{ 1024, 768, 1344, 806, 160, 136, 6, 29 };  // 1024x768 Timing                                                                             
//{ 1024, 600, 1324, 628, 160, 116, 3, 24 };   // 1024x600 Timing  

static int MIPI_Timing[] =                                                                                                                   
//  H_act   V_act   H_total V_total H_BP    H_sync  V_sync  V_BP                                                                             
//  {1920,  1080,   2200,   1125,   148,        44,     5,      36};// 1080P  Vesa Timing                                                    
//    { 1280, 720,        1650,   750,        220,        40,     5,      20};// 720P                                                          
//  {720,   480,        858,        525,        60,     62,     6,      30};// 480P                                                          
//  {1366,  768,        1500,   800,        64,     56,     3,      28};// 1366x768 VESA Timing                                              
  { 1024, 768,        1344,   806,        160,        136,        6,      29};// 1024x768 Timing
#endif 


static int LVDS_Panel_Timing[] =                                                                                                             
//  H_act    V_act   H_total V_total H_BP    H_sync  V_sync  V_BP                                                                            
// { 1024, 600, 1344, 635, 140, 20, 3, 20 };       // 1024x600 Timing                                                                           
//{ 1024, 768, 1344, 806, 160, 136, 6, 29 };  // 1024x768 Timing                                                                             
//{ 1024, 600, 1324, 628, 160, 116, 3, 24 };   // 1024x600 Timing  
{ 1280, 800, 1447, 823, 60, 17, 3, 60 };	 // 1280x800 Timing 

static int MIPI_Timing[] =                                                                                                                   
//  H_act   V_act   H_total V_total H_BP    H_sync  V_sync  V_BP                                                                             
//  {1920,  1080,   2200,   1125,   148,        44,     5,      36};// 1080P  Vesa Timing                                                    
//    { 1280, 720,        1650,   750,        220,        40,     5,      20};// 720P                                                          
//  {720,   480,        858,        525,        60,     62,     6,      30};// 480P                                                          
//  {1366,  768,        1500,   800,        64,     56,     3,      28};// 1366x768 VESA Timing                                              
//  { 1024, 768,        1344,   806,        160,        136,        6,      29};// 1024x768 Timing
//  { 1024, 600, 1324, 628, 160, 116, 3, 24 };   // 1024x600 Timing 
  { 1280, 800, 1447, 823, 60, 17, 3, 60 };	 // 1280x800 Timing 
  
#endif 
// #endif 

static int lt8912_attach_dsi(struct lt8912 *lt);

static inline struct lt8912 *bridge_to_lt8912(struct drm_bridge *b)
{
	return container_of(b, struct lt8912, bridge);
}

static inline struct lt8912 *connector_to_lt8912(struct drm_connector *c)
{
	return container_of(c, struct lt8912, connector);
}

/* LT8912 MIPI to HDMI & LVDS REG setting - 20180115.txt */
static void lt8912_init(struct lt8912 *lt)
{
	u8 lanes = lt->dsi->lanes;
	const struct drm_display_mode *mode = &lt->mode;
	u32 hactive, hfp, hsync, hbp, vfp, vsync, vbp, htotal, vtotal,vactive;
//	unsigned int hsync_activehigh, vsync_activehigh, reg;
	unsigned int version[2];

	union Temp  Core_PLL_Ratio;
	float       f_DIV;

	dev_info(lt->dev, DRM_MODE_FMT "\n", DRM_MODE_ARG(mode));
	/* TODO: lvds output init */

	hactive = mode->hdisplay;
	vactive = mode->vdisplay;
	hfp = mode->hsync_start - mode->hdisplay;
	hsync = mode->hsync_end - mode->hsync_start;
//	hsync_activehigh = !!(mode->flags & DRM_MODE_FLAG_PHSYNC);
	hbp = mode->htotal - mode->hsync_end;
	vfp = mode->vsync_start - mode->vdisplay;
	vsync = mode->vsync_end - mode->vsync_start;
//	vsync_activehigh = !!(mode->flags & DRM_MODE_FLAG_PVSYNC);
	vbp = mode->vtotal - mode->vsync_end;
	htotal = mode->htotal;
	vtotal = mode->vtotal;


//	dev_info(lt->dev, "mode --- : %d, %d \n",hactive,vactive);

	regmap_read(lt->regmap[0], 0x00, &version[0]);
	regmap_read(lt->regmap[0], 0x01, &version[1]);

	dev_info(lt->dev, "LT8912 ID: %02x, %02x\n",version[0], version[1]);

	/* DigitalClockEn */
	regmap_write(lt->regmap[0], 0x08, 0xff);
	regmap_write(lt->regmap[0], 0x09, 0xff);
	regmap_write(lt->regmap[0], 0x0a, 0xff);
	regmap_write(lt->regmap[0], 0x0b, 0x7c);
	regmap_write(lt->regmap[0], 0x0c, 0xff);

	/* TxAnalog */
	regmap_write(lt->regmap[0], 0x31, 0xa1);
	regmap_write(lt->regmap[0], 0x32, 0xa1);
	regmap_write(lt->regmap[0], 0x33, 0x03);
	regmap_write(lt->regmap[0], 0x37, 0x00);
	regmap_write(lt->regmap[0], 0x38, 0x22);
	regmap_write(lt->regmap[0], 0x60, 0x82);

	/* CbusAnalog */
	regmap_write(lt->regmap[0], 0x39, 0x45);
	regmap_write(lt->regmap[0], 0x3a, 0x00);
	regmap_write(lt->regmap[0], 0x3b, 0x00);

	/* HDMIPllAnalog */
	regmap_write(lt->regmap[0], 0x44, 0x31);
	regmap_write(lt->regmap[0], 0x55, 0x44);
	regmap_write(lt->regmap[0], 0x57, 0x01);
	regmap_write(lt->regmap[0], 0x5a, 0x02);

	/* MIPIAnalog */
	regmap_write(lt->regmap[0], 0x3e, 0xce);
	regmap_write(lt->regmap[0], 0x3f, 0xd4);
	regmap_write(lt->regmap[0], 0x41, 0x3c);

	/* MipiBasicSet */
	regmap_write(lt->regmap[1], 0x12, 0x04);
	regmap_write(lt->regmap[1], 0x13, lanes % 4);
	regmap_write(lt->regmap[1], 0x14, 0x00);

	regmap_write(lt->regmap[1], 0x15, 0x00);
	regmap_write(lt->regmap[1], 0x1a, 0x03);
	regmap_write(lt->regmap[1], 0x1b, 0x03);

	/* MIPIDig */
	regmap_write(lt->regmap[1], 0x18, hsync%256);
	regmap_write(lt->regmap[1], 0x19, vsync%256);
	regmap_write(lt->regmap[1], 0x1c, hactive %256);
	regmap_write(lt->regmap[1], 0x1d, hactive/256);

//	regmap_write(lt->regmap[1], 0x2f, 0x0c);
	regmap_write(lt->regmap[1], 0x1e, 0x67);
	regmap_write(lt->regmap[1], 0x2f, 0x0c);

	regmap_write(lt->regmap[1], 0x34, htotal%256);
	regmap_write(lt->regmap[1], 0x35, htotal/256);
	regmap_write(lt->regmap[1], 0x36, vtotal%256);
	regmap_write(lt->regmap[1], 0x37, vtotal/256);
	regmap_write(lt->regmap[1], 0x38, vbp%256);
	regmap_write(lt->regmap[1], 0x39, vbp/256);
	regmap_write(lt->regmap[1], 0x3a, vfp % 0x100);
	regmap_write(lt->regmap[1], 0x3b, vfp >> 8);
	regmap_write(lt->regmap[1], 0x3c, hbp % 0x100);
	regmap_write(lt->regmap[1], 0x3d, hbp >> 8);
	regmap_write(lt->regmap[1], 0x3e, hfp % 0x100);
	regmap_write(lt->regmap[1], 0x3f, hfp >> 8);
//	regmap_read(lt->regmap[0], 0xab, &reg);
//	reg &= 0xfc;
//	reg |= (hsync_activehigh < 1) | vsync_activehigh;
//	regmap_write(lt->regmap[0], 0xab, reg);

	/* DDSConfig */
	regmap_write(lt->regmap[1], 0x4e, 0x99);
	regmap_write(lt->regmap[1], 0x4f, 0x99);
	regmap_write(lt->regmap[1], 0x50, 0x69);
	regmap_write(lt->regmap[1], 0x51, 0x80);
	regmap_write(lt->regmap[1], 0x51, 0x00);

	regmap_write(lt->regmap[1], 0x1f, 0x5e);
	regmap_write(lt->regmap[1], 0x20, 0x01);
	regmap_write(lt->regmap[1], 0x21, 0x2c);
	regmap_write(lt->regmap[1], 0x22, 0x01);
	regmap_write(lt->regmap[1], 0x23, 0xfa);
	regmap_write(lt->regmap[1], 0x24, 0x00);
	regmap_write(lt->regmap[1], 0x25, 0xc8);
	regmap_write(lt->regmap[1], 0x26, 0x00);
	regmap_write(lt->regmap[1], 0x27, 0x5e);
	regmap_write(lt->regmap[1], 0x28, 0x01);
	regmap_write(lt->regmap[1], 0x29, 0x2c);
	regmap_write(lt->regmap[1], 0x2a, 0x01);
	regmap_write(lt->regmap[1], 0x2b, 0xfa);
	regmap_write(lt->regmap[1], 0x2c, 0x00);
	regmap_write(lt->regmap[1], 0x2d, 0xc8);
	regmap_write(lt->regmap[1], 0x2e, 0x00);

	regmap_write(lt->regmap[0], 0x03, 0x7f);
	usleep_range(10000, 20000);
	regmap_write(lt->regmap[0], 0x03, 0xff);
	
	regmap_write(lt->regmap[1], 0x42, 0x64);
	regmap_write(lt->regmap[1], 0x43, 0x00);
	regmap_write(lt->regmap[1], 0x44, 0x04);
	regmap_write(lt->regmap[1], 0x45, 0x00);
	regmap_write(lt->regmap[1], 0x46, 0x59);
	regmap_write(lt->regmap[1], 0x47, 0x00);
	regmap_write(lt->regmap[1], 0x48, 0xf2);
	regmap_write(lt->regmap[1], 0x49, 0x06);
	regmap_write(lt->regmap[1], 0x4a, 0x00);
	regmap_write(lt->regmap[1], 0x4b, 0x72);
	regmap_write(lt->regmap[1], 0x4c, 0x45);
	regmap_write(lt->regmap[1], 0x4d, 0x00);
	regmap_write(lt->regmap[1], 0x52, 0x08);
	regmap_write(lt->regmap[1], 0x53, 0x00);
	regmap_write(lt->regmap[1], 0x54, 0xb2);
	regmap_write(lt->regmap[1], 0x55, 0x00);
	regmap_write(lt->regmap[1], 0x56, 0xe4);
	regmap_write(lt->regmap[1], 0x57, 0x0d);
	regmap_write(lt->regmap[1], 0x58, 0x00);
	regmap_write(lt->regmap[1], 0x59, 0xe4);
	regmap_write(lt->regmap[1], 0x5a, 0x8a);
	regmap_write(lt->regmap[1], 0x5b, 0x00);
	regmap_write(lt->regmap[1], 0x5c, 0x34);
	regmap_write(lt->regmap[1], 0x1e, 0x4f);
	regmap_write(lt->regmap[1], 0x51, 0x00);

	regmap_write(lt->regmap[0], 0xb2, lt->sink_is_hdmi);
//	regmap_write(lt->regmap[0], 0xb2, 0x01);

	/* Audio Disable */
//	regmap_write(lt->regmap[2], 0x06, 0x00);
//	regmap_write(lt->regmap[2], 0x07, 0x00);
//	regmap_write(lt->regmap[2], 0x34, 0xd2);
//	regmap_write(lt->regmap[2], 0x3c, 0x41);
	/* AudioIIsEn */
	regmap_write(lt->regmap[2], 0x06, 0x08);
	regmap_write(lt->regmap[2], 0x07, 0xf0);
	regmap_write(lt->regmap[2], 0x34, 0xd2);
	regmap_write(lt->regmap[2], 0x3c, 0x41);
	
    /* AVI Packet Config*/
	regmap_write(lt->regmap[2], 0x3e, 0x0A);
	regmap_write(lt->regmap[2], 0x43, 0x46-4);
	regmap_write(lt->regmap[2], 0x44, 0x10);
	regmap_write(lt->regmap[2], 0x45, 0x19);
	regmap_write(lt->regmap[2], 0x47, 0x00+4);
	

	/* MIPIRxLogicRes */
	regmap_write(lt->regmap[0], 0x03, 0x7f);
	usleep_range(10000, 20000);
	regmap_write(lt->regmap[0], 0x03, 0xff);

	regmap_write(lt->regmap[1], 0x51, 0x80);
	usleep_range(10000, 20000);
	regmap_write(lt->regmap[1], 0x51, 0x00);

#if 1
	if(lt->lvds_ctr){
		
	Core_PLL_Ratio.Temp32 = (Panel_Pixel_CLK / 25	) * 7;
	regmap_write(lt->regmap[0], 0x50, 0x24);
	regmap_write(lt->regmap[0], 0x51, 0x05);
	regmap_write(lt->regmap[0], 0x52, 0x14);

	regmap_write(lt->regmap[0], 0x69, (u8)((Core_PLL_Ratio.Temp32/100) & 0x000000FF)) ;
	regmap_write(lt->regmap[0], 0x69,  0x80 + (u8)((Core_PLL_Ratio.Temp32/100) & 0x000000FF)) ;
	Core_PLL_Ratio.Temp32  = (Core_PLL_Ratio.Temp32 % 100) & 0x000000FF;
	Core_PLL_Ratio.Temp32  = Core_PLL_Ratio.Temp32 * 16384;
	Core_PLL_Ratio.Temp32  = Core_PLL_Ratio.Temp32 / 100;
	regmap_write(lt->regmap[0], 0x6c, 0x80 + Core_PLL_Ratio.Temp8[1]) ;
	regmap_write(lt->regmap[0], 0x6b, Core_PLL_Ratio.Temp8[0]) ;
	regmap_write(lt->regmap[0], 0x04,  0xfb) ;
	regmap_write(lt->regmap[0], 0x04,  0xff) ;
 

	regmap_write(lt->regmap[0], 0x80,  0x00) ;
	regmap_write(lt->regmap[0], 0x81,  0xff) ;
	regmap_write(lt->regmap[0], 0x82,  0x03) ;
	regmap_write(lt->regmap[0], 0x83,  0x00) ;
	regmap_write(lt->regmap[0], 0x84,  0x05) ;
	regmap_write(lt->regmap[0], 0x85,  0x80) ;
	regmap_write(lt->regmap[0], 0x86,  0x10) ;

#if 0
	regmap_write(lt->regmap[0], 0x87,  (u8)(LVDS_Panel_Timing[H_tol] % 256	)) ;
	regmap_write(lt->regmap[0], 0x88,  (u8)(LVDS_Panel_Timing[H_tol] / 256	)) ;
	regmap_write(lt->regmap[0], 0x89,  (u8)(LVDS_Panel_Timing[H_sync] % 256  )) ;
	regmap_write(lt->regmap[0], 0x8a,  (u8)(LVDS_Panel_Timing[H_bp] % 256  ) ) ;
	regmap_write(lt->regmap[0], 0x8b,  (u8)((LVDS_Panel_Timing[H_bp]/256)*0x80+(LVDS_Panel_Timing[V_sync]%256 ))) ;
	regmap_write(lt->regmap[0], 0x8c,  (u8)(LVDS_Panel_Timing[H_act] % 256)) ;
	regmap_write(lt->regmap[0], 0x8d,  (u8)(LVDS_Panel_Timing[V_act] % 256)) ;
	regmap_write(lt->regmap[0], 0x8e,  (u8)((LVDS_Panel_Timing[V_act]/256)*0x10+(LVDS_Panel_Timing[H_act]/256 ))) ;
#else 
	regmap_write(lt->regmap[0], 0x87,  (u8)(htotal % 256	)) ;
	regmap_write(lt->regmap[0], 0x88,  (u8)(htotal / 256	)) ;
	regmap_write(lt->regmap[0], 0x89,  (u8)(hsync % 256  )) ;
	regmap_write(lt->regmap[0], 0x8a,  (u8)(hbp % 256  ) ) ;
	regmap_write(lt->regmap[0], 0x8b,  (u8)((hbp/256)*0x80+(vsync%256 ))) ;
	regmap_write(lt->regmap[0], 0x8c,  (u8)( hactive % 256)) ;
	regmap_write(lt->regmap[0], 0x8d,  (u8)( vactive % 256)) ;
	regmap_write(lt->regmap[0], 0x8e,  (u8)((vactive/256)*0x10+(hactive/256 ))) ;
#endif 

//	f_DIV				   = ( ( (float)( MIPI_Timing[H_act] - 1 ) ) / (float)( LVDS_Panel_Timing[H_act] - 1 ) ) * 4096;
//	Core_PLL_Ratio.Temp32  = (u32)f_DIV;
    Core_PLL_Ratio.Temp32  = (hactive-1)*4096/(hactive - 1);
	regmap_write(lt->regmap[0], 0x8f, Core_PLL_Ratio.Temp8[0]) ;
	regmap_write(lt->regmap[0], 0x90, Core_PLL_Ratio.Temp8[1]) ;

//	f_DIV				   = ( ( (float)( MIPI_Timing[V_act] - 1 ) ) / (float)( LVDS_Panel_Timing[V_act] - 1 ) ) * 4096;
//	Core_PLL_Ratio.Temp32  = (u32)f_DIV;
	Core_PLL_Ratio.Temp32  = (vactive-1)*4096/(vactive - 1);
	regmap_write(lt->regmap[0], 0x91, Core_PLL_Ratio.Temp8[0]) ;
	regmap_write(lt->regmap[0], 0x92, Core_PLL_Ratio.Temp8[1]) ;


	regmap_write(lt->regmap[0], 0x7f, 0x9c) ;
//    regmap_write(lt->regmap[0], 0xa8, _VesaJeidaMode + _DE_Sync_mode + _ColorDeepth);
    regmap_write(lt->regmap[0], 0xa8, 0x1b);

	usleep_range(300000, 400000);
	regmap_write(lt->regmap[0], 0x44, 0x30);

	}

#endif  

}

static void lt8912_wakeup(struct lt8912 *lt)
{
	gpiod_direction_output(lt->reset_n, 1);
	msleep(120);
	gpiod_direction_output(lt->reset_n, 0);

	regmap_write(lt->regmap[0], 0x08,0xff); /* enable clk gating */
	regmap_write(lt->regmap[0], 0x41,0x3c); /* MIPI Rx Power On */
	regmap_write(lt->regmap[0], 0x05,0xfb); /* DDS logical reset */
	regmap_write(lt->regmap[0], 0x05,0xff);
	regmap_write(lt->regmap[0], 0x03,0x7f); /* MIPI RX logical reset */
	usleep_range(10000, 20000);
	regmap_write(lt->regmap[0], 0x03,0xff);
	regmap_write(lt->regmap[0], 0x32,0xa1);
	regmap_write(lt->regmap[0], 0x33,0x03);
}

static void lt8912_sleep(struct lt8912 *lt)
{
	regmap_write(lt->regmap[0], 0x32,0xa0);
	regmap_write(lt->regmap[0], 0x33,0x00); /* Disable HDMI output. */
	regmap_write(lt->regmap[0], 0x41,0x3d); /* MIPI Rx Power Down. */
	regmap_write(lt->regmap[0], 0x08,0x00); /* diable DDS clk. */

	gpiod_direction_output(lt->reset_n, 1);
}

static enum drm_connector_status
lt8912_connector_detect(struct drm_connector *connector, bool force)
{
	struct lt8912 *lt = connector_to_lt8912(connector);
	enum drm_connector_status hpd, hpd_last;
	int timeout = 0;

	hpd = connector_status_unknown;
	do {
		hpd_last = hpd;
		hpd = gpiod_get_value_cansleep(lt->hpd_gpio) ?
			connector_status_connected : connector_status_disconnected;
		msleep(20);
		timeout += 20;
	} while((hpd_last != hpd) && (timeout < 500));

	return hpd;
}

static const struct drm_connector_funcs lt8912_connector_funcs = {
	.detect = lt8912_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static irqreturn_t lt8912_hpd_irq_thread(int irq, void *arg)
{
	struct lt8912 *lt = arg;
	struct drm_connector *connector = &lt->connector;

	drm_helper_hpd_irq_event(connector->dev);

	lt8912_init(lt);
	
	return IRQ_HANDLED;
}

static struct drm_encoder *
lt8912_connector_best_encoder(struct drm_connector *connector)
{
	struct lt8912 *lt = connector_to_lt8912(connector);

	return lt->bridge.encoder;
}

static int lt8912_connector_get_modes(struct drm_connector *connector)
{
	struct lt8912 *lt = connector_to_lt8912(connector);
	struct edid *edid;
	struct display_timings *timings;
	u32 bus_format = MEDIA_BUS_FMT_RGB888_1X24;
	int i, ret, num_modes = 0;

	/* Check if optional DDC I2C bus should be used. */
	if (lt->ddc) {
		edid = drm_get_edid(connector, lt->ddc);
		if (edid) {
			drm_connector_update_edid_property(connector,
								edid);
			num_modes = drm_add_edid_modes(connector, edid);
			lt->sink_is_hdmi = !!drm_detect_hdmi_monitor(edid);
			kfree(edid);
		}
		if (num_modes == 0) {
			dev_warn(lt->dev, "failed to get display timings from EDID\n");
//			return 0;
		}

		 printk(" alex get display timings from dtb"); 
		timings = of_get_display_timings(lt->dev->of_node);
		
		if (timings->num_timings == 0) {
			dev_err(lt->dev, "failed to get display timings from dtb\n");
			return 0;
		}
		
		for (i = 0; i < timings->num_timings; i++) {
			struct drm_display_mode *mode;
			struct videomode vm;
		
			if (videomode_from_timings(timings, &vm, i)) {
				continue;
			}
			mode = drm_mode_create(connector->dev);
			drm_display_mode_from_videomode(&vm, mode);
			mode->type = DRM_MODE_TYPE_DRIVER;
		
			if (timings->native_mode == i)
				mode->type |= DRM_MODE_TYPE_PREFERRED;
		
			drm_mode_set_name(mode);
			drm_mode_probed_add(connector, mode);
			num_modes++;
		}
		if (num_modes == 0) {
			dev_err(lt->dev, "failed to get display modes from dtb\n");
			return 0;
		}

		
	} else { /* if not EDID, use dtb timings */
		timings = of_get_display_timings(lt->dev->of_node);

		if (timings->num_timings == 0) {
			dev_err(lt->dev, "failed to get display timings from dtb\n");
			return 0;
		}

		for (i = 0; i < timings->num_timings; i++) {
			struct drm_display_mode *mode;
			struct videomode vm;

			if (videomode_from_timings(timings, &vm, i)) {
				continue;
			}

			mode = drm_mode_create(connector->dev);
			drm_display_mode_from_videomode(&vm, mode);
			mode->type = DRM_MODE_TYPE_DRIVER;

			if (timings->native_mode == i)
				mode->type |= DRM_MODE_TYPE_PREFERRED;

			drm_mode_set_name(mode);
			drm_mode_probed_add(connector, mode);
			num_modes++;
		}
		if (num_modes == 0) {
			dev_err(lt->dev, "failed to get display modes from dtb\n");
			return 0;
		}
	}

	connector->display_info.bus_flags = DRM_BUS_FLAG_DE_LOW |
					    DRM_BUS_FLAG_PIXDATA_NEGEDGE;
	ret = drm_display_info_set_bus_formats(&connector->display_info,
					       &bus_format, 1);

	if (ret)
		return ret;

	return num_modes;
}

static enum drm_mode_status lt8912_connector_mode_valid(struct drm_connector *connector,
			     struct drm_display_mode *mode)
{
	if (mode->clock > 150000)
		return MODE_CLOCK_HIGH;

	if (mode->hdisplay > 1920)
		return MODE_BAD_HVALUE;

	if (mode->vdisplay > 1080)
		return MODE_BAD_VVALUE;

	return MODE_OK;
}

static const struct drm_connector_helper_funcs lt8912_connector_helper_funcs = {
	.get_modes = lt8912_connector_get_modes,
	.best_encoder = lt8912_connector_best_encoder,
	.mode_valid = lt8912_connector_mode_valid,
};

static void lt8912_bridge_post_disable(struct drm_bridge *bridge)
{
	struct lt8912 *lt = bridge_to_lt8912(bridge);
	lt8912_sleep(lt);
}

static void lt8912_bridge_enable(struct drm_bridge *bridge)
{
	struct lt8912 *lt = bridge_to_lt8912(bridge);
	lt8912_init(lt);
}

static void lt8912_bridge_pre_enable(struct drm_bridge *bridge)
{
	struct lt8912 *lt = bridge_to_lt8912(bridge);
	lt8912_wakeup(lt);
}

static void lt8912_bridge_mode_set(struct drm_bridge *bridge,
				  const struct drm_display_mode *mode,
				  const struct drm_display_mode *adj)
{
	struct lt8912 *lt = bridge_to_lt8912(bridge);

	drm_mode_copy(&lt->mode, adj);
}

static int lt8912_bridge_attach(struct drm_bridge *bridge)
{
	struct lt8912 *lt = bridge_to_lt8912(bridge);
	struct drm_connector *connector = &lt->connector;
	int ret;

	connector->polled = DRM_CONNECTOR_POLL_HPD;

	ret = drm_connector_init(bridge->dev, connector,
				 &lt8912_connector_funcs,
				 DRM_MODE_CONNECTOR_HDMIA);
	if (ret) {
		dev_err(lt->dev, "failed to initialize connector\n");
		return ret;
	}

	drm_connector_helper_add(connector, &lt8912_connector_helper_funcs);
	drm_connector_attach_encoder(connector, bridge->encoder);

	ret = lt8912_attach_dsi(lt);

	enable_irq(lt->irq);

	return ret;
}

static const struct drm_bridge_funcs lt8912_bridge_funcs = {
	.attach = lt8912_bridge_attach,
	.mode_set = lt8912_bridge_mode_set,
	.pre_enable = lt8912_bridge_pre_enable,
	.enable = lt8912_bridge_enable,
	.post_disable = lt8912_bridge_post_disable,
};

static const struct regmap_config lt8912_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0xff,
};

static int lt8912_i2c_init(struct lt8912 *lt,
			   struct i2c_client *client)
{
	struct i2c_board_info info[] = {
		{ I2C_BOARD_INFO("lt8912p0", 0x48), },
		{ I2C_BOARD_INFO("lt8912p1", 0x49), },
		{ I2C_BOARD_INFO("lt8912p2", 0x4a), }
	};
	struct regmap *regmap;
	unsigned int i;
	int ret;

	if (!lt || !client)
		return -ENODEV;

	for (i = 0; i < ARRAY_SIZE(info); i++) {
		if (i > 0 ) {
			client = i2c_new_dummy(client->adapter, info[i].addr);
			if (!client)
				return -ENODEV;
		}
		regmap = devm_regmap_init_i2c(client, &lt8912_regmap_config);
		if (IS_ERR(regmap)) {
			ret = PTR_ERR(regmap);
			dev_err(lt->dev,
				"Failed to initialize regmap: %d\n", ret);
			return ret;
		}

		lt->regmap[i] = regmap;
	}

	return 0;
}

int lt8912_attach_dsi(struct lt8912 *lt)
{
	struct device *dev = lt->dev;
	struct mipi_dsi_host *host;
	struct mipi_dsi_device *dsi;
	int ret = 0;
	const struct mipi_dsi_device_info info = { .type = "lt8912",
						   .channel = lt->channel_id,
						   .node = NULL,
						 };

	host = of_find_mipi_dsi_host_by_node(lt->host_node);
	if (!host) {
		dev_err(dev, "failed to find dsi host\n");
		return -EPROBE_DEFER;
	}

	dsi = mipi_dsi_device_register_full(host, &info);
	if (IS_ERR(dsi)) {
		dev_err(dev, "failed to create dsi device\n");
		ret = PTR_ERR(dsi);
		goto err_dsi_device;
	}

	lt->dsi = dsi;

	dsi->lanes = lt->num_dsi_lanes;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_MODE_LPM | MIPI_DSI_MODE_EOT_PACKET;

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		dev_err(dev, "failed to attach dsi to host\n");
		goto err_dsi_attach;
	}

	return 0;

err_dsi_attach:
	mipi_dsi_device_unregister(dsi);
err_dsi_device:
	return ret;
}

void lt8912_detach_dsi(struct lt8912 *lt)
{
	mipi_dsi_detach(lt->dsi);
	mipi_dsi_device_unregister(lt->dsi);
}


static int lt8912_probe(struct i2c_client *i2c, const struct i2c_device_id *id)
{
	struct device *dev = &i2c->dev;
	struct lt8912 *lt;
	struct device_node *ddc_phandle;
	struct device_node *endpoint;
	unsigned int irq_flags;
	int ret,dsi_lanes;
	u32 lvds_ctr=0;

	static int initialize_it = 1;

	if(!initialize_it) {
		initialize_it = 1;
		return -EPROBE_DEFER;
	}


	lt = devm_kzalloc(dev, sizeof(*lt), GFP_KERNEL);
	if (!lt)
		return -ENOMEM;

	lt->dev = dev;
    lt->lvds_ctr =0;

	of_property_read_u32(dev->of_node, "dsi-lanes", &dsi_lanes);
	of_property_read_u32(dev->of_node, "lvds-enabled", &lvds_ctr);


	/* get optional regular DDC I2C bus */
	ddc_phandle = of_parse_phandle(dev->of_node, "ddc-i2c-bus", 0);
	if (ddc_phandle) {
		lt->ddc = of_get_i2c_adapter_by_node(ddc_phandle);
		if (!(lt->ddc))
			ret = -EPROBE_DEFER;
		of_node_put(ddc_phandle);
	}

	lt->hpd_gpio = devm_gpiod_get(dev, "hpd", GPIOD_IN);
	if (IS_ERR(lt->hpd_gpio)) {
		dev_err(dev, "failed to get hpd gpio\n");
		return ret;
	}

	lt->irq = gpiod_to_irq(lt->hpd_gpio);
	if (lt->irq == -ENXIO) {
		dev_err(dev, "failed to get hpd irq\n");
		return -ENODEV;
	}
	if (lt->irq < 0) {
		dev_err(dev, "failed to get hpd irq, %i\n", lt->irq);
		return lt->irq;
	}

	irq_flags = IRQF_TRIGGER_RISING | IRQF_ONESHOT;
//	irq_flags = IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING | IRQF_ONESHOT;
	ret = devm_request_threaded_irq(dev, lt->irq,
					NULL,
					lt8912_hpd_irq_thread,
					irq_flags, "lt8912_hpd", lt);
	if (ret) {
		dev_err(dev, "failed to request irq\n");
		return -ENODEV;
	}

	disable_irq(lt->irq);

	lt->reset_n = devm_gpiod_get_optional(dev, "reset", GPIOD_ASIS);
	if (IS_ERR(lt->reset_n)) {
		ret = PTR_ERR(lt->reset_n);
		dev_err(dev, "failed to request reset GPIO: %d\n", ret);
		return ret;
	}

	/*reset lt8912*/
	gpiod_direction_output(lt->reset_n, 0);
	msleep(100);
	gpiod_direction_output(lt->reset_n, 1);
	msleep(100);


	ret = lt8912_i2c_init(lt, i2c);
	if (ret)
		return ret;

	/* TODO: interrupt handing */
  
	lt->num_dsi_lanes = dsi_lanes;
	lt->channel_id = 1;
	lt->lvds_ctr =  lvds_ctr;

	endpoint = of_graph_get_next_endpoint(dev->of_node, NULL);
	if (!endpoint)
		return -ENODEV;

	lt->host_node = of_graph_get_remote_port_parent(endpoint);
	if (!lt->host_node) {
		of_node_put(endpoint);
		return -ENODEV;
	}

	of_node_put(endpoint);
	of_node_put(lt->host_node);

	lt->bridge.funcs = &lt8912_bridge_funcs;
	lt->bridge.of_node = dev->of_node;
	drm_bridge_add(&lt->bridge);
	

	return 0;
}

static int lt8912_remove(struct i2c_client *i2c)
{
	struct lt8912 *lt = i2c_get_clientdata(i2c);

	lt8912_sleep(lt);
	mipi_dsi_detach(lt->dsi);
	drm_bridge_remove(&lt->bridge);

	return 0;
}

static const struct i2c_device_id lt8912_i2c_ids[] = {
	{ "lt8912", 0 },
	{ }
};

static const struct of_device_id lt8912_of_match[] = {
	{ .compatible = "lontium,lt8912" },
	{}
};
MODULE_DEVICE_TABLE(of, lt8912_of_match);

static struct mipi_dsi_driver lt8912_driver = {
	.driver.name = "lt8912",
};

static struct i2c_driver lt8912_i2c_driver = {
	.driver = {
		.name = "lt8912",
		.of_match_table = lt8912_of_match,
	},
	.id_table = lt8912_i2c_ids,
	.probe = lt8912_probe,
	.remove = lt8912_remove,
};

static int __init lt8912_i2c_drv_init(void)
{
	mipi_dsi_driver_register(&lt8912_driver);

	return i2c_add_driver(&lt8912_i2c_driver);
}
module_init(lt8912_i2c_drv_init);

static void __exit lt8912_i2c_exit(void)
{
	i2c_del_driver(&lt8912_i2c_driver);

	mipi_dsi_driver_unregister(&lt8912_driver);
}
module_exit(lt8912_i2c_exit);

MODULE_AUTHOR("Wyon Bi <bivvy.bi@rock-chips.com>");
MODULE_DESCRIPTION("Lontium LT8912 MIPI-DSI to LVDS and HDMI/MHL bridge");
MODULE_LICENSE("GPL v2");
