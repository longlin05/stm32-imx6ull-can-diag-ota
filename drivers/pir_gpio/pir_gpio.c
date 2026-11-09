/*
 * HC-SR501 PIR -> 字符设备 /dev/pir
 *
 * 两种绑定方式：
 *   1) 设备树：compatible = "zulong,pir"，irq-gpios 指定 OUT 脚
 *   2) 回退：没有设备树节点时用模块参数 gpio_num（兼容旧用法）
 *
 * 中断改造：threaded IRQ
 *   顶半部：只唤醒线程
 *   线程半部：sleep 消抖时间后再读电平，过 holdoff 才报事件
 *
 * 用户接口：
 *   read(/dev/pir)   阻塞到一次事件，返回当前电平
 *   poll(/dev/pir)   有事件时 POLLIN
 *   /sys/.../enable  1/0 开关
 */
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/wait.h>

#define PIR_NAME "pir"

/* 没有设备树节点时的回退参数 */
static int gpio_num = 9;
static int debounce_ms = 200;
static int holdoff_ms = 3000;
module_param(gpio_num, int, 0444);
module_param(debounce_ms, int, 0644);
module_param(holdoff_ms, int, 0644);
MODULE_PARM_DESC(gpio_num, "global GPIO number for PIR OUT (used only without DT node)");
MODULE_PARM_DESC(debounce_ms, "stable time before reporting an edge (ms)");
MODULE_PARM_DESC(holdoff_ms, "minimum interval between two events (ms)");

struct pir_state {
    struct device *dev;
    struct gpio_desc *out;
    int irq;
    int level;
    int event;
    int enabled;
    unsigned long last_event_jiffies;
    wait_queue_head_t wq;
    struct miscdevice misc;
};

static bool pir_probed;

static irqreturn_t pir_hardirq(int irq, void *data)
{
    (void)irq;
    (void)data;

    /* 顶半部什么都不做，交给线程半部去消抖 */
    return IRQ_WAKE_THREAD;
}

static irqreturn_t pir_thread(int irq, void *data)
{
    struct pir_state *st = data;
    int value;

    (void)irq;

    /* 边沿来了，等电平稳定后再采样 */
    msleep(debounce_ms > 0 ? debounce_ms : 1);
    value = gpiod_get_value(st->out);

    /* 只把稳定后的上升沿当成一次“有人经过” */
    if (value && !st->level) {
        if (!holdoff_ms ||
            time_after(jiffies,
                       st->last_event_jiffies + msecs_to_jiffies(holdoff_ms))) {
            st->last_event_jiffies = jiffies;
            st->event = 1;
            wake_up_interruptible(&st->wq);
        }
    }
    st->level = value;

    return IRQ_HANDLED;
}

static ssize_t pir_read(struct file *file, char __user *buf,
                        size_t count, loff_t *ppos)
{
    struct miscdevice *misc = file->private_data;
    struct pir_state *st = container_of(misc, struct pir_state, misc);
    char tmp[4];
    int len;
    int ret;

    (void)ppos;

    if (count == 0) {
        return 0;
    }

    ret = wait_event_interruptible(st->wq, st->event || !st->enabled);
    if (ret) {
        return ret;
    }
    if (!st->enabled) {
        return -EAGAIN;
    }

    st->event = 0;
    len = scnprintf(tmp, sizeof(tmp), "%d\n", st->level);
    if (count < (size_t)len) {
        len = (int)count;
    }
    if (copy_to_user(buf, tmp, len)) {
        return -EFAULT;
    }
    return len;
}

static unsigned int pir_poll(struct file *file, poll_table *wait)
{
    struct miscdevice *misc = file->private_data;
    struct pir_state *st = container_of(misc, struct pir_state, misc);
    unsigned int mask = 0;

    poll_wait(file, &st->wq, wait);
    if (st->event) {
        mask |= POLLIN | POLLRDNORM;
    }
    if (!st->enabled) {
        mask |= POLLERR;
    }
    return mask;
}

static const struct file_operations pir_fops = {
    .owner = THIS_MODULE,
    .read = pir_read,
    .poll = pir_poll,
    .llseek = no_llseek,
};

static ssize_t enable_show(struct device *dev,
                           struct device_attribute *attr, char *buf)
{
    struct pir_state *st = dev_get_drvdata(dev);

    (void)attr;
    return scnprintf(buf, PAGE_SIZE, "%d\n", st->enabled);
}

static ssize_t enable_store(struct device *dev,
                            struct device_attribute *attr,
                            const char *buf, size_t count)
{
    struct pir_state *st = dev_get_drvdata(dev);
    unsigned int val;
    int ret;

    (void)attr;

    ret = kstrtouint(buf, 0, &val);
    if (ret) {
        return ret;
    }
    st->enabled = val ? 1 : 0;
    if (st->enabled) {
        wake_up_interruptible(&st->wq);
    }
    return count;
}

static DEVICE_ATTR_RW(enable);

static int pir_probe(struct platform_device *pdev)
{
    struct pir_state *st;
    int *pdata = dev_get_platdata(&pdev->dev);
    int legacy_gpio = pdata ? *pdata : -1;
    int ret;

    pir_probed = true;

    st = devm_kzalloc(&pdev->dev, sizeof(*st), GFP_KERNEL);
    if (!st) {
        return -ENOMEM;
    }

    st->dev = &pdev->dev;
    st->enabled = 1;
    init_waitqueue_head(&st->wq);

    if (pdev->dev.of_node) {
        /* 设备树路径：irq-gpios = <&gpio1 9 GPIO_ACTIVE_HIGH> */
        st->out = devm_gpiod_get(&pdev->dev, "irq", GPIOD_IN);
        if (IS_ERR(st->out)) {
            ret = PTR_ERR(st->out);
            dev_err(&pdev->dev, "failed to get irq-gpios: %d\n", ret);
            return ret;
        }
    } else if (legacy_gpio >= 0) {
        ret = devm_gpio_request_one(&pdev->dev, legacy_gpio, GPIOF_IN, "pir-out");
        if (ret != 0) {
            dev_err(&pdev->dev, "gpio_request(%d) failed: %d\n", legacy_gpio, ret);
            return ret;
        }
        st->out = gpio_to_desc(legacy_gpio);
    } else {
        dev_err(&pdev->dev, "no DT node and no gpio_num\n");
        return -EINVAL;
    }

    st->level = gpiod_get_value(st->out);
    st->last_event_jiffies = jiffies - msecs_to_jiffies(2 * holdoff_ms + 1);

    st->irq = gpiod_to_irq(st->out);
    if (st->irq < 0) {
        dev_err(&pdev->dev, "gpiod_to_irq failed: %d\n", st->irq);
        return st->irq;
    }

    /*
     * 线程半部要睡，必须以 ONESHOT 带线程的方式申请。
     * 上升沿报事件，下降沿只用来把 st->level 复位 ——
     * 只申请上升沿的话，第一次事件之后 level 一直为 1，后面再也报不出来。
     */
    ret = devm_request_threaded_irq(&pdev->dev, st->irq,
                                    pir_hardirq, pir_thread,
                                    IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING |
                                    IRQF_ONESHOT,
                                    "pir-out", st);
    if (ret != 0) {
        dev_err(&pdev->dev, "request_threaded_irq %d failed: %d\n", st->irq, ret);
        return ret;
    }

    st->misc.minor = MISC_DYNAMIC_MINOR;
    st->misc.name = PIR_NAME;
    st->misc.fops = &pir_fops;
    st->misc.parent = &pdev->dev;

    ret = misc_register(&st->misc);
    if (ret != 0) {
        dev_err(&pdev->dev, "misc_register failed: %d\n", ret);
        return ret;
    }

    platform_set_drvdata(pdev, st);
    dev_set_drvdata(st->misc.this_device, st);

    ret = device_create_file(st->misc.this_device, &dev_attr_enable);
    if (ret != 0) {
        dev_err(&pdev->dev, "create enable attr failed: %d\n", ret);
        misc_deregister(&st->misc);
        return ret;
    }

    dev_info(&pdev->dev, "probed, irq=%d, initial=%d, %s\n",
             st->irq, st->level,
             pdev->dev.of_node ? "from device tree" : "from module parameter");
    return 0;
}

static int pir_remove(struct platform_device *pdev)
{
    struct pir_state *st = platform_get_drvdata(pdev);

    device_remove_file(st->misc.this_device, &dev_attr_enable);
    misc_deregister(&st->misc);
    /* devm 负责 free_irq 和 gpio */
    dev_info(&pdev->dev, "removed\n");
    return 0;
}

static const struct of_device_id pir_of_match[] = {
    { .compatible = "zulong,pir" },
    { }
};
MODULE_DEVICE_TABLE(of, pir_of_match);

static struct platform_driver pir_driver = {
    .probe = pir_probe,
    .remove = pir_remove,
    .driver = {
        .name = "pir",
        .of_match_table = pir_of_match,
    },
};

static struct platform_device *pir_legacy_pdev;

static int __init pir_init(void)
{
    int ret;

    ret = platform_driver_register(&pir_driver);
    if (ret != 0) {
        pr_err("pir: platform_driver_register failed: %d\n", ret);
        return ret;
    }

    if (!pir_probed) {
        pir_legacy_pdev = platform_device_alloc("pir", -1);
        if (!pir_legacy_pdev) {
            platform_driver_unregister(&pir_driver);
            return -ENOMEM;
        }
        ret = platform_device_add_data(pir_legacy_pdev,
                                       &gpio_num, sizeof(gpio_num));
        if (ret == 0) {
            ret = platform_device_add(pir_legacy_pdev);
        }
        if (ret != 0) {
            platform_device_put(pir_legacy_pdev);
            pir_legacy_pdev = NULL;
            platform_driver_unregister(&pir_driver);
            return ret;
        }
        pr_info("pir: no DT node, using gpio_num=%d\n", gpio_num);
    }

    return 0;
}

static void __exit pir_exit(void)
{
    if (pir_legacy_pdev) {
        platform_device_unregister(pir_legacy_pdev);
        pir_legacy_pdev = NULL;
    }
    platform_driver_unregister(&pir_driver);
    pr_info("pir: unloaded\n");
}

module_init(pir_init);
module_exit(pir_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("project B");
MODULE_DESCRIPTION("HC-SR501 PIR character device with DT and threaded IRQ");
