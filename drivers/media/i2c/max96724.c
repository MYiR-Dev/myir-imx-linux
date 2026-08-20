// SPDX-License-Identifier: GPL-2.0+
/*
 * Maxim MAX96724 GMSL2/1 Deserializer Driver
 *
 * Copyright 2024 NXP
 *
 */
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/i2c-mux.h>
#include <linux/of_gpio.h>
#include <linux/of_graph.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/workqueue.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

#include "max96724_regs.h"

#define MAX96722_DEV_ID				0xA1
#define MAX96724_DEV_ID				0xA2

/*
 * The sink and source pads are created to match the OF graph port numbers so
 * that their indexes can be used interchangeably.
 */
#define MAX96724_N_GMSL			4

#define MAX96724_N_SINKS		4
#define MAX96724_N_SOURCES		2
#define MAX96724_N_PADS			6
#define MAX96724_SRC_PAD		4

/* MAX96717 transmits video on its fixed GMSL video pipe Z. */
#define MAX96724_GMSL_VIDEO_PIPE_Z	2

#define MAX96724_XTAL_CLOCK		25000000ULL

/* Let the SC360AT ISP converge after the ALI360 module leaves reset. */
#define MAX96724_ALI360_AE_SETTLE_MS	1000

enum max96724_data_type {
	MAX96724_DT_EMBEDDED		= 0x12,
	MAX96724_DT_YUV422_8BIT		= 0x1E,
	MAX96724_DT_YUV422_10BIT	= 0x1F,
	MAX96724_DT_YUV422_12BIT	= 0x30,
	MAX96724_DT_RGB565		= 0x22,
	MAX96724_DT_RGB666		= 0x23,
	MAX96724_DT_RGB888		= 0x24,
	MAX96724_DT_RAW8		= 0x2A,
	MAX96724_DT_RAW10		= 0x2B,
	MAX96724_DT_RAW12		= 0x2C,
	MAX96724_DT_RAW14		= 0x2D,
	MAX96724_DT_RAW16		= 0x2E,
	MAX96724_DT_RAW20		= 0x2F,
	MAX96724_DT_YUV422_20BIT	= 0x30,
};

enum max96724_gmsl_speed {
	MAX96724_GMSL_3G		= 1,
	MAX96724_GMSL_6G		= 2,
};


enum max96724_i2c_speed {
	MAX96724_I2C_BPS_9920,
	MAX96724_I2C_BPS_33200,
	MAX96724_I2C_BPS_99200,
	MAX96724_I2C_BPS_123000,
	MAX96724_I2C_BPS_203000,
	MAX96724_I2C_BPS_397000,
	MAX96724_I2C_BPS_625000,
	MAX96724_I2C_BPS_980000,
};

/*
 * The i.MX8MP staging CSI driver still uses the legacy .s_stream callback,
 * which cannot pass a stream mask.  Keep stream 0 as the default, but allow
 * bring-up to select one of the four routed virtual channels explicitly.
 */
static unsigned int max96724_legacy_stream;
module_param_named(legacy_stream, max96724_legacy_stream, uint, 0644);
MODULE_PARM_DESC(legacy_stream,
		 "source stream selected by the legacy s_stream callback (0-3)");

static bool max96722_phy_relock = true;
module_param_named(phy_relock, max96722_phy_relock, bool, 0644);
MODULE_PARM_DESC(phy_relock,
		 "restart MAX96722 CSI output after stream start (default: enabled)");

struct max96724_source {
	struct v4l2_subdev *sd;
	struct fwnode_handle *fwnode;
};

struct max96724_asc {
	struct v4l2_async_connection base;
	struct max96724_source *source;
};

struct max96724_priv {
	struct i2c_client *client;
	struct regmap *rmap;

	unsigned int reset_gpio;

	struct i2c_mux_core *mux;
	int mux_chan;

	unsigned int gmsl_link_mask;
	unsigned int gmsl_links_used;
	bool allow_no_link;
	bool is_max96722;
	bool ali360_yh_profile;
	unsigned int active_camera;

	unsigned int source_mask;
	unsigned int nsources;

	struct v4l2_subdev sd;
	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl *pixrate_ctrl;
	struct v4l2_ctrl *link_freq_ctrl;

	struct media_pad pads[MAX96724_N_PADS];
	struct max96724_source sources[MAX96724_N_GMSL];
	struct v4l2_async_notifier notifier;

	struct v4l2_mbus_framefmt sink_fmt[MAX96724_N_SINKS];
	struct v4l2_fract interval;

	/* Protects controls and fmt structures */
	struct mutex lock;
	/* Serializes legacy s_stream users from the two ISI channels. */
	struct mutex legacy_lock;
	unsigned int legacy_users;

	int csi2_data_lanes[MAX96724_N_SOURCES];
	unsigned int csi2_video_pipe_mask[MAX96724_N_SOURCES];

	struct delayed_work phy_relock_work;
	int enable_count;
};

#define to_index(priv, source) ((source) - &(priv)->sources[0])

static bool max96724_writable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case MAX96724_DEV_CTRL12 ... MAX96724_DEV_REG13:
	case MAX96724_TOP_CTRL_PWR0:
	case MAX96724_TOP_CTRL_CTRL3:
	case MAX96724_TOP_CTRL_INTR3:
	case MAX96724_TOP_CTRL_INTR5:
	case MAX96724_TOP_CTRL_INTR7:
	case MAX96724_TOP_CTRL_INTR9:
	case MAX96724_TOP_CTRL_INTR11:
	case MAX96724_TOP_CTRL_VID_PXL_CRC_VIDEOMASK_INT_FLAG:
	case MAX96724_TOP_CTRL_PWR_STATUS_OV_FLAG:
	case MAX96724_TOP_CTRL_VDDCMP_STATUS_FLAG:
	case MAX96724_TOP_CTRL_DEV_REV:
	case MAX96724_TOP_CTRL_EFUSE_CRC_ERR:
	case MAX96724_LINE_FAULT_REG1 ... MAX96724_LINE_FAULT_REG5:
	case MAX96724_VID_RX_VIDEO_RX8(0):
	case MAX96724_VID_RX_VIDEO_RX8(1):
	case MAX96724_VID_RX_VIDEO_RX8(2):
	case MAX96724_VID_RX_VIDEO_RX8(3):
	case MAX96724_GPIO_AGGR0_READ_A_L ... MAX96724_GPIO_AGGR0_READ_CD_H:
	case MAX96724_BACKTOP0_1:
	case MAX96724_BACKTOP0_11:
	case MAX96724_BACKTOP1_12 ... MAX96724_BACKTOP1_15:
	case MAX96724_BACKTOP1_33:
	case MAX96724_FSYNC_ERR_CNT:
	case MAX96724_FSYNC_CALC_FRM_LEN_L ... MAX96724_FSYNC_22:
	case MAX96724_MIPI_PHY_11:
	case MAX96724_MIPI_PHY_17 ... MAX96724_MIPI_PHY_18:
	case MAX96724_MIPI_PHY_25 ... MAX96724_MIPI_PHY_28:
	case MAX96724_MIPI_PHY_ERR_PKT_8 ... MAX96724_MIPI_PHY_ERR_PKT_11:
	case MAX96724_MIPI_TX_STATUS(0):
	case MAX96724_MIPI_TX_STATUS(1):
	case MAX96724_MIPI_TX_STATUS(2):
	case MAX96724_MIPI_TX_STATUS(3):
	case MAX96724_MIPI_TX_52(0):
	case MAX96724_MIPI_TX_52(1):
	case MAX96724_MIPI_TX_52(2):
	case MAX96724_MIPI_TX_52(3):
	case MAX96724_GMSL1_DET_ERR(0) ... MAX96724_GMSL1_CRC_VALUE_3(0):
	case MAX96724_GMSL1_DET_ERR(1) ... MAX96724_GMSL1_CRC_VALUE_3(1):
	case MAX96724_GMSL1_DET_ERR(2) ... MAX96724_GMSL1_CRC_VALUE_3(2):
	case MAX96724_GMSL1_DET_ERR(3) ... MAX96724_GMSL1_CRC_VALUE_3(3):
	case MAX96724_GMSL1_CB(0):
	case MAX96724_GMSL1_CB(1):
	case MAX96724_GMSL1_CB(2):
	case MAX96724_GMSL1_CB(3):
	case MAX96724_VID_HVD_DET_DE_DET ... MAX96724_VID_HVD_DET_VS_POL:
	case MAX96724_VID_HVD_DET_VS_CNT_0 ... MAX96724_VID_HVD_DET_DE_CNT_0_LSB:
	case MAX96724_VID_HVD_DET_VRX_0_CMP_ERR_FLAG:
	case MAX96724_VID_HVD_DET_VS_CNT_1 ... MAX96724_VID_HVD_DET_DE_CNT_1_LSB:
	case MAX96724_VID_HVD_DET_VRX_1_CMP_ERR_FLAG:
	case MAX96724_VID_HVD_DET_VS_CNT_2 ... MAX96724_VID_HVD_DET_DE_CNT_2_LSB:
	case MAX96724_VID_HVD_DET_VRX_2_CMP_ERR_FLAG:
	case MAX96724_VID_HVD_DET_VS_CNT_3 ... MAX96724_VID_HVD_DET_DE_CNT_3_LSB:
	case MAX96724_VID_HVD_DET_VRX_3_CMP_ERR_FLAG:
		return false;

	default:
		return true;
	}
}

static bool max96724_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case MAX96724_DEV_CTRL12 ... MAX96724_DEV_CTRL14:
	case MAX96724_TOP_CTRL_CTRL1 ... MAX96724_TOP_CTRL_CTRL3:
	case MAX96724_TOP_CTRL_INTR3:
	case MAX96724_TOP_CTRL_INTR5:
	case MAX96724_TOP_CTRL_INTR7:
	case MAX96724_TOP_CTRL_INTR9:
	case MAX96724_TOP_CTRL_INTR11:
	case MAX96724_TOP_CTRL_INTR13 ... MAX96724_TOP_CTRL_INTR16:
	case MAX96724_TOP_CTRL_DEC_ERR_A ... MAX96724_TOP_CTRL_IDLE_ERR_D:
	case MAX96724_TOP_CTRL_VID_PXL_CRC_VIDEOMASK_INT_FLAG:
	case MAX96724_TOP_CTRL_PWR_STATUS_OV_FLAG:
	case MAX96724_CFGL_GPIO_ARQ2(0):
	case MAX96724_CFGL_GPIO_ARQ2(1):
	case MAX96724_CFGL_GPIO_ARQ2(2):
	case MAX96724_CFGL_GPIO_ARQ2(3):
	case MAX96724_LINE_FAULT_REG1 ... MAX96724_LINE_FAULT_REG5:
	case MAX96724_GPIO_A_A(0):
	case MAX96724_GPIO_A_A(1):
	case MAX96724_GPIO_A_A(2):
	case MAX96724_GPIO_A_A(3):
	case MAX96724_GPIO_A_A(4):
	case MAX96724_GPIO_A_A(5):
	case MAX96724_GPIO_A_A(6):
	case MAX96724_GPIO_A_A(7):
	case MAX96724_GPIO_A_A(8):
	case MAX96724_GPIO_A_A(9):
	case MAX96724_GPIO_A_A(10):
	case MAX96724_BACKTOP0_1:
	case MAX96724_BACKTOP0_11:
	case MAX96724_BACKTOP1_33 ... MAX96724_BACKTOP1_HDR_ERR_4:
	case MAX96724_BACKTOP1_SRAM_LCRC_ERR:
	case MAX96724_FSYNC_ERR_CNT:
	case MAX96724_FSYNC_22:
	case MAX96724_MIPI_PHY_11:
	case MAX96724_MIPI_PHY_17:
	case MAX96724_VID_PXL_CRC_ERR_AX ... MAX96724_VID_PXL_CRC_ERR_DU:
	case MAX96724_VID_HVD_DET_DE_DET ... MAX96724_VID_HVD_DET_VS_POL:
		return true;

	default:
		return false;
	}
}

static const struct regmap_config max96724_regmap_cfg = {
	.name = "max96724",
	.reg_bits = 16,
	.val_bits = 8,

	.max_register = MAX96724_TUN_DET_CPHY_DET,
	.writeable_reg = max96724_writable_register,
	.volatile_reg = max96724_volatile_reg,

	.cache_type = REGCACHE_RBTREE,
};

struct max96724_format_info {
	u32 code;
	u8 data_type;
};

static const struct max96724_format_info max96724_formats[] = {
	/* YUV formats */
	{
		.code = MEDIA_BUS_FMT_UYVY8_1X16,
		.data_type = MAX96724_DT_YUV422_8BIT,
	}, {
		.code = MEDIA_BUS_FMT_VYUY8_1X16,
		.data_type = MAX96724_DT_YUV422_8BIT,
	}, {
		.code = MEDIA_BUS_FMT_YUYV8_1X16,
		.data_type = MAX96724_DT_YUV422_8BIT,
	}, {
		.code = MEDIA_BUS_FMT_YVYU8_1X16,
		.data_type = MAX96724_DT_YUV422_8BIT,
	}, {
		.code = MEDIA_BUS_FMT_UYVY10_1X20,
		.data_type = MAX96724_DT_YUV422_10BIT,
	}, {
		.code = MEDIA_BUS_FMT_VYUY10_1X20,
		.data_type = MAX96724_DT_YUV422_10BIT,
	}, {
		.code = MEDIA_BUS_FMT_YUYV10_1X20,
		.data_type = MAX96724_DT_YUV422_10BIT,
	}, {
		.code = MEDIA_BUS_FMT_YVYU10_1X20,
		.data_type = MAX96724_DT_YUV422_10BIT,
	},
	/* RGB formats */
	{
		.code = MEDIA_BUS_FMT_RGB565_1X16,
		.data_type = MAX96724_DT_RGB565,
	}, {
		.code = MEDIA_BUS_FMT_BGR888_1X24,
		.data_type = MAX96724_DT_RGB888,
	}, {
		/* i.MX8MP CSIS advertises RGB888 for CSI-2 data type 0x24. */
		.code = MEDIA_BUS_FMT_RGB888_1X24,
		.data_type = MAX96724_DT_RGB888,
	},
	/* RAW formats */
	{
		.code = MEDIA_BUS_FMT_SBGGR8_1X8,
		.data_type = MAX96724_DT_RAW8,
	}, {
		.code = MEDIA_BUS_FMT_SGBRG8_1X8,
		.data_type = MAX96724_DT_RAW8,
	}, {
		.code = MEDIA_BUS_FMT_SGRBG8_1X8,
		.data_type = MAX96724_DT_RAW8,
	}, {
		.code = MEDIA_BUS_FMT_SRGGB8_1X8,
		.data_type = MAX96724_DT_RAW8,
	}, {
		.code = MEDIA_BUS_FMT_SBGGR12_1X12,
		.data_type = MAX96724_DT_RAW12,
	}, {
		.code = MEDIA_BUS_FMT_SGBRG12_1X12,
		.data_type = MAX96724_DT_RAW12,
	}, {
		.code = MEDIA_BUS_FMT_SGRBG12_1X12,
		.data_type = MAX96724_DT_RAW12,
	}, {
		.code = MEDIA_BUS_FMT_SRGGB12_1X12,
		.data_type = MAX96724_DT_RAW12,
	}, {
		.code = MEDIA_BUS_FMT_SBGGR14_1X14,
		.data_type = MAX96724_DT_RAW14,
	}, {
		.code = MEDIA_BUS_FMT_SGBRG14_1X14,
		.data_type = MAX96724_DT_RAW14,
	}, {
		.code = MEDIA_BUS_FMT_SGRBG14_1X14,
		.data_type = MAX96724_DT_RAW14,
	}, {
		.code = MEDIA_BUS_FMT_SRGGB14_1X14,
		.data_type = MAX96724_DT_RAW14,
	}, {
		.code = MEDIA_BUS_FMT_SBGGR16_1X16,
		.data_type = MAX96724_DT_RAW16,
	}, {
		.code = MEDIA_BUS_FMT_SGBRG16_1X16,
		.data_type = MAX96724_DT_RAW16,
	}, {
		.code = MEDIA_BUS_FMT_SGRBG16_1X16,
		.data_type = MAX96724_DT_RAW16,
	}, {
		.code = MEDIA_BUS_FMT_SRGGB16_1X16,
		.data_type = MAX96724_DT_RAW16,
	},
};

static const struct v4l2_mbus_framefmt max96724_default_format = {
	.width		= 1920,
	.height		= 1280,
	.code		= MEDIA_BUS_FMT_SBGGR16_1X16,
	.colorspace	= V4L2_COLORSPACE_RAW,
	.xfer_func	= V4L2_XFER_FUNC_DEFAULT,
};

static const struct v4l2_mbus_framefmt max96724_ali360_yh_format = {
	.width		= 1920,
	.height		= 1536,
	.code		= MEDIA_BUS_FMT_UYVY8_1X16,
	.colorspace	= V4L2_COLORSPACE_SRGB,
	.xfer_func	= V4L2_XFER_FUNC_DEFAULT,
	.ycbcr_enc	= V4L2_YCBCR_ENC_601,
	.quantization	= V4L2_QUANTIZATION_FULL_RANGE,
};

static int max96724_i2c_mux_select(struct i2c_mux_core *muxc, u32 chan)
{
	struct max96724_priv *priv = i2c_mux_priv(muxc);
	u8 val = 0xff;

	if (priv->mux_chan == chan)
		return 0;

	val &= ~(0x3 << (chan * 2));
	val |= 0x2 << (chan * 2);
	regmap_write(priv->rmap, MAX96724_DEV_REG3, val);

	priv->mux_chan = chan;

	return 0;
}

static int max96724_i2c_init(struct max96724_priv *priv)
{
	int link;
	int ret;

	if (!i2c_check_functionality(priv->client->adapter, I2C_FUNC_SMBUS_WRITE_BYTE_DATA))
		return -ENODEV;

	priv->mux_chan = -1;

	priv->mux = i2c_mux_alloc(priv->client->adapter, &priv->client->dev,
				  priv->gmsl_links_used, 0, I2C_MUX_LOCKED,
				  max96724_i2c_mux_select, NULL);
	if (!priv->mux)
		return -ENOMEM;

	priv->mux->priv = priv;

	for (link = 0; link < MAX96724_N_GMSL; link++) {
		if (!(priv->gmsl_link_mask & BIT(link)))
			continue;

		ret = i2c_mux_add_adapter(priv->mux, 0, link);
		if (ret < 0)
			goto error;
	}

	return 0;

error:
	i2c_mux_del_adapters(priv->mux);
	return ret;
}

static int max96724_i2c_parse_dt(struct max96724_priv *priv)
{
	struct device *dev = &priv->client->dev;
	struct device_node *i2c_mux;
	struct device_node *node = NULL;

	i2c_mux = of_find_node_by_name(dev->of_node, "i2c-mux");
	if (!i2c_mux) {
		dev_err(dev, "Failed to find i2c-mux node\n");
		return -EINVAL;
	}

	/* Identify which i2c-mux channels are enabled */
	for_each_child_of_node(i2c_mux, node) {
		u32 id = 0;

		of_property_read_u32(node, "reg", &id);
		if (id >= MAX96724_N_GMSL)
			continue;

		if (!of_device_is_available(node)) {
			dev_dbg(dev, "Skipping disabled I2C bus port %u\n", id);
			continue;
		}

		priv->gmsl_link_mask |= BIT(id);
		priv->gmsl_links_used++;
	}
	of_node_put(node);
	of_node_put(i2c_mux);
	of_node_put(dev->of_node);

	return 0;
};

static int max96724_dt_parse_source_ep(struct max96724_priv *priv, struct device_node *node,
				       struct of_endpoint *ep)
{
	struct device *dev = &priv->client->dev;
	struct v4l2_fwnode_endpoint vep = {
		.bus_type = V4L2_MBUS_CSI2_DPHY
	};
	int csi_port;
	int ret;

	if (ep->id < 4)
		return 0;

	ret = v4l2_fwnode_endpoint_parse(of_fwnode_handle(node), &vep);
	if (ret) {
		of_node_put(node);
		return ret;
	}

	csi_port = ep->port - MAX96724_SRC_PAD;

	if (csi_port > 1) {
		dev_err(dev, "Wrong CSI port, deserializer has only 2 ports.\n");
		return -EINVAL;
	}

	priv->csi2_data_lanes[csi_port] = vep.bus.mipi_csi2.num_data_lanes;

	if (priv->csi2_data_lanes[csi_port] != 2 && priv->csi2_data_lanes[csi_port] != 4) {
		dev_err(dev, "Only 2 or 4 lanes are supported.");
		return -EINVAL;
	}

	return 0;
}

static int max96724_dt_parse_sink_ep(struct max96724_priv *priv, struct device_node *node,
				     struct of_endpoint *ep)
{
	struct device *dev = &priv->client->dev;
	struct max96724_source *source;
	struct device_node *csi_ep;
	struct of_endpoint csi_of_ep;
	unsigned int csi_port;

	/* Skip if the corresponding GMSL link is unavailable. */
	if (!(priv->gmsl_link_mask & BIT(ep->port)))
		return 0;

	if (ep->id == 1) {
		csi_ep = of_graph_get_remote_endpoint(node);
		of_graph_parse_endpoint(csi_ep, &csi_of_ep);

		csi_port = csi_of_ep.port - MAX96724_SRC_PAD;

		if (csi_port > 1) {
			dev_err(dev, "Wrong CSI port, deserializer has only 2 ports.\n");
			return -EINVAL;
		}

		priv->csi2_video_pipe_mask[csi_port] |= BIT(ep->port);
		of_node_put(csi_ep);
		return 0;
	}

	if (priv->sources[ep->port].fwnode) {
		dev_info(dev, "Multiple port endpoints are not supported: %d", ep->port);
		return 0;
	}

	source = &priv->sources[ep->port];
	source->fwnode = fwnode_graph_get_remote_endpoint(of_fwnode_handle(node));
	if (!source->fwnode) {
		dev_info(dev, "Endpoint %pOF has no remote endpoint connection\n", ep->local_node);
		return 0;
	}

	priv->source_mask |= BIT(ep->port);
	priv->nsources++;

	return 0;
}

static int max96724_parse_dt(struct max96724_priv *priv)
{
	struct device *dev = &priv->client->dev;
	struct device_node *node = NULL;
	int ret;

	for_each_endpoint_of_node(dev->of_node, node) {
		struct of_endpoint ep;

		of_graph_parse_endpoint(node, &ep);
		dev_dbg(dev, "Endpoint %pOF on port %d", ep.local_node, ep.port);

		if (ep.port >= MAX96724_N_PADS) {
			dev_err(dev, "Invalid endpoint %s on port %d",
				of_node_full_name(ep.local_node), ep.port);
			continue;
		}

		if (ep.port >= MAX96724_SRC_PAD) {
			ret = max96724_dt_parse_source_ep(priv, node, &ep);
			if (ret)
				return ret;

			continue;
		}

		ret = max96724_dt_parse_sink_ep(priv, node, &ep);
		if (ret)
			return ret;
	}
	of_node_put(node);

	return 0;
}

static unsigned int max96724_check_gmsl_links(struct max96724_priv *priv)
{
	unsigned int locked_links_mask = 0;
	unsigned int links_mask = priv->gmsl_link_mask;
	u16 link_lock_addr[4] = {
		MAX96724_TOP_CTRL_CTRL3,
		MAX96724_DEV_CTRL12,
		MAX96724_DEV_CTRL13,
		MAX96724_DEV_CTRL14
	};
	unsigned long timeout;

	regmap_update_bits(priv->rmap, MAX96724_DEV_REG6,
			   LINK_EN_A | LINK_EN_B | LINK_EN_C | LINK_EN_D,
			   priv->gmsl_link_mask);

	timeout = jiffies + msecs_to_jiffies(100);

	while (!time_after(jiffies, timeout)) {
		int current_link = ffs(links_mask) - 1;

		if (current_link == -1)
			break;

		if (regmap_test_bits(priv->rmap, link_lock_addr[current_link], BIT(3)))
			locked_links_mask |= BIT(current_link);

		links_mask &= ~BIT(current_link);

		if (!links_mask && priv->gmsl_link_mask == locked_links_mask)
			break;
		else if (!links_mask)
			links_mask = priv->gmsl_link_mask & ~locked_links_mask;

		usleep_range(1000, 2000);
	}

	return locked_links_mask;
}

static int max96724_chip_init(struct max96724_priv *priv)
{
	unsigned int locked_links;
	struct device *dev = &priv->client->dev;
	int i, retries = 3;

	while (retries--) {
		locked_links = max96724_check_gmsl_links(priv);
		if (locked_links == priv->gmsl_link_mask)
			break;

		regmap_write(priv->rmap, MAX96724_TOP_CTRL_PWR1, RESET_ALL);
		usleep_range(2000, 2500);
	}

	if (locked_links == 0 && !priv->allow_no_link) {
		dev_err(dev, "No GMSL link has locked after 3 retries. Abort!\n");
		return -ENODEV;
	}

	if (locked_links == 0) {
		dev_warn(dev, "No GMSL link locked; continuing in no-link debug mode\n");
		locked_links = priv->gmsl_link_mask;
	}

	dev_info(dev, "GMSL link mask: configured = 0x%x, locked = 0x%x\n",
		 priv->gmsl_link_mask, locked_links);

	/* Disable links that didn't lock. Perhaps user didn't connect all sensors. */
	regmap_update_bits(priv->rmap, MAX96724_DEV_REG6,
			   LINK_EN_A | LINK_EN_B | LINK_EN_C | LINK_EN_D,
			   locked_links);

	/* Disable remote control channel on all links. */
	regmap_write(priv->rmap, MAX96724_DEV_REG3, 0xff);

	/* Disable all video pipes access. */
	regmap_write(priv->rmap, MAX96724_DEV_REG4, 0);

	/* Disable all MIPI PHYs. */
	regmap_update_bits(priv->rmap, MAX96724_MIPI_PHY_2, PHY_STDBY_N_MASK, 0);

	/* Disable CSI output. */
	regmap_update_bits(priv->rmap, MAX96724_BACKTOP0_12, CSI_OUT_EN, 0);

	/* Disable all video pipes */
	regmap_write(priv->rmap, MAX96724_VIDEO_PIPE_SEL_VIDEO_PIPE_EN, 0);

	/* Set I2C speed on al GMSL ports to 980kbps */
	for (i = 0; i < 4; i++)
		regmap_update_bits(priv->rmap, MAX96724_CC_G2P0_I2C_1(i), MST_BT_P0_A_MASK,
				   MAX96724_I2C_BPS_980000 << MST_BT_P0_A_SHIFT);

	priv->gmsl_link_mask = locked_links;
	priv->gmsl_links_used = hweight8(locked_links);

	return 0;
}

static int __maybe_unused max96724_gmsl_speed_set(struct max96724_priv *priv,
						  enum max96724_gmsl_speed speed)
{
	int ret;

	ret = regmap_update_bits(priv->rmap, MAX96724_DEV_REG26,
				 RX_RATE_PHYA_MASK | RX_RATE_PHYB_MASK,
				 (priv->gmsl_link_mask & 0x1) ? speed << RX_RATE_PHYA_SHIFT : 0 |
				 (priv->gmsl_link_mask & 0x2) ? speed << RX_RATE_PHYB_SHIFT : 0);
	ret |= regmap_update_bits(priv->rmap, MAX96724_DEV_REG27,
				  RX_RATE_PHYC_MASK | RX_RATE_PHYD_MASK,
				 (priv->gmsl_link_mask & 0x4) ? speed << RX_RATE_PHYC_SHIFT : 0 |
				 (priv->gmsl_link_mask & 0x8) ? speed << RX_RATE_PHYD_SHIFT : 0);

	return ret ? -EIO : 0;
}

static int max96724_vc_mapping_set(struct max96724_priv *priv, int pipe, int map_no,
				   int src_vc, enum max96724_data_type src_dt,
				   int dst_vc, enum max96724_data_type dst_dt)
{
	int ret;

	ret = regmap_write(priv->rmap, MAX96724_MIPI_TX_MAP_SRC(pipe, map_no),
			   (src_vc << 6) | (src_dt & 0x3f));
	ret |= regmap_write(priv->rmap, MAX96724_MIPI_TX_MAP_DST(pipe, map_no),
			    (dst_vc << 6) | (dst_dt & 0x3f));

	return ret ? -EIO : 0;
}

/* Each set bit in the mapping_mask will enable a mapping. */
static int max96724_vc_mapping_en(struct max96724_priv *priv, int pipe, u16 mapping_mask)
{
	int ret;

	ret = regmap_write(priv->rmap, MAX96724_MIPI_TX_MAP_EN_H(pipe), mapping_mask >> 8);
	ret |= regmap_write(priv->rmap, MAX96724_MIPI_TX_MAP_EN_L(pipe), mapping_mask & 0xff);

	return ret ? -EIO : 0;
}

/*
 * The i.MX8MP capture path exposes at most two ISI DMA channels.  The
 * ALI360-YH board can have four links populated, so follow the NVP6324/N4
 * single-output model: keep all remote cameras alive, but route only one
 * MAX96722 video pipe to CSI and always present it as VC0.
 *
 * The caller must hold priv->lock.  MAX96722 DEV_REG4 keeps all four receive
 * packet detectors alive, while VIDEO_PIPE_EN and the CSI mappings select
 * exactly one transmit pipe.  Keeping VIDEO_PIPE_EN set for every pipe
 * disturbs the shared CSI transmitter; clearing DEV_REG4 makes an inactive
 * pipe lose sequence lock.
 * A short CSI-PHY restart after changing mappings gives the i.MX8MP CSIS a
 * clean packet boundary and prevents a later live switch from stalling DMA.
 */
static int max96724_wait_video_lock(struct max96724_priv *priv,
				    unsigned int camera);
static int max96724_phy_enable(struct max96724_priv *priv, bool en);

static int max96724_apply_active_camera(struct max96724_priv *priv)
{
	u16 mapping_mask = 0x7;
	unsigned int camera = priv->active_camera;
	u8 rx_pipe_mask = priv->csi2_video_pipe_mask[0] & VIDEO_PIPE_EN_MASK;
	bool live_switch = priv->enable_count;
	int i, ret = 0;

	if (!priv->ali360_yh_profile)
		return 0;

	if (camera >= MAX96724_N_GMSL ||
	    !(priv->csi2_video_pipe_mask[0] & BIT(camera)))
		return -ENOLINK;

	/* Live switches only select pipes prelocked during initial STREAMON. */
	if (live_switch) {
		ret = max96724_wait_video_lock(priv, camera);
		if (ret)
			return ret;

		ret = max96724_phy_enable(priv, false);
		if (ret)
			return ret;
	}

	for (i = 0; i < MAX96724_N_GMSL; i++)
		ret |= max96724_vc_mapping_en(priv, i, 0);
	if (!ret) {
		ret = max96724_vc_mapping_en(priv, camera, mapping_mask);
		ret |= regmap_update_bits(priv->rmap, MAX96724_DEV_REG4,
					  VIDEO_PIPE_EN_MASK, rx_pipe_mask);
		ret |= regmap_update_bits(priv->rmap,
					  MAX96724_VIDEO_PIPE_SEL_VIDEO_PIPE_EN,
					  VIDEO_PIPE_EN_MASK, BIT(camera));
	}

	if (live_switch) {
		msleep(50);
		ret |= max96724_phy_enable(priv, true);
	}

	return ret ? -EIO : 0;
}

/*
 * Wait only for the requested ALI360 pipe.  The caller enables that pipe
 * before polling; waiting for every link would make a single missing camera
 * prevent all capture.
 *
 * The caller must hold priv->lock.
 */
static int max96724_wait_video_lock(struct max96724_priv *priv,
				    unsigned int camera)
{
	struct device *dev = &priv->client->dev;
	unsigned int video_status = 0;
	int retries;
	int ret;

	for (retries = 0; retries < 100; retries++) {
		ret = regmap_read(priv->rmap,
				  MAX96724_VID_RX_VIDEO_RX8(camera),
				  &video_status);
		if (ret)
			return ret;

		if ((video_status & (VID_PKT_DET | VID_LOCK)) ==
		    (VID_PKT_DET | VID_LOCK))
			return 0;

		usleep_range(5000, 7000);
	}

	dev_err(dev, "ALI360-YH camera %u video did not lock (status 0x%02x)\n",
		camera, video_status);

	return -ETIMEDOUT;
}

/* Each VC src/dst map with a set bit in mapping_mask will be routed to dphy_no. */
static int max96724_vc_dphy_dst_select(struct max96724_priv *priv, int pipe,
				       unsigned long *mapping_mask, int dphy_no)
{
	int i, bit;
	int base_dphy_map_addr = MAX96724_MIPI_TX_45(pipe);
	u8 offs;
	u8 dphy_map[4] = {0};
	int ret = 0;

	for_each_set_bit(bit, mapping_mask, 16) {
		offs = bit / 4;
		dphy_map[offs] |= dphy_no << ((bit & 0x3) * 2);
	}

	for (i = 0; i < 4; i++)
		ret |= regmap_write(priv->rmap, base_dphy_map_addr + i, dphy_map[i]);

	return ret ? -EIO : 0;
}

static int max96724_assign_pipe_to_mipi_ctrl(struct max96724_priv *priv, int pipe)
{
	int ctrl_sel = 0;
	int csi_out_port = 0;
	int i;

	for (i = 0; i < MAX96724_N_SOURCES; i++) {
		if (priv->csi2_video_pipe_mask[i] & BIT(pipe)) {
			csi_out_port = i;
			break;
		}
	}

	/* in 2x4 lane mode, CSI-2 port A's master is PHY1 and CSI-2 port B's master is PHY2*/
	if (priv->csi2_data_lanes[csi_out_port] == 4)
		ctrl_sel = csi_out_port == 0 ? 1 : 2;
	else
		ctrl_sel = pipe;

	return regmap_update_bits(priv->rmap, MAX96724_MIPI_PHY_MIPI_CTRL_SEL, 0x3 << pipe * 2,
				  ctrl_sel << pipe * 2);
}

static int max96724_pipe_setup(struct max96724_priv *priv, int pipe,
			       struct v4l2_subdev_state *state)
{
	int ret, i;
	struct v4l2_mbus_framefmt *format;
	const struct max96724_format_info *info = NULL;
	unsigned long mapping_mask;
	u8 dt_vc0;
	u8 dst_vc;
	int csi_port = (priv->csi2_video_pipe_mask[0] & BIT(pipe)) ? 0 : 1;
	u8 pos_shift;

	/* the pixel data is on VC0 */
	format = v4l2_subdev_state_get_format(state, pipe, 0);
	if (!format)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(max96724_formats); ++i) {
		if (max96724_formats[i].code == format->code) {
			info = &max96724_formats[i];
			break;
		}
	}

	if (!info) {
		dev_err(&priv->client->dev, "Format not supported.\n");
		return -ENOTSUPP;
	}

	/* Setup GMSL pipe selection and enable pipe */
	pos_shift = (pipe & 0x1) * 4;
	ret = regmap_update_bits(priv->rmap,
				 MAX96724_VIDEO_PIPE_SEL_VIDEO_PIPE_SEL_0 + pipe / 2,
				 0xf << pos_shift,
				 ((pipe << 2) | MAX96724_GMSL_VIDEO_PIPE_Z) << pos_shift);
	ret |= regmap_update_bits(priv->rmap, MAX96724_DEV_REG4, BIT(pipe), BIT(pipe));
	ret |= regmap_update_bits(priv->rmap, MAX96724_VIDEO_PIPE_SEL_VIDEO_PIPE_EN,
				  BIT(pipe), BIT(pipe));

	/*
	 * The ALI360-YH stream contains image data plus frame-start/frame-end
	 * short packets.  Do not enable the unused embedded-data mapping: the
	 * vendor 4-lane profile programs MAP_EN=0x07 and DPHY_DEST=0x15.
	 */
	mapping_mask = priv->ali360_yh_profile ? 0x7 : 0xf;
	dt_vc0 = info->data_type;
	dst_vc = priv->ali360_yh_profile ? 0 : pipe;

	ret |= max96724_vc_mapping_en(priv, pipe, mapping_mask);
	ret |= max96724_vc_mapping_set(priv, pipe, 0, 0, dt_vc0,
				       dst_vc, dt_vc0);
	ret |= max96724_vc_mapping_set(priv, pipe, 1, 0, 0x00,
				       dst_vc, 0x00); /* frame-start */
	ret |= max96724_vc_mapping_set(priv, pipe, 2, 0, 0x01,
				       dst_vc, 0x01); /* frame-end */
	ret |= max96724_vc_mapping_set(priv, pipe, 3, 0, MAX96724_DT_EMBEDDED,
				       dst_vc, MAX96724_DT_EMBEDDED);

	ret |= max96724_vc_dphy_dst_select(priv, pipe, &mapping_mask, csi_port == 0 ? 1 : 2);
	/*
	 * MAX96722 keeps one CSI controller per video pipe when a 2x4 PHY port
	 * is operated with only two data lanes.  MAX96724 instead programs the
	 * master PHY controller for the selected output port.
	 */
	if (priv->is_max96722 && priv->csi2_data_lanes[csi_port] == 2)
		i = pipe;
	else
		i = csi_port == 0 ? 1 : 2;

	ret |= regmap_update_bits(priv->rmap, MAX96724_MIPI_TX_10(i),
			CSI2_LANE_CNT_MASK,
			(priv->csi2_data_lanes[csi_port] - 1) << CSI2_LANE_CNT_SHIFT);

	/* Disable diskew */
	ret |= regmap_write(priv->rmap, MAX96724_MIPI_TX_DESKEW_INIT(csi_port == 0 ? 1 : 2), 0x07);
	ret |= regmap_write(priv->rmap, MAX96724_MIPI_TX_DESKEW_PER(csi_port == 0 ? 1 : 2), 0x01);

	ret |= max96724_assign_pipe_to_mipi_ctrl(priv, pipe);

	return ret ? -EIO : 0;
}

static int max96724_dphy_config(struct max96724_priv *priv)
{
	static const unsigned int dpll_regs[] = {
		MAX96724_BACKTOP0_22,
		MAX96724_BACKTOP0_25,
		MAX96724_BACKTOP0_28,
		MAX96724_BACKTOP0_31,
	};
	int i;
	u8 setting;
	struct dphy_setting {
		int port_a_lanes;
		int port_b_lanes;
		u8 setting;
	} dphy_settings[] = {
		{.port_a_lanes = 4, .port_b_lanes = 4, .setting = PHY_2X4},
		{.port_a_lanes = 4, .port_b_lanes = 2, .setting = PHY_1X4A_22},
		{.port_a_lanes = 4, .port_b_lanes = 0, .setting = PHY_2X4},
		{.port_a_lanes = 2, .port_b_lanes = 4, .setting = PHY_1X4B_22},
		{.port_a_lanes = 2, .port_b_lanes = 2, .setting = PHY_4X2},
		{.port_a_lanes = 2, .port_b_lanes = 0, .setting = PHY_4X2},
		{.port_a_lanes = 0, .port_b_lanes = 4, .setting = PHY_2X4},
		{.port_a_lanes = 0, .port_b_lanes = 2, .setting = PHY_4X2},
	};

	/*
	 * Unlike MAX96724, MAX96722 exposes two four-lane CSI ports and has no
	 * 4x2-lane output mode.  A two-lane endpoint therefore still uses the
	 * paired PHYs of the corresponding 2x4-lane port; the CSI controller's
	 * lane-count field below limits the transmitted packet to two lanes.
	 */
	if (priv->is_max96722) {
		setting = PHY_2X4;
	} else {
		for (i = 0; i < ARRAY_SIZE(dphy_settings); i++) {
			if (priv->csi2_data_lanes[0] == dphy_settings[i].port_a_lanes &&
			    priv->csi2_data_lanes[1] == dphy_settings[i].port_b_lanes) {
				setting = dphy_settings[i].setting;
				break;
			}
		}

		if (i == ARRAY_SIZE(dphy_settings))
			return -EINVAL;
	}

	regmap_write(priv->rmap, MAX96724_MIPI_PHY_0, setting);

	for (i = 0; i < MAX96724_N_SOURCES; i++) {
		/*
		 * CSI port A uses PHY0/PHY1 and port B uses PHY2/PHY3.  Their
		 * lane-map registers start at MIPI_PHY_3 (0x08a3), while the
		 * polarity registers start at MIPI_PHY_5 (0x08a5).
		 */
		if (priv->csi2_data_lanes[i] == 4)
			regmap_write(priv->rmap, MAX96724_MIPI_PHY_3 + i, 0xE4);
		else if (priv->csi2_data_lanes[i] == 2)
			regmap_write(priv->rmap, MAX96724_MIPI_PHY_3 + i, 0x44);

		/* map the polarities */
		regmap_write(priv->rmap, MAX96724_MIPI_PHY_5 + i, 0x0); /* normal polarities */
	}

	if (priv->ali360_yh_profile) {
		/* The MAX96722 4-lane port uses all four physical PHY lane maps. */
		if (priv->is_max96722 && priv->csi2_data_lanes[0] == 4) {
			regmap_write(priv->rmap, MAX96724_MIPI_PHY_3, 0xe4);
			regmap_write(priv->rmap, MAX96724_MIPI_PHY_4, 0xe4);
		}

		for (i = 0; i < ARRAY_SIZE(dpll_regs); i++)
			regmap_write(priv->rmap, dpll_regs[i], 0x2a);

		regmap_write(priv->rmap, MAX96724_MIPI_PHY_9, 0xc8);
		regmap_write(priv->rmap, MAX96724_MIPI_PHY_10, 0xe0);
	}

	return 0;
}

static int max96724_phy_enable(struct max96724_priv *priv, bool en)
{
	int i;
	u8 phy_en = 0;
	int ret;

	if (priv->is_max96722 &&
	    (priv->csi2_data_lanes[0] || priv->csi2_data_lanes[1])) {
		/* Vendor 4-lane sequence ends with MIPI_PHY_2 = 0xf4. */
		phy_en = 0xf;
	} else {
		for (i = 0; i < MAX96724_N_SOURCES; i++)
			phy_en |= (priv->csi2_data_lanes[i] == 4 ? 0x3 :
				   priv->csi2_data_lanes[i] == 2 ?
				   (i == 0 ? 0x2 : 0x1) : 0) << (2 * i);
	}

	ret = regmap_update_bits(priv->rmap, MAX96724_MIPI_PHY_2,
				 PHY_STDBY_N_MASK, en ? phy_en << PHY_STDBY_N_SHIFT : 0);

	ret |= regmap_update_bits(priv->rmap, MAX96724_BACKTOP0_12, CSI_OUT_EN,
				  en ? CSI_OUT_EN : 0);

	return ret ? -EIO : 0;
}

static int max96724_pipes_disable_all(struct max96724_priv *priv)
{
	return regmap_update_bits(priv->rmap, MAX96724_VIDEO_PIPE_SEL_VIDEO_PIPE_EN,
				  VIDEO_PIPE_EN_MASK, 0);
}

static int max96724_fsync_set(struct max96724_priv *priv)
{
	int ret;

	/*
	 * According to Max96724 users guide, "sync signal frequency must be specified in terms of
	 * the onboard crystal clock (25MHz)"
	 */
	u32 fsync;

	if (!priv->interval.numerator || !priv->interval.denominator)
		return regmap_update_bits(priv->rmap, MAX96724_FSYNC_0, FSYNC_METH_MASK, 0);

	fsync = div_u64(MAX96724_XTAL_CLOCK * priv->interval.numerator, priv->interval.denominator);

	ret = regmap_write(priv->rmap, MAX96724_FSYNC_PERIOD_H, (fsync >> 16) & 0xff);
	ret |= regmap_write(priv->rmap, MAX96724_FSYNC_PERIOD_M, (fsync >> 8) & 0xff);
	ret |= regmap_write(priv->rmap, MAX96724_FSYNC_PERIOD_L, (fsync >> 0) & 0xff);

	ret |= regmap_update_bits(priv->rmap, MAX96724_FSYNC_0, FSYNC_METH_MASK | FSYNC_MODE_MASK,
				  (0x0 << FSYNC_METH_SHIFT) | (0x1 << FSYNC_MODE_SHIFT));

	ret |= regmap_write(priv->rmap, MAX96724_FSYNC_17, 0x00);
	ret |= regmap_write(priv->rmap, MAX96724_FSYNC_2, 0x00);

	if (priv->ali360_yh_profile) {
		/* Route internal FSYNC TX ID 0 to GPIO0 on all four GMSL links. */
		ret |= regmap_write(priv->rmap, MAX96724_GPIO_A_A(0), 0x83);
		ret |= regmap_write(priv->rmap, MAX96724_GPIO_A_B(0), 0x00);
		ret |= regmap_write(priv->rmap, MAX96724_GPIO_B_B(0), 0x20);
		ret |= regmap_write(priv->rmap, MAX96724_GPIO_C_B(0), 0x20);
		ret |= regmap_write(priv->rmap, MAX96724_GPIO0_D_B(0), 0x20);
	}

	return ret ? -EIO : 0;
}

/* V4L2 stuff */
static const struct media_entity_operations max96724_v4l2_media_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static int max96724_s_ctrl(struct v4l2_ctrl *ctrl)
{
	switch (ctrl->id) {
	case V4L2_CID_PIXEL_RATE:
		return 0;
	default:
		return -EINVAL;
	}
}

static const struct v4l2_ctrl_ops max96724_v4l2_ctrl_ops = {
	.s_ctrl = max96724_s_ctrl,
};

static int max96724_setup_all_pipes(struct max96724_priv *priv, struct v4l2_subdev_state *state)
{
	int i, ret;

	for (i = 0; i < MAX96724_N_SINKS; i++) {
		struct max96724_source *source = &priv->sources[i];

		if (!source->fwnode)
			continue;

		ret = max96724_pipe_setup(priv, i, state);
		if (ret == -EIO)
			return ret;
	}

	return 0;
}

static int max96724_get_frame_interval(struct v4l2_subdev *sd,
				       struct v4l2_subdev_state *state,
				       struct v4l2_subdev_frame_interval *interval)
{
	struct max96724_priv *priv = container_of(sd, struct max96724_priv, sd);

	if (interval->pad < MAX96724_SRC_PAD)
		return -EINVAL;

	interval->interval = priv->interval;

	return 0;
}

static int max96724_set_frame_interval(struct v4l2_subdev *sd,
				       struct v4l2_subdev_state *state,
				       struct v4l2_subdev_frame_interval *interval)
{
	struct max96724_priv *priv = container_of(sd, struct max96724_priv, sd);

	if (interval->pad < MAX96724_SRC_PAD)
		return -EINVAL;

	priv->interval = interval->interval;

	return 0;
}

static int max96724_set_fmt(struct v4l2_subdev *sd, struct v4l2_subdev_state *state,
			    struct v4l2_subdev_format *format)
{
	unsigned int pad = format->pad;
	struct v4l2_mbus_framefmt *sink_fmt;
	struct v4l2_subdev_route *route;
	int i;

	if (pad >= MAX96724_SRC_PAD)
		return v4l2_subdev_get_fmt(sd, state, format);

	/* Validate the format. */
	for (i = 0; i < ARRAY_SIZE(max96724_formats); ++i) {
		if (max96724_formats[i].code == format->format.code)
			break;
	}

	if (i == ARRAY_SIZE(max96724_formats))
		format->format.code = max96724_formats[12].code;

	sink_fmt = v4l2_subdev_state_get_format(state, format->pad, format->stream);
	if (!sink_fmt)
		return -EINVAL;

	*sink_fmt = format->format;

	for_each_active_route(&state->routing, route) {
		struct v4l2_mbus_framefmt *source_fmt;

		if (route->sink_pad != format->pad || route->sink_stream != format->stream)
			continue;

		source_fmt = v4l2_subdev_state_get_format(state, route->source_pad,
								 route->source_stream);
		if (!source_fmt)
			return -EINVAL;

		*source_fmt = format->format;
	}

	return 0;
}

static int max96724_init_state(struct v4l2_subdev *sd, struct v4l2_subdev_state *sd_state)
{
	struct max96724_priv *priv = container_of(sd, struct max96724_priv, sd);
	const struct v4l2_mbus_framefmt *default_format =
		priv->ali360_yh_profile ? &max96724_ali360_yh_format :
					      &max96724_default_format;
	struct v4l2_subdev_krouting routing = {};
	struct v4l2_subdev_route *routes;
	int i;
	int src_pad[2] = { MAX96724_SRC_PAD, MAX96724_SRC_PAD + 1 };

	routes = kcalloc(MAX96724_N_SINKS, sizeof(*routes), GFP_KERNEL);
	if (!routes)
		return -ENOMEM;

	for (i = 0; i < MAX96724_N_SINKS; i++) {
		struct v4l2_subdev_route *route = &routes[i];

		route->source_pad = (BIT(i) & priv->csi2_video_pipe_mask[0]) ? src_pad[0] :
									       src_pad[1];
		route->source_stream = i;
		route->sink_pad = i;
		route->sink_stream = 0;
		route->flags = V4L2_SUBDEV_ROUTE_FL_ACTIVE;
	}

	routing.num_routes = MAX96724_N_SINKS;
	routing.routes = routes;

	return v4l2_subdev_set_routing_with_fmt(sd, sd_state, &routing,
						 default_format);
}

static int max96724_set_routing(struct v4l2_subdev *sd, struct v4l2_subdev_state *state,
				enum v4l2_subdev_format_whence which,
				struct v4l2_subdev_krouting *routing)
{
	struct max96724_priv *priv = container_of(sd, struct max96724_priv, sd);
	const struct v4l2_mbus_framefmt *default_format =
		priv->ali360_yh_profile ? &max96724_ali360_yh_format :
					      &max96724_default_format;
	int ret;

	if (which == V4L2_SUBDEV_FORMAT_ACTIVE && media_entity_is_streaming(&sd->entity))
		return -EBUSY;

	ret = v4l2_subdev_routing_validate(sd, routing, V4L2_SUBDEV_ROUTING_ONLY_1_TO_1);
	if (ret)
		return ret;

	return v4l2_subdev_set_routing_with_fmt(sd, state, routing, default_format);
}

static struct v4l2_subdev *max96724_xlate_streams(struct max96724_priv *priv,
						  struct v4l2_subdev_state *state, u32 src_pad,
						  u64 src_streams, u32 sink_pad, u64 *sink_streams,
						  u32 *remote_pad)
{
	struct device *dev = &priv->client->dev;
	u64 streams;
	struct v4l2_subdev *remote_sd;
	struct media_pad *pad;

	streams = v4l2_subdev_state_xlate_streams(state, src_pad, sink_pad, &src_streams);
	if (!streams)
		dev_dbg(dev, "no streams found on sink pad\n");

	pad = media_pad_remote_pad_first(&priv->pads[sink_pad]);
	if (!pad) {
		dev_dbg(dev, "no remote pad found for sink pad\n");
		return ERR_PTR(-EPIPE);
	}

	remote_sd = media_entity_to_v4l2_subdev(pad->entity);
	if (!remote_sd) {
		dev_dbg(dev, "no entity connected to CSI2 input\n");
		return ERR_PTR(-EPIPE);
	}

	*sink_streams = streams;
	*remote_pad = pad->index;

	return remote_sd;
}

static void max96722_phy_relock_work(struct work_struct *work)
{
	struct max96724_priv *priv =
		container_of(to_delayed_work(work), struct max96724_priv,
			     phy_relock_work);
	struct device *dev = &priv->client->dev;
	int ret;

	mutex_lock(&priv->lock);

	if (!priv->enable_count)
		goto unlock;

	ret = max96724_phy_enable(priv, false);
	if (ret) {
		dev_err(dev, "failed to stop MAX96722 CSI PHY for re-lock: %d\n",
			ret);
		goto unlock;
	}

	msleep(50);

	ret = max96724_phy_enable(priv, true);
	if (ret)
		dev_err(dev, "failed to restart MAX96722 CSI PHY: %d\n",
			ret);

unlock:
	mutex_unlock(&priv->lock);
}

static int max96724_enable_streams(struct v4l2_subdev *sd, struct v4l2_subdev_state *state,
				   u32 src_pad, u64 streams_mask)
{
	struct max96724_priv *priv = container_of(sd, struct max96724_priv, sd);
	struct device *dev = &priv->client->dev;
	struct v4l2_subdev *remote_sd;
	bool first_enable;
	int i;
	int ret = 0;
	u32 remote_pad = 0;
	u64 sink_streams = 0;
	u64 sources_mask = streams_mask;

	mutex_lock(&priv->lock);

	first_enable = !priv->enable_count;
	if (first_enable) {
		ret = max96724_setup_all_pipes(priv, state);
		if (ret)
			goto unlock;

		ret = max96724_fsync_set(priv);
		if (ret)
			goto unlock;
	}

	while (true) {
		int pos = ffs(sources_mask) - 1;

		if (pos == -1)
			break;

		remote_sd = max96724_xlate_streams(priv, state, src_pad, BIT(pos), pos,
						   &sink_streams, &remote_pad);
		if (IS_ERR(remote_sd)) {
			ret = PTR_ERR(remote_sd);
			goto unlock;
		}

		ret = v4l2_subdev_enable_streams(remote_sd, remote_pad, 0x1);
		if (ret) {
			dev_err(dev, "failed to enable streams 0x%llx on '%s':%u: %d\n",
				sink_streams, remote_sd->name, remote_pad, ret);
			goto unlock;
		}

		sources_mask &= ~BIT(pos);
	}

	if (first_enable) {
		/*
		 * ALI360-YH is reset and released by the remote serializer above.
		 * Do not start the CSI output while its packet stream is changing:
		 * MAX96722 otherwise latches CMD_OVERFLOW0 and emits malformed
		 * packets until the next complete power/stream cycle.
		 */
		if (priv->ali360_yh_profile) {
			/*
			 * pipe_setup() initially enables both receive and transmit layers
			 * for every populated link.  Acquire the first packet lock on all
			 * cameras while CSI output is still disabled; afterwards DEV_REG4
			 * retains those receive locks and apply_active_camera() reduces the
			 * transmit layer and mappings to one VC0 stream.
			 */
			for (i = 0; i < MAX96724_N_GMSL; i++) {
				if (!(streams_mask & BIT(i)))
					continue;

				ret = max96724_wait_video_lock(priv, i);
				if (ret)
					goto unlock;
			}

			ret = max96724_apply_active_camera(priv);
			if (ret)
				goto unlock;

			/*
			 * The module starts with near-black exposure after its GPIO reset
			 * is released.  Keep the MAX96722 CSI output disabled for another
			 * 30 internal-FSYNC periods so the SC360AT ISP can converge before
			 * the first buffer is visible to the capture application.
			 */
			msleep(MAX96724_ALI360_AE_SETTLE_MS);

			regmap_write(priv->rmap, MAX96724_BACKTOP0_11, 0);
		}

		ret = max96724_phy_enable(priv, true);
		if (ret)
			goto unlock;
	}

	priv->enable_count++;

	/*
	 * The legacy i.MX8MP media graph starts the MAX96722 before the CSIS
	 * receiver.  Re-latch 2x4 mode only after this callback has returned so
	 * that the receiver is already listening when the clock lane restarts.
	 */
	if (max96722_phy_relock && first_enable && priv->is_max96722)
		schedule_delayed_work(&priv->phy_relock_work,
				      msecs_to_jiffies(100));

unlock:
	mutex_unlock(&priv->lock);

	return ret;
}

static int max96724_disable_streams(struct v4l2_subdev *sd, struct v4l2_subdev_state *state,
				    u32 src_pad, u64 streams_mask)
{
	struct max96724_priv *priv = container_of(sd, struct max96724_priv, sd);
	struct device *dev = &priv->client->dev;
	struct v4l2_subdev *remote_sd;
	u64 sink_streams = 0;
	u64 sources_mask = streams_mask;
	u32 remote_pad = 0;
	int ret = 0;

	mutex_lock(&priv->lock);

	if (!priv->enable_count)
		goto unlock;

	priv->enable_count--;

	/* disable cameras*/
	while (true) {
		int pos = ffs(sources_mask) - 1;

		if (pos == -1)
			break;

		remote_sd = max96724_xlate_streams(priv, state, src_pad, BIT(pos), pos,
						   &sink_streams, &remote_pad);
		if (IS_ERR(remote_sd)) {
			ret = PTR_ERR(remote_sd);
			goto unlock;
		}

		ret = v4l2_subdev_disable_streams(remote_sd, remote_pad, 0x1);
		if (ret) {
			dev_err(dev,
				"failed to disable streams 0x%llx on '%s':%u: %d\n",
				sink_streams, remote_sd->name, remote_pad, ret);
			goto unlock;
		}

		sources_mask &= ~BIT(pos);
	}

	if (!priv->enable_count) {
		cancel_delayed_work(&priv->phy_relock_work);
		max96724_phy_enable(priv, false);
		max96724_pipes_disable_all(priv);
	}

unlock:
	mutex_unlock(&priv->lock);

	return ret;
}

static int max96724_get_frame_desc(struct v4l2_subdev *sd, unsigned int pad,
				   struct v4l2_mbus_frame_desc *fd)
{
	struct max96724_priv *priv = container_of(sd, struct max96724_priv, sd);
	struct device *dev = &priv->client->dev;
	struct v4l2_subdev_state *state;
	struct v4l2_subdev_route *route;
	struct media_pad *remote_pad;
	int ret = 0;
	int i;

	if (pad < MAX96724_SRC_PAD)
		return -EINVAL;

	memset(fd, 0, sizeof(*fd));

	fd->type = V4L2_MBUS_FRAME_DESC_TYPE_CSI2;

	state = v4l2_subdev_lock_and_get_active_state(sd);

	for_each_active_route(&state->routing, route) {
		struct v4l2_mbus_frame_desc_entry *source_entry = NULL;
		struct v4l2_mbus_frame_desc source_fd = {0};

		if (route->source_pad != pad)
			continue;

		remote_pad = media_pad_remote_pad_first(&priv->pads[route->sink_pad]);
		if (!remote_pad) {
			dev_dbg(dev, "no remote pad found for sink pad\n");
			ret = -EPIPE;
			goto unlock_state;
		}

		ret = v4l2_subdev_call(priv->sources[route->sink_pad].sd, pad, get_frame_desc,
				       remote_pad->index, &source_fd);
		if (ret) {
			dev_err(dev, "Failed to get source frame desc for pad %u\n",
				route->sink_pad);

			goto unlock_state;
		}

		for (i = 0; i < source_fd.num_entries; i++) {
			if (source_fd.entry[i].stream == route->sink_stream) {
				source_entry = &source_fd.entry[i];
				break;
			}
		}

		if (!source_entry) {
			dev_err(dev, "Failed to find stream from source frame desc\n");

			ret = -EPIPE;
			goto unlock_state;
		}

		fd->entry[fd->num_entries].stream = route->source_stream;
		fd->entry[fd->num_entries].flags = source_entry->flags;
		fd->entry[fd->num_entries].length = source_entry->length;
		fd->entry[fd->num_entries].pixelcode = source_entry->pixelcode;

		/*
		 * TODO: Currently, the sink pad gives the VC number. But we need to fix this
		 * if we're using the 2nd CSI port as well as we will end up with VC0 and VC1 on
		 * CSI1 and VC2 and VC3 on CSI2. Should be VC0 and VC1 on CSI2.
		 */
		fd->entry[fd->num_entries].bus.csi2.vc = route->sink_pad;
		fd->entry[fd->num_entries].bus.csi2.dt = source_entry->bus.csi2.dt;

		fd->num_entries++;
	}

unlock_state:
	v4l2_subdev_unlock_state(state);

	return ret;
}

static int max96724_enum_mbus_code(struct v4l2_subdev *sd, struct v4l2_subdev_state *sd_state,
				   struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->pad >= MAX96724_SRC_PAD) {
		struct v4l2_mbus_framefmt *fmt;

		if (code->index > 0 || code->pad >= MAX96724_N_PADS)
			return -EINVAL;

		fmt = v4l2_subdev_state_get_opposite_stream_format(sd_state, code->pad,
								   code->stream);
		if (!fmt)
			return -EINVAL;

		code->code = fmt->code;

		return 0;
	}

	if (code->index >= ARRAY_SIZE(max96724_formats))
		return -EINVAL;

	code->code = max96724_formats[code->index].code;

	return 0;
}

static const struct v4l2_subdev_pad_ops max96724_v4l2_pad_ops = {
	.enum_mbus_code		= max96724_enum_mbus_code,
	.get_fmt		= v4l2_subdev_get_fmt,
	.set_fmt		= max96724_set_fmt,
	.set_routing		= max96724_set_routing,
	.get_frame_desc		= max96724_get_frame_desc,
	.get_frame_interval	= max96724_get_frame_interval,
	.set_frame_interval	= max96724_set_frame_interval,
	.enable_streams		= max96724_enable_streams,
	.disable_streams	= max96724_disable_streams,
};

static int max96724_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct max96724_priv *priv = container_of(sd, struct max96724_priv, sd);
	struct v4l2_subdev_state *state;
	u64 streams_mask;
	int ret;

	if (max96724_legacy_stream >= MAX96724_N_GMSL)
		return -EINVAL;

	streams_mask = priv->ali360_yh_profile ? priv->csi2_video_pipe_mask[0] :
						  BIT(max96724_legacy_stream);
	if (!streams_mask)
		return -ENOLINK;

	mutex_lock(&priv->legacy_lock);
	state = v4l2_subdev_lock_and_get_active_state(sd);
	if (enable) {
		if (priv->legacy_users++) {
			ret = 0;
		} else {
			ret = max96724_enable_streams(sd, state, MAX96724_SRC_PAD,
						      streams_mask);
			if (ret)
				priv->legacy_users--;
		}
	} else if (!priv->legacy_users) {
		ret = 0;
	} else if (--priv->legacy_users) {
		ret = 0;
	} else {
		ret = max96724_disable_streams(sd, state, MAX96724_SRC_PAD,
						       streams_mask);
	}
	v4l2_subdev_unlock_state(state);
	mutex_unlock(&priv->legacy_lock);

	return ret;
}

static const struct v4l2_subdev_video_ops max96724_v4l2_video_ops = {
	.s_stream = max96724_s_stream,
};

static ssize_t active_camera_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(to_i2c_client(dev));
	struct max96724_priv *priv = container_of(sd, struct max96724_priv, sd);

	return sysfs_emit(buf, "%u\n", READ_ONCE(priv->active_camera));
}

static ssize_t active_camera_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(to_i2c_client(dev));
	struct max96724_priv *priv = container_of(sd, struct max96724_priv, sd);
	unsigned int old_camera;
	unsigned int camera;
	int ret;

	ret = kstrtouint(buf, 0, &camera);
	if (ret)
		return ret;
	if (camera >= MAX96724_N_GMSL)
		return -ERANGE;

	mutex_lock(&priv->legacy_lock);
	if (!priv->ali360_yh_profile) {
		ret = -EOPNOTSUPP;
		goto unlock_legacy;
	}
	if (!(priv->gmsl_link_mask & BIT(camera)) ||
	    !(priv->csi2_video_pipe_mask[0] & BIT(camera))) {
		ret = -ENOLINK;
		goto unlock_legacy;
	}

	mutex_lock(&priv->lock);
	old_camera = priv->active_camera;
	if (camera == old_camera) {
		ret = 0;
	} else {
		priv->active_camera = camera;
		ret = priv->enable_count ? max96724_apply_active_camera(priv) : 0;
		if (ret) {
			priv->active_camera = old_camera;
			max96724_apply_active_camera(priv);
			if (priv->enable_count)
				max96724_wait_video_lock(priv, old_camera);
		}
	}
	mutex_unlock(&priv->lock);

unlock_legacy:
	mutex_unlock(&priv->legacy_lock);

	return ret ? ret : count;
}
static DEVICE_ATTR_RW(active_camera);

static struct attribute *max96724_attrs[] = {
	&dev_attr_active_camera.attr,
	NULL,
};

static const struct attribute_group max96724_attr_group = {
	.attrs = max96724_attrs,
};

static int max96724_s_power(struct v4l2_subdev *sd, int on)
{
	return 0;
}

static const struct v4l2_subdev_core_ops max96724_v4l2_core_ops = {
	.s_power = max96724_s_power,
};

static int max96724_notify_bound(struct v4l2_async_notifier *notifier,
				 struct v4l2_subdev *subdev,
				 struct v4l2_async_connection *asc)
{
	struct max96724_priv *priv = container_of(notifier->sd, struct max96724_priv, sd);
	struct max96724_asc *async_conn = container_of(asc, struct max96724_asc, base);
	struct max96724_source *source = async_conn->source;
	unsigned int index = to_index(priv, source);
	unsigned int src_pad;
	int ret;

	ret = media_entity_get_fwnode_pad(&subdev->entity, source->fwnode, MEDIA_PAD_FL_SOURCE);
	if (ret < 0) {
		dev_err(&priv->client->dev, "Failed to find pad for %s\n", subdev->name);
		return ret;
	}

	source->sd = subdev;
	src_pad = ret;

	ret = media_create_pad_link(&source->sd->entity, src_pad, &priv->sd.entity, index,
				    MEDIA_LNK_FL_ENABLED | MEDIA_LNK_FL_IMMUTABLE);
	if (ret) {
		dev_err(&priv->client->dev, "Unable to link %s:%u -> %s:%u\n",
			source->sd->name, src_pad, priv->sd.name, index);
		return ret;
	}

	dev_dbg(&priv->client->dev, "Bound %s pad: %u on index %u\n", subdev->name, src_pad, index);

	return 0;
}

static void max96724_notify_unbind(struct v4l2_async_notifier *notifier,
				   struct v4l2_subdev *subdev,
				   struct v4l2_async_connection *asc)
{
	struct max96724_asc *async_conn = container_of(asc, struct max96724_asc, base);
	struct max96724_source *source = async_conn->source;

	source->sd = NULL;
}

static const struct v4l2_subdev_ops max96724_v4l2_ops = {
	.core = &max96724_v4l2_core_ops,
	.video = &max96724_v4l2_video_ops,
	.pad = &max96724_v4l2_pad_ops,
};

static const struct v4l2_subdev_internal_ops max96724_internal_ops = {
	.init_state = max96724_init_state,
};

static const struct v4l2_async_notifier_operations max96724_notify_ops = {
	.bound = max96724_notify_bound,
	.unbind = max96724_notify_unbind,
};

static int max96724_v4l2_notifier_register(struct max96724_priv *priv)
{
	int i, ret;
	struct device *dev = &priv->client->dev;
	struct max96724_source *source = NULL;

	if (!priv->nsources)
		return 0;

	v4l2_async_subdev_nf_init(&priv->notifier, &priv->sd);

	for (i = 0; i < MAX96724_N_SINKS; i++) {
		source = &priv->sources[i];
		struct max96724_asc *asc;

		if (!source->fwnode)
			continue;

		asc = v4l2_async_nf_add_fwnode(&priv->notifier, source->fwnode,
					       struct max96724_asc);
		if (IS_ERR(asc)) {
			dev_err(dev, "Failed to add subdev for source %u: %ld", i, PTR_ERR(asc));
			v4l2_async_nf_cleanup(&priv->notifier);
			return PTR_ERR(asc);
		}

		asc->source = source;
	}

	priv->notifier.ops = &max96724_notify_ops;

	ret = v4l2_async_nf_register(&priv->notifier);
	if (ret) {
		dev_err(dev, "Failed to register subdev_notifier");
		v4l2_async_nf_cleanup(&priv->notifier);
		return ret;
	}

	return 0;
}

static void max96724_v4l2_notifier_unregister(struct max96724_priv *priv)
{
	if (!priv->nsources)
		return;

	v4l2_async_nf_unregister(&priv->notifier);
	v4l2_async_nf_cleanup(&priv->notifier);
}

static int max96724_v4l2_init(struct max96724_priv *priv)
{
	int ret, i;
	struct device *dev = &priv->client->dev;
	struct v4l2_subdev *sd = &priv->sd;
	static const s64 link_freq[] = {
		750000000UL,
	};

	v4l2_i2c_subdev_init(sd, priv->client, &max96724_v4l2_ops);
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_STREAMS;

	ret = v4l2_ctrl_handler_init(&priv->ctrl_handler, 1);
	if (ret < 0) {
		dev_err(dev, "Cannot initialize V4L2 ctrl handler.\n");
		return ret;
	}
	sd->ctrl_handler = &priv->ctrl_handler;
	sd->entity.function = MEDIA_ENT_F_VID_IF_BRIDGE;
	sd->entity.ops = &max96724_v4l2_media_ops;
	sd->internal_ops = &max96724_internal_ops;

	priv->pixrate_ctrl = v4l2_ctrl_new_std(&priv->ctrl_handler, &max96724_v4l2_ctrl_ops,
					       V4L2_CID_PIXEL_RATE, 1, INT_MAX, 1, 50000000);

	priv->link_freq_ctrl = v4l2_ctrl_new_int_menu(&priv->ctrl_handler, NULL, V4L2_CID_LINK_FREQ,
						      ARRAY_SIZE(link_freq) - 1, 0, link_freq);
	if (priv->link_freq_ctrl)
		priv->link_freq_ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	for (i = 0; i < MAX96724_N_PADS; i++)
		priv->pads[i].flags = i < MAX96724_SRC_PAD ?
				      MEDIA_PAD_FL_SINK : MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&priv->sd.entity, MAX96724_N_PADS, priv->pads);
	if (ret)
		goto error;

	ret = v4l2_subdev_init_finalize(&priv->sd);
	if (ret < 0)
		goto error_cleanup_entity;

	ret = max96724_v4l2_notifier_register(priv);
	if (ret) {
		dev_err(dev, "Unable to register v4l2 async notifiers\n");
		goto error_cleanup_entity;
	}

	ret = v4l2_async_register_subdev(&priv->sd);
	if (ret < 0) {
		dev_err(dev, "Unable to register subdevice\n");
		goto error_cleanup_entity;
	}

	return 0;

error_cleanup_entity:
	media_entity_cleanup(&priv->sd.entity);

error:
	v4l2_ctrl_handler_free(&priv->ctrl_handler);

	return ret;
}

static void max96724_v4l2_deinit(struct max96724_priv *priv)
{
	v4l2_async_unregister_subdev(&priv->sd);
	max96724_v4l2_notifier_unregister(priv);
	media_entity_cleanup(&priv->sd.entity);
	v4l2_ctrl_handler_free(&priv->ctrl_handler);
}

static int max96724_probe(struct i2c_client *client)
{
	struct max96724_priv *priv;
	struct device *dev = &client->dev;
	int ret;
	int chip_id;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->client = client;
	priv->allow_no_link = of_property_read_bool(dev->of_node,
						    "maxim,allow-no-link");
	priv->ali360_yh_profile = of_property_read_bool(dev->of_node,
						       "maxim,ali360-yh-profile");
	if (priv->ali360_yh_profile) {
		priv->interval.numerator = 1;
		priv->interval.denominator = 30;
	}

	mutex_init(&priv->lock);
	mutex_init(&priv->legacy_lock);
	INIT_DELAYED_WORK(&priv->phy_relock_work, max96722_phy_relock_work);

	priv->rmap = devm_regmap_init_i2c(client, &max96724_regmap_cfg);
	if (IS_ERR(priv->rmap)) {
		ret = PTR_ERR(priv->rmap);
		dev_err(dev, "Failed to allocate register map: %d\n", ret);
		return ret;
	}

	i2c_set_clientdata(client, priv);

	ret = devm_regulator_get_enable_optional(dev, "v12p0");
	if (ret < 0 && ret != -ENODEV)
		return ret;

	priv->reset_gpio = of_get_named_gpio(dev->of_node, "rst-gpios", 0);
	if (gpio_is_valid(priv->reset_gpio)) {
		ret = devm_gpio_request_one(dev, priv->reset_gpio, GPIOF_OUT_INIT_HIGH,
					    "max96724_mipi_reset");
		if (ret < 0) {
			dev_err(dev, "Failed to set reset pin\n");
			return ret;
		}
	}

	/* power-up completes in approx 2ms, according to specifications */
	usleep_range(2000, 2500);

	ret = regmap_read(priv->rmap, MAX96724_DEV_REG13, &chip_id);
	if (ret) {
		dev_err(dev, "Failed to read device id: %d\n", ret);
		return ret;
	}

	if (chip_id != MAX96722_DEV_ID && chip_id != MAX96724_DEV_ID) {
		dev_err(dev, "Unsupported Maxim deserializer detected: id 0x%x\n",
			chip_id);
		return -ENODEV;
	}
	priv->is_max96722 = chip_id == MAX96722_DEV_ID;

	dev_info(dev, "Detected MAX9672%c deserializer, id 0x%x\n",
		 chip_id == MAX96722_DEV_ID ? '2' : '4', chip_id);

	ret = max96724_i2c_parse_dt(priv);
	if (ret)
		return -ENODEV;

	ret = max96724_chip_init(priv);
	if (ret)
		return -ENODEV;

	ret = max96724_parse_dt(priv);
	if (ret)
		return ret;

	ret = max96724_dphy_config(priv);
	if (ret)
		return ret;

	ret = max96724_v4l2_init(priv);
	if (ret)
		return ret;

	ret = max96724_i2c_init(priv);
	if (ret)
		goto err_v4l2_deinit;

	ret = devm_device_add_group(dev, &max96724_attr_group);
	if (ret)
		goto err_i2c_mux;

	return 0;

err_i2c_mux:
	i2c_mux_del_adapters(priv->mux);
err_v4l2_deinit:
	max96724_v4l2_deinit(priv);
	return ret;
}

static void max96724_remove(struct i2c_client *client)
{
	struct v4l2_subdev *subdev = i2c_get_clientdata(client);
	struct max96724_priv *priv = container_of(subdev, struct max96724_priv, sd);

	cancel_delayed_work_sync(&priv->phy_relock_work);
	i2c_mux_del_adapters(priv->mux);
	max96724_v4l2_deinit(priv);
	mutex_destroy(&priv->lock);
}

static const struct of_device_id max96724_dt_ids[] = {
	{ .compatible = "maxim,max96722" },
	{ .compatible = "maxim,max96724" },
	{},
};
MODULE_DEVICE_TABLE(of, max96724_dt_ids);

static struct i2c_driver max96724_i2c_driver = {
	.driver	= {
		.name		= "max96724",
		.of_match_table	= of_match_ptr(max96724_dt_ids),
	},
	.probe		= max96724_probe,
	.remove		= max96724_remove,
};

module_i2c_driver(max96724_i2c_driver);

MODULE_DESCRIPTION("Maxim MAX96722/MAX96724 GMSL2/1 Deserializer Driver");
MODULE_AUTHOR("Laurentiu Palcu");
MODULE_LICENSE("GPL");
