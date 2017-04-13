/* 
 * drivers/input/touchscreen/ft5x0x_ts.c
 *
 * FocalTech ft5x0x TouchScreen driver. 
 *
 * Copyright (c) 2010  Focal tech Ltd.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *
 *    note: only support mulititouch    Wenfs 2010-10-01
 */

#include <linux/module.h>
#include <linux/hrtimer.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/platform_device.h>
 
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif

#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_device.h>
#include <linux/input/edt-ft5x06.h>

#if 0
#define DEBUG 	1
#define DEBUG_0 1
#endif
 
#ifdef DEBUG_0
#define TS_DEBUG(fmt,args...) printk(fmt, ##args )
#else
#define TS_DEBUG(fmt,args...)
#endif
 
#ifdef DEBUG
#define TS_DEBUG1(fmt,args...) printk(fmt, ##args )
#else
#define TS_DEBUG1(fmt,args...)
#endif
 
static struct i2c_client *this_client;
static struct ft5x0x_ts_platform_data *ts_plat_data;
 
struct ts_event {
    u16    x1;
    u16    y1;
    u16    x2;
    u16    y2;
    u16    x3;
    u16    y3;
    u16    x4;
    u16    y4;
    u16    x5;
    u16    y5;
    u16    pressure;
    s16 touch_ID1;
    s16 touch_ID2;
    s16 touch_ID3;
    s16 touch_ID4;
    s16 touch_ID5;
    u8  touch_point;
};
 
struct ft5x0x_ts_data {
    struct input_dev    *input_dev;
    struct ts_event        event;

    struct delayed_work     poll_work;
    int stop_poll_flag;
    struct work_struct     pen_event_work;
    struct workqueue_struct *ts_workqueue;
	
#ifdef CONFIG_HAS_EARLYSUSPEND
    struct early_suspend    early_suspend;
#endif
};
 
static int ft5x0x_i2c_rxdata(char *rxdata, int length)
{
    int ret;
 
    struct i2c_msg msgs[] = {
        {
            .addr    = this_client->addr,
            .flags    = 0,
            .len    = 1,
            .buf    = rxdata,
        },
        {
            .addr    = this_client->addr,
            .flags    = I2C_M_RD,
            .len    = length,
            .buf    = rxdata,
        },
    };
 
    //msleep(1);
    ret = i2c_transfer(this_client->adapter, msgs, 2);
    if (ret < 0)
        pr_err("msg %s i2c read error: %d\n", __func__, ret);
    
    return ret;
}

static void ft5x0x_ts_release(void)
{
    struct ft5x0x_ts_data *data = i2c_get_clientdata(this_client);
    //TS_DEBUG("ft5x0x_ts_release\n");

	if (ts_plat_data->multi_touch) {
		input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR, 0);
		input_report_abs(data->input_dev, ABS_MT_WIDTH_MAJOR, 0);
	} else {
		input_report_abs(data->input_dev, ABS_PRESSURE, 0);
		input_report_key(data->input_dev, BTN_TOUCH, 0);
	}
    input_sync(data->input_dev);
}
 
static int ft5x0x_read_data(void)
{
    struct ft5x0x_ts_data *data = i2c_get_clientdata(this_client);
    struct ts_event *event = &data->event;
    u8 buf[32] = {0};
    int ret = -1;
    int status = 0;

	if (ts_plat_data->multi_touch) {
		ret = ft5x0x_i2c_rxdata(buf, 31);
	} else {
		ret = ft5x0x_i2c_rxdata(buf, 7);
	}
    if (ret < 0) {
        printk("%s read_data i2c_rxdata failed: %d\n", __func__, ret);
        return ret;
    }
 
    memset(event, 0, sizeof(struct ts_event));
    event->touch_point = buf[2] & 0x07;// 000 0111
 
    if (event->touch_point == 0) {
        ft5x0x_ts_release();
        return 1; 
    }

	if (ts_plat_data->multi_touch) {
		switch (event->touch_point) {
		case 5:
			event->x5 = (s16)(buf[0x1b] & 0x0F)<<8 | (s16)buf[0x1c];
			event->y5 = (s16)(buf[0x1d] & 0x0F)<<8 | (s16)buf[0x1e];
			status = (s16)((buf[0x1b] & 0xc0) >> 6);
			event->touch_ID5=(s16)(buf[0x1D] & 0xF0)>>4;
			if (status == 1) {
				ft5x0x_ts_release();
			}
		case 4:
			event->x4 = (s16)(buf[0x15] & 0x0F)<<8 | (s16)buf[0x16];
			event->y4 = (s16)(buf[0x17] & 0x0F)<<8 | (s16)buf[0x18];
			status = (s16)((buf[0x15] & 0xc0) >> 6);
			event->touch_ID4=(s16)(buf[0x17] & 0xF0)>>4;
			if (status == 1) {
				ft5x0x_ts_release();
			}
		case 3:
			event->x3 = (s16)(buf[0x0f] & 0x0F)<<8 | (s16)buf[0x10];
			event->y3 = (s16)(buf[0x11] & 0x0F)<<8 | (s16)buf[0x12];
			status = (s16)((buf[0x0f] & 0xc0) >> 6);
			event->touch_ID3=(s16)(buf[0x11] & 0xF0)>>4;
			if (status == 1) {
				ft5x0x_ts_release();
			}
		case 2:
			event->x2 = (s16)(buf[9] & 0x0F)<<8 | (s16)buf[10];
			event->y2 = (s16)(buf[11] & 0x0F)<<8 | (s16)buf[12];
			status = (s16)((buf[0x9] & 0xc0) >> 6);
			event->touch_ID2=(s16)(buf[0x0b] & 0xF0)>>4;
			if (status == 1) {
				ft5x0x_ts_release();
			}
		case 1:
			event->x1 = (s16)(buf[3] & 0x0F)<<8 | (s16)buf[4];
			event->y1 = (s16)(buf[5] & 0x0F)<<8 | (s16)buf[6];
			status = (s16)((buf[0x3] & 0xc0) >> 6);
			event->touch_ID1=(s16)(buf[0x05] & 0xF0)>>4;
			if (status == 1) {
				ft5x0x_ts_release();
			}
			break;
		default:
			return -1;
		}
	} else {
		if (event->touch_point == 1) {
			event->x1 = (s16)(buf[3] & 0x0F)<<8 | (s16)buf[4];
			event->y1 = (s16)(buf[5] & 0x0F)<<8 | (s16)buf[6];
		}
	}
    event->pressure = 200;

    dev_dbg(&this_client->dev, "%s: 1:%d %d 2:%d %d \n", __func__,
			event->x1, event->y1, event->x2, event->y2);
 
    return 0;
}
 
static void ft5x0x_report_value(void)
{
    struct ft5x0x_ts_data *data = i2c_get_clientdata(this_client);
    struct ts_event *event = &data->event;
 
	TS_DEBUG("==ft5x0x_report_value =\n");

	if (ts_plat_data->multi_touch) {
		switch(event->touch_point) {
		case 5:
			input_report_abs(data->input_dev, ABS_MT_TRACKING_ID, event->touch_ID5);
			input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR, event->pressure);
			input_report_abs(data->input_dev, ABS_MT_POSITION_X, event->x5);
			input_report_abs(data->input_dev, ABS_MT_POSITION_Y, event->y5);
			input_report_abs(data->input_dev, ABS_MT_WIDTH_MAJOR, 1);
			input_mt_sync(data->input_dev);
			TS_DEBUG("===x2 = %d,y2 = %d ====\n",event->x2,event->y2);
		case 4:
			input_report_abs(data->input_dev, ABS_MT_TRACKING_ID, event->touch_ID4);
			input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR, event->pressure);
			input_report_abs(data->input_dev, ABS_MT_POSITION_X, event->x4);
			input_report_abs(data->input_dev, ABS_MT_POSITION_Y, event->y4);
			input_report_abs(data->input_dev, ABS_MT_WIDTH_MAJOR, 1);
			input_mt_sync(data->input_dev);
			TS_DEBUG("===x2 = %d,y2 = %d ====\n",event->x2,event->y2);
		case 3:
			input_report_abs(data->input_dev, ABS_MT_TRACKING_ID, event->touch_ID3);
			input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR, event->pressure);
			input_report_abs(data->input_dev, ABS_MT_POSITION_X, event->x3);
			input_report_abs(data->input_dev, ABS_MT_POSITION_Y, event->y3);
			input_report_abs(data->input_dev, ABS_MT_WIDTH_MAJOR, 1);
			input_mt_sync(data->input_dev);
			TS_DEBUG("===x2 = %d,y2 = %d ====\n",event->x2,event->y2);
		case 2:
			input_report_abs(data->input_dev, ABS_MT_TRACKING_ID, event->touch_ID2);
			input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR, event->pressure);
			input_report_abs(data->input_dev, ABS_MT_POSITION_X, event->x2);
			input_report_abs(data->input_dev, ABS_MT_POSITION_Y, event->y2);
			input_report_abs(data->input_dev, ABS_MT_WIDTH_MAJOR, 1);
			input_mt_sync(data->input_dev);
			TS_DEBUG("===x2 = %d,y2 = %d ====\n",event->x2,event->y2);
		case 1:
			input_report_abs(data->input_dev, ABS_MT_TRACKING_ID, event->touch_ID1);
			input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR, event->pressure);
			input_report_abs(data->input_dev, ABS_MT_POSITION_X, event->x1);
			input_report_abs(data->input_dev, ABS_MT_POSITION_Y, event->y1);
			input_report_abs(data->input_dev, ABS_MT_WIDTH_MAJOR, 1);
			input_mt_sync(data->input_dev);
		default:
			TS_DEBUG("==touch_point default =\n");
			break;
		}
	} else {
		if (event->touch_point == 1) {
			input_report_abs(data->input_dev, ABS_X, event->x1);
			input_report_abs(data->input_dev, ABS_Y, event->y1);
			input_report_abs(data->input_dev, ABS_PRESSURE, event->pressure);
		}
		input_report_key(data->input_dev, BTN_TOUCH, 1);
	}
    input_sync(data->input_dev);
 
    dev_dbg(&this_client->dev, "%s: 1:%d %d 2:%d %d \n", __func__,
			event->x1, event->y1, event->x2, event->y2);
    TS_DEBUG1("1:(%d, %d) 2:(%d, %d) 3:(%d, %d) 4:(%d, %d) 5:(%d, %d)\n", 
			  event->x1, event->y1, event->x2, event->y2, event->x3, event->y3,
			  event->x4, event->y4, event->x5, event->y5);
}    /*end ft5x0x_report_value*/

/* Added by Kevin, test only */
static void ft5x0x_poll_work(struct work_struct *work)
{
    int ret = -1;
	struct delayed_work* dwork = to_delayed_work(work);
	struct ft5x0x_ts_data *ts_data = container_of(dwork, struct ft5x0x_ts_data, poll_work);

    ret = ft5x0x_read_data();
    if (ret == 0) {
        ft5x0x_report_value();
    }

    if (!ts_data->stop_poll_flag) {
        queue_delayed_work(ts_data->ts_workqueue, &ts_data->poll_work, 2);
    } else {
        printk(KERN_ERR"stop polling...\n");
    }
}

static void ft5x0x_ts_pen_irq_work(struct work_struct *work)
{
    int ret = -1;
 
    TS_DEBUG("==work 1=\n");
    ret = ft5x0x_read_data();    
    if (ret == 0) {    
        ft5x0x_report_value();
    }
    else
        TS_DEBUG("data package read error\n");
    TS_DEBUG("==work 2=\n");
}

static irqreturn_t ft5x0x_ts_interrupt(int irq, void *dev_id)
{
    struct ft5x0x_ts_data *ft5x0x_ts = dev_id;
 
    TS_DEBUG("==int ft5x0x_ts_interrupt=\n");
    if (!work_pending(&ft5x0x_ts->pen_event_work)) {
        queue_work(ft5x0x_ts->ts_workqueue, &ft5x0x_ts->pen_event_work);
    }
 
    return IRQ_HANDLED;
}
 
#ifdef CONFIG_HAS_EARLYSUSPEND
static void ft5x0x_ts_suspend(struct early_suspend *handler)
{
	//    struct ft5x0x_ts_data *ts;
	//    ts =  container_of(handler, struct ft5x0x_ts_data, early_suspend);
 
    TS_DEBUG("==ft5x0x_ts_suspend=\n");
	//    disable_irq(this_client->irq);
	//    disable_irq(IRQ_EINT(6));
	//    cancel_work_sync(&ts->pen_event_work);
	//    flush_workqueue(ts->ts_workqueue);
    // ==set mode ==, 
	//        ft5x0x_set_reg(FT5X0X_REG_PMODE, PMODE_HIBERNATE);
}
 
static void ft5x0x_ts_resume(struct early_suspend *handler)
{
    TS_DEBUG("==ft5x0x_ts_resume=\n");
    // wake the mode
	//    __gpio_as_output(GPIO_FT5X0X_WAKE);        
	//    __gpio_clear_pin(GPIO_FT5X0X_WAKE);        //set wake = 0,base on system
	//     msleep(100);
	//    __gpio_set_pin(GPIO_FT5X0X_WAKE);            //set wake = 1,base on system
	//    msleep(100);
	//    enable_irq(this_client->irq);
	//    enable_irq(IRQ_EINT(6));
}
#endif  //CONFIG_HAS_EARLYSUSPEND

/* Parse the ft5x0x from device tree, MYiR */
#if defined(CONFIG_OF)
static const struct of_device_id ft5x0x_ts_match[];

static struct ft5x0x_ts_platform_data *ft5x0x_parse_dt(struct device *dev)
{
	struct ft5x0x_ts_platform_data *pdata;
	struct device_node *np = dev->of_node;
	const struct of_device_id *match;

	match = of_match_device(of_match_ptr(ft5x0x_ts_match), dev);
	if (!match)
		return ERR_PTR(-EINVAL);

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return ERR_PTR(-ENOMEM);
	
    if (of_property_read_u8(np, "polling_mode", &pdata->polling_mode)) {
		dev_err(dev, "Failed to get polling_mode property, set to 0\n");
		pdata->polling_mode = 0;
	}
	
    if (of_property_read_u8(np, "multi_touch", &pdata->multi_touch)) {
		dev_err(dev, "Failed to get multi_touch property, set to 0\n");
		pdata->multi_touch = 0;
	}

	pdata->tp_int = of_get_named_gpio_flags(np, "tp_int", 0, NULL);
	if (gpio_is_valid(pdata->tp_int)) {	
		//printk("tp int gpio=%d\n", pdata->tp_int);
		if (gpio_request(pdata->tp_int, "touch irq")) {
			printk("request touch gpio[%d] err\n", pdata->tp_int);
			pdata->tp_int = -1;
		} else {
			pdata->irq = gpio_to_irq(pdata->tp_int);
			//printk("touch tp_int irq=%d\n", pdata->irq);
		}
	}
	
	pdata->tp_resetn = of_get_named_gpio_flags(np, "tp_resetn", 0, NULL);
	printk("tp reset gpio=%d\n", pdata->tp_resetn);

	TS_DEBUG("ft5x0x detected, polling_mode: %d, multi_touch: %d\n",
			pdata->polling_mode, pdata->multi_touch);

	dev_dbg(dev, "ft5x0x detected, polling_mode: %d, multi_touch: %d\n",
			pdata->polling_mode, pdata->multi_touch);

	return pdata;
}
#else
static struct ft5x0x_ts_platform_data *ft5x0x_parse_dt(struct device *dev)
{
	return NULL;
}
#endif

static int 
ft5x0x_ts_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    struct ft5x0x_ts_data *ft5x0x_ts;
	struct device *dev = &client->dev;
	struct device_node *np = dev->of_node;
    struct input_dev *input_dev;
    int err = 0;
    
    if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
        err = -ENODEV;
        goto exit_check_functionality_failed;
    }

	/*
	 * Add by JBO
	 * Use platform data to initialize ft5x0x
	 */
	this_client = client;
	ts_plat_data = dev_get_platdata(&client->dev);
	if (np) {
		ts_plat_data = ft5x0x_parse_dt(dev);
		if (IS_ERR(ts_plat_data))
			goto exit_get_platdata_failed;

		if (client->irq && !ts_plat_data->irq) {
			ts_plat_data->irq = client->irq;
		} else {
			client->irq = ts_plat_data->irq;
		}
	} else if (!ts_plat_data)
		goto exit_get_platdata_failed;

	if (gpio_is_valid(ts_plat_data->tp_resetn)) {
		err = gpio_request(ts_plat_data->tp_resetn, "tp_resetn");
		if (!err) {
			gpio_direction_output(ts_plat_data->tp_resetn, 1);
			gpio_export(ts_plat_data->tp_resetn, 0);
			// may be you need some delay here
		} else {
			printk("request tp_resetn[gpio %d] err:%d\n", 
					ts_plat_data->tp_resetn, err);
		}
	}
	//  Read device ID
    err = 0xa1;
    if (ft5x0x_i2c_rxdata((char *)&err, 2) < 0) {
    	printk(KERN_ERR"ft5x0x read ID error!\n");
		goto exit_check_functionality_failed;
    }
    printk(KERN_ERR "ft5x0x id: 0x%04X\n", err);
    err = 0;

    TS_DEBUG("==kzalloc=\n");
    ft5x0x_ts = kzalloc(sizeof(*ft5x0x_ts), GFP_KERNEL);
    if (!ft5x0x_ts)    {
        err = -ENOMEM;
        goto exit_alloc_data_failed;
    }
 
    TS_DEBUG("==i2c_set_clientdata=\n");
	//    this_client = client;
    i2c_set_clientdata(client, ft5x0x_ts);
	//    i2c_jz_setclk(client, 100*1000);
 
    TS_DEBUG("==INIT_WORK=\n");

	if (ts_plat_data->polling_mode) {
		/* Added by Kevin, test only */
		TS_DEBUG("poll mode\n");	
		INIT_DELAYED_WORK(&ft5x0x_ts->poll_work, ft5x0x_poll_work);
		ft5x0x_ts->stop_poll_flag = 0;
	} else {
		INIT_WORK(&ft5x0x_ts->pen_event_work, ft5x0x_ts_pen_irq_work);
	}

    TS_DEBUG("==create_singlethread_workqueue=\n");
    ft5x0x_ts->ts_workqueue = create_singlethread_workqueue(dev_name(&client->dev));
    if (!ft5x0x_ts->ts_workqueue) {
        err = -ESRCH;
        goto exit_create_singlethread;
    }

	if (!ts_plat_data->polling_mode) {
		err = request_irq(ts_plat_data->irq, ft5x0x_ts_interrupt, 
				IRQF_ONESHOT | IRQF_TRIGGER_FALLING, "ft5x0x_ts", ft5x0x_ts);
		if (err < 0) {
			dev_err(&client->dev, "ft5x0x_probe: request irq failed\n");
			goto exit_irq_request_failed;
		}
	}

    TS_DEBUG("==input_allocate_device=\n");
    input_dev = input_allocate_device();
    if (!input_dev) {
        err = -ENOMEM;
        dev_err(&client->dev, "failed to allocate input device\n");
        goto exit_input_dev_alloc_failed;
    }
    
    ft5x0x_ts->input_dev = input_dev;

	if (ts_plat_data->multi_touch) {
		set_bit(ABS_MT_TOUCH_MAJOR, input_dev->absbit);
		set_bit(ABS_MT_POSITION_X, input_dev->absbit);
		set_bit(ABS_MT_POSITION_Y, input_dev->absbit);
		set_bit(ABS_MT_WIDTH_MAJOR, input_dev->absbit);
 
		input_set_abs_params(input_dev,
							 ABS_MT_POSITION_X, 0, SCREEN_MAX_X, 0, 0);
		input_set_abs_params(input_dev,
							 ABS_MT_POSITION_Y, 0, SCREEN_MAX_Y, 0, 0);
		input_set_abs_params(input_dev,
							 ABS_MT_TOUCH_MAJOR, 0, PRESS_MAX, 0, 0);
		input_set_abs_params(input_dev,
							 ABS_MT_WIDTH_MAJOR, 0, 200, 0, 0);
	} else {
		set_bit(ABS_X, input_dev->absbit);
		set_bit(ABS_Y, input_dev->absbit);
		set_bit(ABS_PRESSURE, input_dev->absbit);
		set_bit(BTN_TOUCH, input_dev->keybit);
 
		input_set_abs_params(input_dev, ABS_X, 0, SCREEN_MAX_X, 0, 0);
		input_set_abs_params(input_dev, ABS_Y, 0, SCREEN_MAX_Y, 0, 0);
		input_set_abs_params(input_dev,
							 ABS_PRESSURE, 0, PRESS_MAX, 0 , 0);
	}
 
    set_bit(EV_ABS, input_dev->evbit);
    set_bit(EV_KEY, input_dev->evbit);
 
    input_dev->name        = FT5X0X_NAME;        //dev_name(&client->dev)
    err = input_register_device(input_dev);
    if (err) {
        dev_err(&client->dev,
				"ft5x0x_ts_probe: failed to register input device: %s\n",
				dev_name(&client->dev));
        goto exit_input_register_device_failed;
    }
 
#ifdef CONFIG_HAS_EARLYSUSPEND
    TS_DEBUG("==register_early_suspend =\n");
    ft5x0x_ts->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
    ft5x0x_ts->early_suspend.suspend = ft5x0x_ts_suspend;
    ft5x0x_ts->early_suspend.resume    = ft5x0x_ts_resume;
    register_early_suspend(&ft5x0x_ts->early_suspend);
#endif
	//wake the CTPM
	//    __gpio_as_output(GPIO_FT5X0X_WAKE);        
	//    __gpio_clear_pin(GPIO_FT5X0X_WAKE);        //set wake = 0,base on system
	//     msleep(100);
	//    __gpio_set_pin(GPIO_FT5X0X_WAKE);            //set wake = 1,base on system
	//    msleep(100);
	//    ft5x0x_set_reg(0x88, 0x05); //5, 6,7,8
	//    ft5x0x_set_reg(0x80, 30);
	//    msleep(50);

	if (!ts_plat_data->polling_mode) {
		//   enable_irq(this_client->irq);
		//    enable_irq(IRQ_EINT(6));

	} else {
		/* Added by Kevin, test only */
		TS_DEBUG("queue delay work\n");	
		queue_delayed_work(ft5x0x_ts->ts_workqueue, &ft5x0x_ts->poll_work, 2); 
	}

    TS_DEBUG("==probe over =, this_client->irq=%d\n\n\n", ts_plat_data->irq);
    return 0;
 
 exit_input_register_device_failed:
    input_free_device(input_dev);
 exit_input_dev_alloc_failed:
	if (!ts_plat_data->polling_mode)
		free_irq(ts_plat_data->irq, ft5x0x_ts);
	//    free_irq(IRQ_EINT(6), ft5x0x_ts);
 exit_irq_request_failed:

	//exit_platform_data_null:
	if (!ts_plat_data->polling_mode)
		cancel_delayed_work_sync(&ft5x0x_ts->poll_work);
	else
		cancel_work_sync(&ft5x0x_ts->pen_event_work);

    destroy_workqueue(ft5x0x_ts->ts_workqueue);
 exit_create_singlethread:
    TS_DEBUG("==singlethread error =\n\n\n");
    i2c_set_clientdata(client, NULL);
    kfree(ft5x0x_ts);
 exit_alloc_data_failed:
 exit_get_platdata_failed:
 	if (gpio_is_valid(ts_plat_data->tp_int)) {
 		gpio_free(ts_plat_data->tp_int);
 	}
	if (gpio_is_valid(ts_plat_data->tp_resetn)) {
		gpio_free(ts_plat_data->tp_resetn);
	}
 exit_check_functionality_failed:
    return err;
}
 
static int ft5x0x_ts_remove(struct i2c_client *client)
{
    struct ft5x0x_ts_data *ft5x0x_ts = i2c_get_clientdata(client);
    TS_DEBUG("==ft5x0x_ts_remove=\n");
#ifdef CONFIG_HAS_EARLYSUSPEND
    unregister_early_suspend(&ft5x0x_ts->early_suspend);
#endif

	if (!ts_plat_data->polling_mode) {
		printk("free irq %d\n", client->irq);
		free_irq(client->irq, ft5x0x_ts);
		cancel_work_sync(&ft5x0x_ts->pen_event_work);
	} else {
		/* Added by Kevin, test only */
		ft5x0x_ts->stop_poll_flag = 1;
		cancel_delayed_work_sync(&ft5x0x_ts->poll_work);
	}
    destroy_workqueue(ft5x0x_ts->ts_workqueue);
    i2c_set_clientdata(client, NULL);

    input_unregister_device(ft5x0x_ts->input_dev);
    kfree(ft5x0x_ts);
	
	if (gpio_is_valid(ts_plat_data->tp_resetn)) {
		printk("gpio free %d\n", ts_plat_data->tp_resetn);
		gpio_free(ts_plat_data->tp_resetn);
	}

    if (gpio_is_valid(ts_plat_data->tp_int)) {
    	printk("gpio free %d\n", ts_plat_data->tp_int);
    	gpio_free(ts_plat_data->tp_int);
    }
    return 0;
}

#if defined(CONFIG_OF)
static const struct of_device_id ft5x0x_ts_match[] = {
	{ .compatible = FT5X0X_NAME, },
	{ },
};
MODULE_DEVICE_TABLE(of, ft5x0x_ts_match);
#endif

static const struct i2c_device_id ft5x0x_ts_id[] = {
    { FT5X0X_NAME, 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, ft5x0x_ts_id);

static struct i2c_driver ft5x0x_ts_driver = {
    .probe        = ft5x0x_ts_probe,
    .remove        = ft5x0x_ts_remove,
    .id_table    = ft5x0x_ts_id,
    .driver    = {
        .name    = FT5X0X_NAME,
		.owner    = THIS_MODULE,
		.of_match_table = of_match_ptr(ft5x0x_ts_match),
    },
};

static int __init ft5x0x_ts_init(void)
{
	int err = 0;
	
	if (i2c_add_driver(&ft5x0x_ts_driver) != 0) {
		printk("i2c_add_driver: can't add i2c driver\n");
		err = -ENODEV;
		goto err_driver;
	}

	printk("ft5x0x_ts_init successful\n");
	
	return 0;

 err_driver:
	printk("ft5x0x_ts_init fail\n");
	return err;
}

static void __exit ft5x0x_ts_exit(void)
{
	i2c_del_driver(&ft5x0x_ts_driver);
	this_client = NULL;
}

 
module_init(ft5x0x_ts_init);
module_exit(ft5x0x_ts_exit);
 
MODULE_AUTHOR("Kevin Su<kevin.su@myirtech.com>");
MODULE_DESCRIPTION("FocalTech ft5x0x TouchScreen driver");
MODULE_LICENSE("GPL");
