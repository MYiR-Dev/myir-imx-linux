/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Minimal VeriSilicon VVCAM sensor ABI used by NXP's i.MX8MP ISP stack.
 * Keep the layout in sync with isp-vvcam vvcam/common/vvsensor.h.
 */

#ifndef __VVCAM_VVSENSOR_H__
#define __VVCAM_VVSENSOR_H__

#include <linux/types.h>

#define VVCAM_SUPPORT_MAX_MODE_COUNT		20
#define VVCAM_CAP_BUS_INFO_I2C_ADAPTER_NR_POS	8
#define SENSOR_FIX_FRACBITS			10

enum {
	VVSENSORIOC_RESET = 0x100,
	VVSENSORIOC_S_POWER,
	VVSENSORIOC_G_POWER,
	VVSENSORIOC_S_CLK,
	VVSENSORIOC_G_CLK,
	VVSENSORIOC_QUERY,
	VVSENSORIOC_S_SENSOR_MODE,
	VVSENSORIOC_G_SENSOR_MODE,
	VVSENSORIOC_READ_REG,
	VVSENSORIOC_WRITE_REG,
	VVSENSORIOC_READ_ARRAY,
	VVSENSORIOC_WRITE_ARRAY,
	VVSENSORIOC_G_NAME,
	VVSENSORIOC_G_RESERVE_ID,
	VVSENSORIOC_G_CHIP_ID,
	VVSENSORIOC_S_INIT,
	VVSENSORIOC_S_STREAM,
	VVSENSORIOC_S_LONG_EXP,
	VVSENSORIOC_S_EXP,
	VVSENSORIOC_S_VSEXP,
	VVSENSORIOC_S_LONG_GAIN,
	VVSENSORIOC_S_GAIN,
	VVSENSORIOC_S_VSGAIN,
	VVSENSORIOC_S_FPS,
	VVSENSORIOC_G_FPS,
	VVSENSORIOC_S_HDR_RADIO,
	VVSENSORIOC_S_WB,
	VVSENSORIOC_S_BLC,
	VVSENSORIOC_G_EXPAND_CURVE,
	VVSENSORIOC_S_TEST_PATTERN,
	VVSENSORIOC_G_LENS,
	VVSENSORIOC_MAX,
};

struct vvcam_clk_s {
	__u32 status;
	unsigned long sensor_mclk;
	unsigned long csi_max_pixel_clk;
};

struct vvcam_sccb_data_s {
	__u32 addr;
	__u32 data;
};

struct sensor_hdr_artio_s {
	__u32 ratio_l_s;
	__u32 ratio_s_vs;
	__u32 accuracy;
};

struct vvcam_ae_info_s {
	__u32 def_frm_len_lines;
	__u32 curr_frm_len_lines;
	__u32 one_line_exp_time_ns;
	__u32 max_longintegration_line;
	__u32 min_longintegration_line;
	__u32 max_integration_line;
	__u32 min_integration_line;
	__u32 max_vsintegration_line;
	__u32 min_vsintegration_line;
	__u32 max_long_again;
	__u32 min_long_again;
	__u32 max_long_dgain;
	__u32 min_long_dgain;
	__u32 max_again;
	__u32 min_again;
	__u32 max_dgain;
	__u32 min_dgain;
	__u32 max_short_again;
	__u32 min_short_again;
	__u32 max_short_dgain;
	__u32 min_short_dgain;
	__u32 start_exposure;
	__u32 gain_step;
	__u32 cur_fps;
	__u32 max_fps;
	__u32 min_fps;
	__u32 min_afps;
	__u8 int_update_delay_frm;
	__u8 gain_update_delay_frm;
	struct sensor_hdr_artio_s hdr_ratio;
};

struct sensor_mipi_info_s {
	__u32 mipi_lane;
};

enum sensor_hdr_mode_e {
	SENSOR_MODE_LINEAR,
	SENSOR_MODE_HDR_STITCH,
	SENSOR_MODE_HDR_NATIVE,
};

enum sensor_bayer_pattern_e {
	BAYER_RGGB,
	BAYER_GRBG,
	BAYER_GBRG,
	BAYER_BGGR,
	BAYER_BUTT,
};

struct sensor_test_pattern_s {
	__u8 enable;
	__u32 pattern;
};

struct sensor_data_compress_s {
	__u32 enable;
	__u32 x_bit;
	__u32 y_bit;
};

struct vvcam_size_s {
	__u32 bounds_width;
	__u32 bounds_height;
	__u32 top;
	__u32 left;
	__u32 width;
	__u32 height;
};

struct vvcam_mode_info_s {
	__u32 index;
	struct vvcam_size_s size;
	__u32 hdr_mode;
	__u32 stitching_mode;
	__u32 bit_width;
	struct sensor_data_compress_s data_compress;
	__u32 bayer_pattern;
	struct vvcam_ae_info_s ae_info;
	struct sensor_mipi_info_s mipi_info;
	void *preg_data;
	__u32 reg_data_count;
};

struct vvcam_mode_info_array_s {
	__u32 count;
	struct vvcam_mode_info_s modes[VVCAM_SUPPORT_MAX_MODE_COUNT];
};

#endif
