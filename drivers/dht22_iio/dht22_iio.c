#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/iio/iio.h>
#include <linux/init.h>
#include <linux/irqflags.h>
#include <linux/jiffies.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/types.h>

#define DHT22_MIN_INTERVAL_MS 2000U
#define DHT22_EDGE_TIMEOUT_US 150U

struct dht22_state {
    int gpio;
    struct mutex lock;
    unsigned long last_jiffies;
    int temp_mc;
    int hum_mc;
};

static int gpio_num = 4;
static struct iio_dev *dht22_indio_dev;
module_param(gpio_num, int, 0444);
MODULE_PARM_DESC(gpio_num, "Linux global GPIO number for DHT22 DATA");

static const struct iio_chan_spec dht22_channels[] = {
    {
        .type = IIO_TEMP,
        .channel = 0,
        .info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
        .datasheet_name = "temperature",
    },
    {
        .type = IIO_HUMIDITYRELATIVE,
        .channel = 0,
        .info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
        .datasheet_name = "humidity",
    },
};

static int dht22_wait_level(int gpio, int level, unsigned int timeout_us)
{
    ktime_t start = ktime_get();

    while (gpio_get_value(gpio) != level) {
        if (ktime_to_us(ktime_sub(ktime_get(), start)) > timeout_us) {
            return -ETIMEDOUT;
        }
        udelay(1);
    }
    return 0;
}

static int dht22_read_raw_hw(struct dht22_state *st, int *temp_mc, int *hum_mc)
{
    uint8_t data[5] = {0};
    unsigned long flags;
    int i;
    int ret = 0;

    local_irq_save(flags);

    /* Start: 至少 1ms 低电平 */
    gpio_direction_output(st->gpio, 0);
    udelay(2000);
    gpio_direction_input(st->gpio);

    /* DHT22: 80us low + 80us high */
    if (dht22_wait_level(st->gpio, 0, DHT22_EDGE_TIMEOUT_US) != 0 ||
        dht22_wait_level(st->gpio, 1, DHT22_EDGE_TIMEOUT_US) != 0 ||
        dht22_wait_level(st->gpio, 0, DHT22_EDGE_TIMEOUT_US) != 0) {
        ret = -ETIMEDOUT;
        goto out;
    }

    for (i = 0; i < 40; i++) {
        if (dht22_wait_level(st->gpio, 1, DHT22_EDGE_TIMEOUT_US) != 0) {
            ret = -ETIMEDOUT;
            goto out;
        }

        /* 26-28us = 0, 70us = 1；40us 时采样 */
        udelay(40);
        if (gpio_get_value(st->gpio)) {
            data[i / 8] |= (uint8_t)(1U << (7 - (i % 8)));
        }

        if (dht22_wait_level(st->gpio, 0, DHT22_EDGE_TIMEOUT_US) != 0) {
            ret = -ETIMEDOUT;
            goto out;
        }
    }

out:
    local_irq_restore(flags);

    if (ret != 0) {
        return ret;
    }

    if ((uint8_t)(data[0] + data[1] + data[2] + data[3]) != data[4]) {
        return -EIO;
    }

    *hum_mc = ((int)data[0] << 8 | data[1]) * 100;
    {
        int16_t t = (int16_t)((uint16_t)data[2] << 8 | data[3]);
        if (t & 0x8000) {
            t = -(int)(t & 0x7FFF);
        }
        *temp_mc = (int)t * 100;
    }
    return 0;
}

static int dht22_update(struct dht22_state *st)
{
    int temp_mc;
    int hum_mc;
    int ret;

    if (time_before(jiffies, st->last_jiffies +
                             msecs_to_jiffies(DHT22_MIN_INTERVAL_MS))) {
        return 0;
    }

    ret = dht22_read_raw_hw(st, &temp_mc, &hum_mc);
    if (ret != 0) {
        return ret;
    }
    st->temp_mc = temp_mc;
    st->hum_mc = hum_mc;
    st->last_jiffies = jiffies;
    return 0;
}

static int dht22_read_raw(struct iio_dev *indio_dev,
                          struct iio_chan_spec const *chan,
                          int *val, int *val2, long mask)
{
    struct dht22_state *st = iio_priv(indio_dev);
    int ret;

    if (mask != IIO_CHAN_INFO_RAW) {
        return -EINVAL;
    }

    mutex_lock(&st->lock);
    ret = dht22_update(st);
    if (ret == 0) {
        if (chan->type == IIO_TEMP) {
            *val = st->temp_mc;
        } else if (chan->type == IIO_HUMIDITYRELATIVE) {
            *val = st->hum_mc;
        } else {
            ret = -EINVAL;
        }
    }
    mutex_unlock(&st->lock);

    return ret ? ret : IIO_VAL_INT;
}

static const struct iio_info dht22_info = {
    .driver_module = THIS_MODULE,
    .read_raw = dht22_read_raw,
};

static int __init dht22_init(void)
{
    struct iio_dev *indio_dev;
    struct dht22_state *st;
    int ret;

    if (!gpio_is_valid(gpio_num)) {
        pr_err("dht22: invalid gpio %d\n", gpio_num);
        return -EINVAL;
    }

    ret = gpio_request(gpio_num, "dht22-data");
    if (ret != 0) {
        pr_err("dht22: gpio_request(%d) failed: %d\n", gpio_num, ret);
        return ret;
    }
    gpio_direction_input(gpio_num);

    indio_dev = iio_device_alloc(sizeof(*st));
    if (!indio_dev) {
        ret = -ENOMEM;
        goto err_gpio;
    }

    st = iio_priv(indio_dev);
    st->gpio = gpio_num;
    mutex_init(&st->lock);
    st->last_jiffies = jiffies - msecs_to_jiffies(DHT22_MIN_INTERVAL_MS);

    indio_dev->name = "dht22";
    indio_dev->dev.parent = NULL;
    indio_dev->modes = INDIO_DIRECT_MODE;
    indio_dev->channels = dht22_channels;
    indio_dev->num_channels = ARRAY_SIZE(dht22_channels);
    indio_dev->info = &dht22_info;

    ret = iio_device_register(indio_dev);
    if (ret != 0) {
        pr_err("dht22: iio_device_register failed: %d\n", ret);
        goto err_iio;
    }

    pr_info("dht22: registered, gpio=%d\n", gpio_num);
    return 0;

err_iio:
    iio_device_free(indio_dev);
err_gpio:
    gpio_free(gpio_num);
    return ret;
}

static void __exit dht22_exit(void)
{
    struct iio_dev *indio_dev = iio_device_alloc(0);

    (void)indio_dev;
    pr_info("dht22: unloaded\n");
}

module_init(dht22_init);
module_exit(dht22_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("project A");
MODULE_DESCRIPTION("DHT22 temperature/humidity IIO driver");
