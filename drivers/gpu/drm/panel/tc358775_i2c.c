/*
 * MYiR drm driver -  MIPI-DSI panel driver
 *
 * Copyright 2019 MYiR Devices
 *
 */

#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/jiffies.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/backlight.h>
#include <linux/fb.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/regulator/consumer.h>
#include <linux/mutex.h>
#include <linux/i2c.h>
#include <linux/of_device.h>
//#include <video/omapdss.h>
#include <video/mipi_display.h>
//#include <omap-panel-dsi85.h>

#include <drm/drm_crtc_helper.h>
#include <drm/drm_mipi_dsi.h>


#include <drm/drmP.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_edid.h>


#define TC3587755DEBUG

//static struct i2c_board_info tc358775_i2c_board_info = {
//	I2C_BOARD_INFO("tc358775_i2c_driver", 0x2d /*0x2c*/),
//};

struct tc358775_i2c_data {
	struct mutex xfer_lock;
	struct device_node *host_node;
	u8 channel_id;
};

//static struct i2c_client *g_tc358775_client = NULL;
static const struct i2c_device_id tc358775_i2c_id[] = {
	{ "toshiba,tc358775", 0 },
	{},
};

MODULE_DEVICE_TABLE(i2c, tc358775_i2c_id);

static struct of_device_id tc358775_dt_ids[] = {
	{ .compatible = "toshiba,tc358775", .data = NULL, },
	{ /* sentinel */ }
};

#define LVDS_CLK_FROM_DSI_CLK  1

#if 0
struct tc358775_lvds_timings {
    u16 hfp;
    u16 hsw;
    u16 hbp;
    u16 vfp;
    u16 vsw;
    u16 vbp;
};

static struct tc358775_lvds_timings lvds_timings = {
	.hfp = 40,
	.hsw = 128,
	.hbp = 40,
	.vfp = 1,
	.vsw = 4,
	.vbp = 9,    
};
#endif

struct tc358775_reg {
	/* Address and register value */
	u8 data[10];
	int len;
};

/*
static void tc358775_dumpconfig(struct i2c_client *client)
{
	struct i2c_client *tc358775_i2c_client = client;
	
    int val;
    val = i2c_smbus_read_byte_data(tc358775_i2c_client, 0x00);
    printk("@@@@@@@@@@@@@@@@@@@ val=%d\n", val);
	printk(KERN_INFO "tc358775-I2C: read 0x%02x  - 0x%02x\n", 0x00, i2c_smbus_read_byte_data(tc358775_i2c_client, 0x00));

	printk(KERN_INFO "tc358775-I2C: read 0x%02x  - 0x%02x\n", 0x01, \
	i2c_smbus_read_byte_data(tc358775_i2c_client, 0x01));

	printk(KERN_INFO "tc358775-I2C: read 0x%02x  - 0x%02x\n", 0x02, \
	i2c_smbus_read_byte_data(tc358775_i2c_client, 0x02));
	
	printk(KERN_INFO "tc358775-I2C: read 0x%02x  - 0x%02x\n", 0x03, \
	i2c_smbus_read_byte_data(tc358775_i2c_client, 0x03));
	printk(KERN_INFO "tc358775-I2C: read 0x%02x  - 0x%02x\n", 0x04, \
	i2c_smbus_read_byte_data(tc358775_i2c_client, 0x04));
	
	printk(KERN_INFO "tc358775-I2C: read 0x%02x  - 0x%02x\n", 0x05, \
	i2c_smbus_read_byte_data(tc358775_i2c_client, 0x05));
	

	printk(KERN_INFO "tc358775-I2C: read 0x%02x  - 0x%02x\n", 0x0D, \
		i2c_smbus_read_byte_data(tc358775_i2c_client, 0x0D));
	printk(KERN_INFO "tc358775-I2C: read 0x%02x  - 0x%02x\n", 0x0A, \
		i2c_smbus_read_byte_data(tc358775_i2c_client, 0x0A));
	printk(KERN_INFO "tc358775-I2C: read 0x%02x  - 0x%02x\n", 0x0B, \
		i2c_smbus_read_byte_data(tc358775_i2c_client, 0x0B));
	printk(KERN_INFO "tc358775-I2C: read 0x%02x  - 0x%02x\n", 0x10, \
		i2c_smbus_read_byte_data(tc358775_i2c_client, 0x10));
	printk(KERN_INFO "tc358775-I2C: read 0x%02x  - 0x%02x\n", 0x18, \
		i2c_smbus_read_byte_data(tc358775_i2c_client, 0x18));
	printk(KERN_INFO "tc358775-I2C: read 0x%02x  - 0x%02x\n", 0x1A, \
		i2c_smbus_read_byte_data(tc358775_i2c_client, 0x1A));
	printk(KERN_INFO "tc358775-I2C: read 0x%02x  - 0x%02x\n", 0x20, \
		i2c_smbus_read_byte_data(tc358775_i2c_client, 0x20));
	printk(KERN_INFO "tc358775-I2C: read 0x%02x  - 0x%02x\n", 0x21, \
		i2c_smbus_read_byte_data(tc358775_i2c_client, 0x21));
	printk(KERN_INFO "tc358775-I2C: read 0x%02x  - 0x%02x\n", 0x22, \
		i2c_smbus_read_byte_data(tc358775_i2c_client, 0x22));
	printk(KERN_INFO "tc358775-I2C: read 0x%02x  - 0x%02x\n", 0x23, \
		i2c_smbus_read_byte_data(tc358775_i2c_client, 0x23));
	printk(KERN_INFO "tc358775-I2C: read 0x%02x  - 0x%02x\n", 0x24, \
		i2c_smbus_read_byte_data(tc358775_i2c_client, 0x24));
	printk(KERN_INFO "tc358775-I2C: read 0x%02x  - 0x%02x\n", 0x25, \
		i2c_smbus_read_byte_data(tc358775_i2c_client, 0x25));
	printk(KERN_INFO "tc358775-I2C: read 0x%02x  - 0x%02x\n", 0x26, \
		i2c_smbus_read_byte_data(tc358775_i2c_client, 0x26));
	printk(KERN_INFO "tc358775-I2C: read 0x%02x  - 0x%02x\n", 0x27, \
		i2c_smbus_read_byte_data(tc358775_i2c_client, 0x27));
	printk(KERN_INFO "tc358775-I2C: read 0x%02x  - 0x%02x\n", 0x28, \
		i2c_smbus_read_byte_data(tc358775_i2c_client, 0x28));
	printk(KERN_INFO "tc358775-I2C: read 0x%02x  - 0x%02x\n", 0x29, \
		i2c_smbus_read_byte_data(tc358775_i2c_client, 0x29));
	printk(KERN_INFO "tc358775-I2C: read 0x%02x  - 0x%02x\n", 0x2A, \
		i2c_smbus_read_byte_data(tc358775_i2c_client, 0x2A));
	printk(KERN_INFO "tc358775-I2C: read 0x%02x  - 0x%02x\n", 0x2B, \
		i2c_smbus_read_byte_data(tc358775_i2c_client, 0x2B));
	printk(KERN_INFO "tc358775-I2C: read 0x%02x  - 0x%02x\n", 0x2C, \
		i2c_smbus_read_byte_data(tc358775_i2c_client, 0x2C));
	printk(KERN_INFO "tc358775-I2C: read 0x%02x  - 0x%02x\n", 0x2D, \
		i2c_smbus_read_byte_data(tc358775_i2c_client, 0x2D));
	printk(KERN_INFO "tc358775-I2C: read 0x%02x  - 0x%02x\n", 0x2E, \
		i2c_smbus_read_byte_data(tc358775_i2c_client, 0x2E));
	printk(KERN_INFO "tc358775-I2C: read 0x%02x  - 0x%02x\n", 0x2F, \
		i2c_smbus_read_byte_data(tc358775_i2c_client, 0x2F));
	printk(KERN_INFO "tc358775-I2C: read 0x%02x  - 0x%02x\n", 0x30, \
		i2c_smbus_read_byte_data(tc358775_i2c_client, 0x30));
	printk(KERN_INFO "tc358775-I2C: read 0x%02x  - 0x%02x\n", 0x31, \
		i2c_smbus_read_byte_data(tc358775_i2c_client, 0x31));
	printk(KERN_INFO "tc358775-I2C: read 0x%02x  - 0x%02x\n", 0x32, \
		i2c_smbus_read_byte_data(tc358775_i2c_client, 0x32));
	printk(KERN_INFO "tc358775-I2C: read 0x%02x  - 0x%02x\n", 0x33, \
		i2c_smbus_read_byte_data(tc358775_i2c_client, 0x33));
	printk(KERN_INFO "tc358775-I2C: read 0x%02x  - 0x%02x\n", 0x34, \
		i2c_smbus_read_byte_data(tc358775_i2c_client, 0x34));
	printk(KERN_INFO "tc358775-I2C: read 0x%02x  - 0x%02x\n", 0x35, \
		i2c_smbus_read_byte_data(tc358775_i2c_client, 0x35));
	printk(KERN_INFO "tc358775-I2C: read 0x%02x  - 0x%02x\n", 0x36, \
		i2c_smbus_read_byte_data(tc358775_i2c_client, 0x32));
	printk(KERN_INFO "tc358775-I2C: read 0x%02x  - 0x%02x\n", 0x37, \
		i2c_smbus_read_byte_data(tc358775_i2c_client, 0x33));
	printk(KERN_INFO "tc358775-I2C: read 0x%02x  - 0x%02x\n", 0x38, \
		i2c_smbus_read_byte_data(tc358775_i2c_client, 0x34));
	printk(KERN_INFO "tc358775-I2C: read 0x%02x  - 0x%02x\n", 0x39, \
		i2c_smbus_read_byte_data(tc358775_i2c_client, 0x35));
	printk(KERN_INFO "tc358775-I2C: read 0x%02x  - 0x%02x\n", 0x3A, \
		i2c_smbus_read_byte_data(tc358775_i2c_client, 0x35));
	printk(KERN_INFO "tc358775-I2C: read 0x%02x  - 0x%02x\n", 0x3B, \
		i2c_smbus_read_byte_data(tc358775_i2c_client, 0x3B));
}
*/

static int tc358775_i2c_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
#if 0
	struct tc358775_i2c_data *tc358775;
	struct device *dev = &client->dev;

	printk(KERN_INFO ">>>>>>>>>>>>>>>>>>>>>>> tc358775-I2C: Probe called!\n");
	tc358775 = kzalloc(sizeof(struct tc358775_i2c_data), GFP_KERNEL);

	if (!tc358775) {
		printk(KERN_ERR "tc358775-I2C: memory allocation failed!\n");
		return -ENOMEM;
	}
    g_tc358775_client = client;
	mutex_init(&tc358775->xfer_lock);
	i2c_set_clientdata(client, tc358775);

   tc358775_dumpconfig(client);
#endif
	return 0;
}

static int tc358775_i2c_remove(struct i2c_client *client)
{
	struct tc358775_i2c_data *tc358775_i2c_data =
					i2c_get_clientdata(client);
	kfree(tc358775_i2c_data);
	return 0;
}

static struct i2c_driver tc358775_i2c_driver = {
	.driver = {
		.name	= "tc358775_i2c_driver",
		.owner = THIS_MODULE,
		.of_match_table = tc358775_dt_ids,
	},
	.probe		= tc358775_i2c_probe,
	.remove		= tc358775_i2c_remove,
	.id_table	= tc358775_i2c_id,
};


module_i2c_driver(tc358775_i2c_driver);
MODULE_AUTHOR("MYiR alex.hu");
MODULE_DESCRIPTION("TC358775 MIPI DSI to LVDS  driver");
MODULE_LICENSE("GPL");
