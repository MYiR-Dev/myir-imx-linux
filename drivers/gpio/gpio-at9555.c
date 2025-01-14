#include <linux/gpio.h>
#include <linux/gpio/driver.h>  // 如果你实现的是 GPIO 驱动
#include <linux/gpio/consumer.h> // 如果你是 GPIO 消费者

#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/slab.h>

#define AT9555_MAXGPIO 16
#define AT9555_BANK(offs) ((offs) >> 3)
#define AT9555_BIT(offs) (1u << ((offs)&0x7))
#define REG_INPUT_PORT0 0
#define REG_INPUT_PORT1 1
#define REG_OUTPUT_PORT0 2
#define REG_OUTPUT_PORT1 3
#define REG_POLARITY_INVERSION_PORT0 4
#define REG_POLARITY_INVERSION_PORT1 5
#define REG_CONFIGURATION_PORT0 6
#define REG_CONFIGURATION_PORT1 7

struct at9555_gpio {
    struct i2c_client *client;
    struct gpio_chip gpio_chip;
    struct mutex lock; /* protect cached dir, dat_out */
    /* protect serialized access to the interrupt controller bus */
    struct mutex irq_lock;
    unsigned gpio_start;
    unsigned irq_base;
    uint8_t dat_out[4];
    uint8_t dir[4];
    // uint8_t int_lvl[3];
    // uint8_t int_en[3];
    // uint8_t irq_mask[3];
    // uint8_t irq_stat[3];
};

struct at9555_platform_data {
    /* number assigned to the first GPIO */
    int gpio_base;
    char *label;
    unsigned n_latch;

    int (*setup)(struct i2c_client *client, int gpio, unsigned ngpio,
                 void *context);
    int (*teardown)(struct i2c_client *client, int gpio, unsigned ngpio,
                    void *context);
    void *context;

    /* list of GPIO names (array length = SC16IS7X2_NR_GPIOS) */
    const char *const *names;
};

static int at9555_gpio_read(struct i2c_client *client, u8 reg) {
    int ret = i2c_smbus_read_byte_data(client, reg);

    if (ret < 0)
        dev_err(&client->dev, "Read Error\n");

    return ret;
}

static int at9555_gpio_write(struct i2c_client *client, u8 reg, u8 val) {
    int ret = i2c_smbus_write_byte_data(client, reg, val);

    if (ret < 0)
        dev_err(&client->dev, "Write Error\n");

    return ret;
}

static int at9555_gpio_get_value(struct gpio_chip *chip, unsigned off) {
    struct at9555_gpio *dev = container_of(chip, struct at9555_gpio, gpio_chip);

    return !!(at9555_gpio_read(dev->client, REG_INPUT_PORT0 + AT9555_BANK(off)) &
              AT9555_BIT(off));
}

static void at9555_gpio_set_value(struct gpio_chip *chip, unsigned off,
                                  int val) {
    unsigned bank, bit;
    struct at9555_gpio *dev = container_of(chip, struct at9555_gpio, gpio_chip);

    bank = AT9555_BANK(off);
    bit = AT9555_BIT(off);

    mutex_lock(&dev->lock);
    if (val)
        dev->dat_out[bank] |= bit;
    else
        dev->dat_out[bank] &= ~bit;

    at9555_gpio_write(dev->client, REG_OUTPUT_PORT0 + bank, dev->dat_out[bank]);
    mutex_unlock(&dev->lock);
}

static int at9555_gpio_direction_input(struct gpio_chip *chip, unsigned off) {
    int ret;
    unsigned bank;
    struct at9555_gpio *dev = container_of(chip, struct at9555_gpio, gpio_chip);

    bank = AT9555_BANK(off);

    mutex_lock(&dev->lock);
    dev->dir[bank] |= AT9555_BIT(off);
    ret = at9555_gpio_write(dev->client, REG_CONFIGURATION_PORT0 + bank,
                            dev->dir[bank]);
    mutex_unlock(&dev->lock);

    return ret;
}

static int at9555_gpio_direction_output(struct gpio_chip *chip, unsigned off,
                                        int val) {
    int ret;
    unsigned bank, bit;
    struct at9555_gpio *dev = container_of(chip, struct at9555_gpio, gpio_chip);

    bank = AT9555_BANK(off);
    bit = AT9555_BIT(off);

    mutex_lock(&dev->lock);
    dev->dir[bank] &= ~bit;

    if (val)
        dev->dat_out[bank] |= bit;
    else
        dev->dat_out[bank] &= ~bit;

    ret = at9555_gpio_write(dev->client, REG_OUTPUT_PORT0 + bank,
                            dev->dat_out[bank]);
    ret |= at9555_gpio_write(dev->client, REG_CONFIGURATION_PORT0 + bank,
                             dev->dir[bank]);
    mutex_unlock(&dev->lock);

    return ret;
}
static struct at9555_platform_data at9555_gpio_data = {
    .gpio_base = -1,
    .label = "ext_gpio",
    .names = NULL,
};

static struct at9555_platform_data *at9555_parse_dt(struct device_node *np) {
    struct at9555_platform_data *pdata = &at9555_gpio_data;
    of_property_read_u32(np, "gpio_base", &pdata->gpio_base);
    printk(KERN_ERR "at9555 gpio_base=%d\n", pdata->gpio_base);
    return pdata;
}

static int at9555_gpio_probe(struct i2c_client *client) {
    int status;
    struct at9555_platform_data *pdata = dev_get_platdata(&client->dev);
    int ret, i;

    // struct device_node		*np = client->dev.of_node;
    struct at9555_gpio *gpio;

    // printk(KERN_INFO "i2c at9555 probe\n");
    printk("myir i2c_gpio probe\n");
    if (client->dev.of_node) {
        pdata = at9555_parse_dt(client->dev.of_node);
    } else {
        pdata = client->dev.platform_data;
    }

    if (!pdata || pdata->gpio_base < 0) {
        dev_err(&client->dev, "incorrect or missing platform data\n");
        return -EINVAL;
    }
    if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
        dev_err(&client->dev, "SMBUS Byte Data not Supported\n");
        return -EIO;
    }

    /* Allocate, initialize, and register this gpio_chip. */
    gpio = devm_kzalloc(&client->dev, sizeof(*gpio), GFP_KERNEL);
    if (!gpio)
        return -ENOMEM;

    at9555_gpio_read(client, REG_OUTPUT_PORT0);

    gpio->gpio_chip.base = pdata ? pdata->gpio_base : -1;
    gpio->gpio_chip.can_sleep = true;
    gpio->gpio_chip.parent = &client->dev;
    gpio->gpio_chip.owner = THIS_MODULE;
    gpio->gpio_chip.get = at9555_gpio_get_value;
    gpio->gpio_chip.set = at9555_gpio_set_value;
    gpio->gpio_chip.direction_input = at9555_gpio_direction_input;
    gpio->gpio_chip.direction_output = at9555_gpio_direction_output;
    gpio->gpio_chip.ngpio = AT9555_MAXGPIO;

    mutex_init(&gpio->lock);

    for (i = 0, ret = 0; i <= AT9555_BANK(AT9555_MAXGPIO); i++) {
        gpio->dat_out[i] = at9555_gpio_read(client, REG_OUTPUT_PORT0 + i);
        gpio->dir[i] = at9555_gpio_read(client, REG_INPUT_PORT0 + i);
    }

    gpio->client = client;
    i2c_set_clientdata(client, gpio);

    status = devm_gpiochip_add_data(&client->dev, &gpio->gpio_chip, gpio);
    if (status < 0)
        goto fail;

    /* Let platform code set up the GPIOs and their users.
     * Now is the first time anyone could use them.
     */
    if (pdata && pdata->setup) {
        status = pdata->setup(client, gpio->gpio_chip.base, gpio->gpio_chip.ngpio,
                              pdata->context);
        if (status < 0)
            dev_warn(&client->dev, "setup --> %d\n", status);
    }

    dev_info(&client->dev, "probed\n");

    return 0;

fail:
    dev_dbg(&client->dev, "probe error %d for '%s'\n", status, client->name);
    kfree(gpio);
    return status;
}

/*static int at9555_gpio_remove(struct i2c_client *client) {

    
    struct at9555_gpio *dev = i2c_get_clientdata(client);

    gpiochip_remove(&dev->gpio_chip);
    // if (ret) {
    //	dev_err(&client->dev, "gpiochip_remove failed %d\n", ret);
    //	return ret;
    //}

    kfree(dev);
    return 0;
}*/

static const struct of_device_id at9555_gpio_match[] = {
    {
        .compatible = "analogtek,at9555",
        //.data = &omap4_pdata,
    },
    {},
};
MODULE_DEVICE_TABLE(of, at9555_gpio_match);

static const struct i2c_device_id at9555_gpio_id[] = {{"at9555", 0}, {}};
MODULE_DEVICE_TABLE(i2c, at9555_gpio_id);

static struct i2c_driver at9555_gpio_driver = {
    .driver =
    {
        .name = "at9555_gpio",
        .of_match_table = of_match_ptr(at9555_gpio_match),
    },
    .probe = at9555_gpio_probe, 
    // .remove = at9555_gpio_remove,
    .id_table = at9555_gpio_id,
};

module_i2c_driver(at9555_gpio_driver);

MODULE_AUTHOR("long.chen <long.chen@myirtech.com>");
MODULE_DESCRIPTION("GPIO AT9555 Driver");
MODULE_LICENSE("GPL");
