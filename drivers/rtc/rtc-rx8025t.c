/*
 * Driver for Epson's RTC module RX-8025 SA/NB
 *
 * Copyright (C) 2009 Wolfgang Grandegger <wg@grandegger.com>
 *
 * Copyright (C) 2005 by Digi International Inc.
 * All rights reserved.
 *
 * Modified by fengjh at rising.com.cn
 * <http://lists.lm-sensors.org/mailman/listinfo/lm-sensors>
 * 2006.11
 *
 * Code cleanup by Sergei Poselenov, <sposelenov@emcraft.com>
 * Converted to new style by Wolfgang Grandegger <wg@grandegger.com>
 * Alarm and periodic interrupt added by Dmitry Rakhchev <rda@emcraft.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/bcd.h>
#include <linux/i2c.h>
#include <linux/list.h>
#include <linux/rtc.h>
#include <linux/of.h>
#include <linux/of_device.h>

/* Register definitions */
#define RX8025_REG_SEC		0x00
#define RX8025_REG_MIN		0x01
#define RX8025_REG_HOUR		0x02
#define RX8025_REG_WDAY		0x03
#define RX8025_REG_MDAY		0x04
#define RX8025_REG_MONTH	0x05
#define RX8025_REG_YEAR		0x06
#define RX8025_REG_RAM		0x07
#define RX8025_REG_ALWMIN	0x08
#define RX8025_REG_ALWHOUR	0x09
#define RX8025_REG_ALWWDAY	0x0a
#define RX8025_REG_TC0		0x0b
#define RX8025_REG_TC1		0x0c
#define RX8025_REG_EXT		0x0d

#define RX8025_REG_FLAG		0x0e
#define RX8025_REG_CTRL		0x0f

#define RX8025_BIT_EXT_TSEL0	(1 << 0)
#define RX8025_BIT_EXT_TSEL1	(1 << 1)
#define RX8025_BIT_EXT_FSEL0	(1 << 2)
#define RX8025_BIT_EXT_FSEL1	(1 << 3)
#define RX8025_BIT_EXT_TE		(1 << 4)
#define RX8025_BIT_EXT_USEL		(1 << 5)
#define RX8025_BIT_EXT_WADA		(1 << 6)
#define RX8025_BIT_EXT_TEST		(1 << 7)

#define RX8025_BIT_FLAG_VDET	(1 << 0)
#define RX8025_BIT_FLAG_VLF		(1 << 1)
#define RX8025_BIT_FLAG_AF		(1 << 3)
#define RX8025_BIT_FLAG_TF		(1 << 4)
#define RX8025_BIT_FLAG_UF		(1 << 5)

#define RX8025_BIT_CTRL_RESET	(1 << 0)
#define RX8025_BIT_CTRL_AIE		(1 << 3)
#define RX8025_BIT_CTRL_TIE		(1 << 4)
#define RX8025_BIT_CTRL_UIE		(1 << 5)
#define RX8025_BIT_CTRL_CSEL0	(1 << 6)
#define RX8025_BIT_CTRL_CSEL1	(1 << 7)

static const struct i2c_device_id rx8025_id[] = {
	{ "rx8025t", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, rx8025_id);

struct rx8025_data {
	struct i2c_client *client;
	struct rtc_device *rtc;
	u8 flag;
};

static int rx8025_read_reg(struct i2c_client *client, int number, u8 *value)
{
	int ret = i2c_smbus_read_byte_data(client, number);

	if (ret < 0) {
		dev_err(&client->dev, "Unable to read register #%d\n", number);
		return ret;
	}

	*value = ret;
	return 0;
}

static int rx8025_read_regs(struct i2c_client *client,
			    int number, u8 length, u8 *values)
{
	int ret = i2c_smbus_read_i2c_block_data(client, number,	length, values);

	if (ret != length) {
		dev_err(&client->dev, "Unable to read registers #%d..#%d\n",
			number, number + length - 1);
		return ret < 0 ? ret : -EIO;
	}

	return 0;
}

static int rx8025_write_reg(struct i2c_client *client, int number, u8 value)
{
	int ret = i2c_smbus_write_byte_data(client, number, value);

	if (ret)
		dev_err(&client->dev, "Unable to write register #%d\n",
			number);

	return ret;
}

static int rx8025_write_regs(struct i2c_client *client,
			     int number, u8 length, u8 *values)
{
	int ret = i2c_smbus_write_i2c_block_data(client, number, length, values);

	if (ret)
		dev_err(&client->dev, "Unable to write registers #%d..#%d\n",
			number, number + length - 1);

	return ret;
}

static int rx8025_get_time(struct device *dev, struct rtc_time *dt)
{
	struct rx8025_data *rx8025 = dev_get_drvdata(dev);
	u8 date[7];
	int err;

	err = rx8025_read_regs(rx8025->client, RX8025_REG_SEC, 7, date);
	if (err)
		return err;

	dev_dbg(dev, "%s: read 0x%02x 0x%02x "
		"0x%02x 0x%02x 0x%02x 0x%02x 0x%02x\n", __func__,
		date[0], date[1], date[2], date[3], date[4],
		date[5], date[6]);

	dt->tm_sec = bcd2bin(date[RX8025_REG_SEC] & 0x7f);
	dt->tm_min = bcd2bin(date[RX8025_REG_MIN] & 0x7f);
	dt->tm_hour = bcd2bin(date[RX8025_REG_HOUR] & 0x3f);
	dt->tm_mday = bcd2bin(date[RX8025_REG_MDAY] & 0x3f);
	dt->tm_mon = bcd2bin(date[RX8025_REG_MONTH] & 0x1f)-1;
	dt->tm_year = bcd2bin(date[RX8025_REG_YEAR])+100;

	dev_dbg(dev, "%s: date %ds %dm %dh %dmd %dm %dy\n", __func__,
		dt->tm_sec, dt->tm_min, dt->tm_hour,
		dt->tm_mday, dt->tm_mon, dt->tm_year);

	return rtc_valid_tm(dt);
}

static int rx8025_set_time(struct device *dev, struct rtc_time *dt)
{
	struct rx8025_data *rx8025 = dev_get_drvdata(dev);
	u8 date[7];

	/*
	 * BUG: The HW assumes every year that is a multiple of 4 to be a leap
	 * year.  Next time this is wrong is 2100, which will not be a leap
	 * year.
	 */

	/*
	 * Here the read-only bits are written as "0".  I'm not sure if that
	 * is sound.
	 */
	date[RX8025_REG_SEC] = bin2bcd(dt->tm_sec);
	date[RX8025_REG_MIN] = bin2bcd(dt->tm_min);
	date[RX8025_REG_HOUR] = bin2bcd(dt->tm_hour);
	date[RX8025_REG_WDAY] = bin2bcd(1 << dt->tm_wday);
	date[RX8025_REG_MDAY] = bin2bcd(dt->tm_mday);
	date[RX8025_REG_MONTH] = bin2bcd(dt->tm_mon+1);
	date[RX8025_REG_YEAR] = bin2bcd(dt->tm_year-100);

	dev_dbg(dev,
		"%s: write 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x\n",
		__func__,
		date[0], date[1], date[2], date[3], date[4], date[5], date[6]);

	return rx8025_write_regs(rx8025->client, RX8025_REG_SEC, 7, date);
}

static int rx8025_init_client(struct i2c_client *client, int *need_reset)
{
	struct rx8025_data *rx8025 = i2c_get_clientdata(client);
	u8 flag;
	int err;

	err = rx8025_read_reg(rx8025->client, RX8025_REG_FLAG, &flag);
	if (err)
		goto out;

	if (flag & RX8025_BIT_FLAG_VLF) {
		dev_warn(&client->dev, "power-on reset was detected, "
			 "you may have to readjust the clock\n");
		*need_reset = 1;
	}

	if (flag & RX8025_BIT_FLAG_VDET) {
		dev_warn(&client->dev, "a power voltage drop was detected, "
			 "you may have to readjust the clock\n");
		*need_reset = 1;
	}

	if (*need_reset) {
		flag &= ~(RX8025_BIT_FLAG_VLF | RX8025_BIT_FLAG_VDET);
		err = rx8025_write_reg(client, RX8025_REG_FLAG, flag);
	}
out:
	return err;
}

/* only have base function, alarm function is not implemented yet */
static struct rtc_class_ops rx8025_rtc_ops = {
	.read_time = rx8025_get_time,
	.set_time = rx8025_set_time,
};

static int rx8025_get_ram(struct device *dev, char *pval)
{
	struct i2c_client *client = to_i2c_client(dev);
	int err;

	err = rx8025_read_reg(client, RX8025_REG_RAM, pval);
	if (err)
		return err;

	return 0;
}

static int rx8025_set_ram(struct device *dev, const char val)
{
	struct i2c_client *client = to_i2c_client(dev);
	int err;

	err = rx8025_write_reg(client, RX8025_REG_RAM, val);
	if (err)
		return err;

	dev_dbg(dev, "%s: write ram: %02x\n", __func__, val);

	return 0;
}

static ssize_t rx8025_sysfs_show_ram(struct device *dev,
					      struct device_attribute *attr,
					      char *buf)
{
	int err;
	char val = 0;

	err = rx8025_get_ram(dev, &val);
	if (err)
		return err;

	return sprintf(buf, "%02x\n", val);
}

static ssize_t rx8025_sysfs_store_ram(struct device *dev,
					       struct device_attribute *attr,
					       const char *buf, size_t count)
{
	int err;

	if (count > 1) {
		return -EINVAL;
	}
	
	err = rx8025_set_ram(dev, buf[0]);

	return err ? err : count;
}

static DEVICE_ATTR(ram, S_IRUGO | S_IWUSR,
		   rx8025_sysfs_show_ram,
		   rx8025_sysfs_store_ram);

static int rx8025_sysfs_register(struct device *dev)
{
	return device_create_file(dev, &dev_attr_ram);
}

static void rx8025_sysfs_unregister(struct device *dev)
{
	device_remove_file(dev, &dev_attr_ram);
}

static int rx8025_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	struct rx8025_data *rx8025;
	int err, need_reset = 0;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA
				     | I2C_FUNC_SMBUS_I2C_BLOCK)) {
		dev_err(&adapter->dev,
			"doesn't support required functionality\n");
		err = -EIO;
		goto errout;
	}

	rx8025 = devm_kzalloc(&client->dev, sizeof(*rx8025), GFP_KERNEL);
	if (!rx8025) {
		dev_err(&adapter->dev, "failed to alloc memory\n");
		err = -ENOMEM;
		goto errout;
	}

	rx8025->client = client;
	i2c_set_clientdata(client, rx8025);

	err = rx8025_init_client(client, &need_reset);
	if (err)
		goto errout;

	if (need_reset) {
		struct rtc_time tm;
		dev_info(&client->dev,
			 "bad conditions detected, resetting date\n");
		rtc_time_to_tm(0, &tm);	/* 1970/1/1 */
		rx8025_set_time(&client->dev, &tm);
	}

	rx8025->rtc = devm_rtc_device_register(&client->dev, client->name,
					  &rx8025_rtc_ops, THIS_MODULE);
	if (IS_ERR(rx8025->rtc)) {
		err = PTR_ERR(rx8025->rtc);
		dev_err(&client->dev, "unable to register the class device\n");
		goto errout;
	}
	
	rx8025->rtc->max_user_freq = 1;

	err = rx8025_sysfs_register(&client->dev);
	if (err)
		goto errout;

	return 0;
	
errout:
	dev_err(&adapter->dev, "probing for rx8025 failed\n");
	return err;
}

static int rx8025_remove(struct i2c_client *client)
{
	rx8025_sysfs_unregister(&client->dev);
	return 0;
}

static const struct of_device_id __maybe_unused rx8025_dt_ids[] = {
        { .compatible = "epson,rx8025t",        .data = 0, },
        { }
};
MODULE_DEVICE_TABLE(of, rx8025_dt_ids);

static struct i2c_driver rx8025_driver = {
	.driver = {
		.name = "rtc-rx8025t",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(rx8025_dt_ids),
	},
	.probe		= rx8025_probe,
	.remove		= rx8025_remove,
	.id_table	= rx8025_id,
};

module_i2c_driver(rx8025_driver);

MODULE_AUTHOR("Kevin Su <kevin.su@myirtech.com>");
MODULE_DESCRIPTION("RX-8025T RTC driver");
MODULE_LICENSE("GPL");
