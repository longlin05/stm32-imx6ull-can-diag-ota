/*
 * DHT22 温湿度传感器 -> IIO
 *
 * 两种绑定方式：
 *   1) 设备树：compatible = "zulong,dht22"，data-gpios 指定数据脚
 *   2) 回退：没有设备树节点时用模块参数 gpio_num（兼容旧用法）
 *
 * IIO 通道：
 *   in_temp_input              温度，单位 0.001 摄氏度
 *   in_humidityrelative_input  湿度，单位 0.001 %RH
 */
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/iio/iio.h>
#include <linux/init.h>
#include <linux/irqflags.h>
#include <linux/jiffies.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/types.h>

#define DHT22_MIN_INTERVAL_MS 2000U
#define DHT22_EDGE_TIMEOUT_US 150U

struct dht22_state {
    struct gpio_desc *data;
    struct mutex lock;
    unsigned long last_jiffies;
    int temp_mc;
    int hum_mc;
};

/* 没有设备树节点时的回退引脚 */
static int gpio_num = 4;
module_param(gpio_num, int, 0444);
MODULE_PARM_DESC(gpio_num, "global GPIO number for DHT22 DATA (used only without DT node)");

/* probe 是否被设备树节点触发过 */
static bool dht22_probed;

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

static int dht22_wait_level(struct gpio_desc *data, int level, unsigned int timeout_us)
{
    ktime_t start = ktime_get();

    while (gpiod_get_value(data) != level) {
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

    /* 起始信号：至少 1ms 低电平 */
    gpiod_direction_output(st->data, 0);
    udelay(2000);
    gpiod_direction_input(st->data);

    /* DHT22 应答：80us 低 + 80us 高 */
    if (dht22_wait_level(st->data, 0, DHT22_EDGE_TIMEOUT_US) != 0 ||
        dht22_wait_level(st->data, 1, DHT22_EDGE_TIMEOUT_US) != 0 ||
        dht22_wait_level(st->data, 0, DHT22_EDGE_TIMEOUT_US) != 0) {
        ret = -ETIMEDOUT;
        goto out;
    }

    for (i = 0; i < 40; i++) {
        if (dht22_wait_level(st->data, 1, DHT22_EDGE_TIMEOUT_US) != 0) {
            ret = -ETIMEDOUT;
            goto out;
        }

        /* 26-28us 表示 0，70us 表示 1，40us 处采样 */
        udelay(40);
        if (gpiod_get_value(st->data)) {
            data[i / 8] |= (uint8_t)(1U << (7 - (i % 8)));
        }

        if (dht22_wait_level(st->data, 0, DHT22_EDGE_TIMEOUT_US) != 0) {
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

static int dht22_probe(struct platform_device *pdev)
{
    struct iio_dev *indio_dev;
    struct dht22_state *st;
    int *pdata = dev_get_platdata(&pdev->dev);
    int legacy_gpio = pdata ? *pdata : -1;
    int ret;

    dht22_probed = true;

    indio_dev = devm_iio_device_alloc(&pdev->dev, sizeof(*st));
    if (!indio_dev) {
        return -ENOMEM;
    }

    st = iio_priv(indio_dev);
    mutex_init(&st->lock);
    st->last_jiffies = jiffies - msecs_to_jiffies(DHT22_MIN_INTERVAL_MS);

    if (pdev->dev.of_node) {
        /* 设备树路径：data-gpios = <&gpio1 4 GPIO_ACTIVE_HIGH> */
        st->data = devm_gpiod_get(&pdev->dev, "data", GPIOD_IN);
        if (IS_ERR(st->data)) {
            ret = PTR_ERR(st->data);
            dev_err(&pdev->dev, "failed to get data-gpios: %d\n", ret);
            return ret;
        }
    } else if (legacy_gpio >= 0) {
        /* 回退路径：模块参数给的全局 GPIO 号 */
        ret = devm_gpio_request_one(&pdev->dev, legacy_gpio, GPIOF_IN, "dht22-data");
        if (ret != 0) {
            dev_err(&pdev->dev, "gpio_request(%d) failed: %d\n", legacy_gpio, ret);
            return ret;
        }
        st->data = gpio_to_desc(legacy_gpio);
    } else {
        dev_err(&pdev->dev, "no DT node and no gpio_num\n");
        return -EINVAL;
    }

    indio_dev->name = "dht22";
    indio_dev->dev.parent = &pdev->dev;
    indio_dev->dev.of_node = pdev->dev.of_node;
    indio_dev->modes = INDIO_DIRECT_MODE;
    indio_dev->channels = dht22_channels;
    indio_dev->num_channels = ARRAY_SIZE(dht22_channels);
    indio_dev->info = &dht22_info;

    ret = iio_device_register(indio_dev);
    if (ret != 0) {
        dev_err(&pdev->dev, "iio_device_register failed: %d\n", ret);
        return ret;
    }

    platform_set_drvdata(pdev, indio_dev);

    dev_info(&pdev->dev, "probed, %s\n",
             pdev->dev.of_node ? "from device tree" : "from module parameter");
    return 0;
}

static int dht22_remove(struct platform_device *pdev)
{
    struct iio_dev *indio_dev = platform_get_drvdata(pdev);

    iio_device_unregister(indio_dev);
    dev_info(&pdev->dev, "removed\n");

    /* gpio 由 devm 释放 */
    return 0;
}

static const struct of_device_id dht22_of_match[] = {
    { .compatible = "zulong,dht22" },
    { }
};
MODULE_DEVICE_TABLE(of, dht22_of_match);

static struct platform_driver dht22_driver = {
    .probe = dht22_probe,
    .remove = dht22_remove,
    .driver = {
        .name = "dht22",
        .of_match_table = dht22_of_match,
    },
};

static struct platform_device *dht22_legacy_pdev;

static int __init dht22_init(void)
{
    int ret;

    ret = platform_driver_register(&dht22_driver);
    if (ret != 0) {
        pr_err("dht22: platform_driver_register failed: %d\n", ret);
        return ret;
    }

    /*
     * 设备树里有节点时，platform_driver_register() 已经把 probe 跑完了；
     * 没有节点才用模块参数补一个 platform device，保留旧用法。
     */
    if (!dht22_probed) {
        dht22_legacy_pdev = platform_device_alloc("dht22", -1);
        if (!dht22_legacy_pdev) {
            platform_driver_unregister(&dht22_driver);
            return -ENOMEM;
        }
        ret = platform_device_add_data(dht22_legacy_pdev,
                                       &gpio_num, sizeof(gpio_num));
        if (ret == 0) {
            ret = platform_device_add(dht22_legacy_pdev);
        }
        if (ret != 0) {
            platform_device_put(dht22_legacy_pdev);
            dht22_legacy_pdev = NULL;
            platform_driver_unregister(&dht22_driver);
            return ret;
        }
        pr_info("dht22: no DT node, using gpio_num=%d\n", gpio_num);
    }

    return 0;
}

static void __exit dht22_exit(void)
{
    if (dht22_legacy_pdev) {
        platform_device_unregister(dht22_legacy_pdev);
        dht22_legacy_pdev = NULL;
    }
    platform_driver_unregister(&dht22_driver);
    pr_info("dht22: unloaded\n");
}

module_init(dht22_init);
module_exit(dht22_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("project B");
MODULE_DESCRIPTION("DHT22 temperature/humidity IIO driver with DT support");
