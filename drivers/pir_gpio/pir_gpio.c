#include <linux/device.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/uaccess.h>
#include <linux/wait.h>

#define PIR_NAME "pir"

static int gpio_num = 9;
static int debounce_ms = 200;
static int holdoff_ms = 3000;
module_param(gpio_num, int, 0444);
module_param(debounce_ms, int, 0644);
MODULE_PARM_DESC(gpio_num, "Linux global GPIO number for PIR OUT");
MODULE_PARM_DESC(debounce_ms, "stable time before reporting an edge (ms)");

static int irq_num;
static int pir_value;
static int pir_level;
static int pir_event;
static int pir_enabled = 1;
static unsigned long last_event_jiffies;
static wait_queue_head_t pir_wq;
static struct miscdevice pir_misc;
static struct timer_list pir_debounce;

static void pir_debounce_fn(unsigned long data)
{
    int value = gpio_get_value(gpio_num);

    (void)data;

    /* 只把稳定后的上升沿当成一次“有人经过” */
    if (value && !pir_level) {
        if (time_after(jiffies,
                       last_event_jiffies + msecs_to_jiffies(holdoff_ms ? holdoff_ms : 1))) {
            last_event_jiffies = jiffies;
            pir_event = 1;
            wake_up_interruptible(&pir_wq);
        }
    }
    pir_level = value;
    pir_value = value;
}

static irqreturn_t pir_irq(int irq, void *dev_id)
{
    (void)irq;
    (void)dev_id;

    /* 边沿来了先重置定时器，等电平稳定 debounce_ms 再采样 */
    mod_timer(&pir_debounce,
              jiffies + msecs_to_jiffies(debounce_ms ? debounce_ms : 1));
    return IRQ_HANDLED;
}

static ssize_t pir_read(struct file *file, char __user *buf,
                        size_t count, loff_t *ppos)
{
    char tmp[4];
    int len;
    int ret;

    (void)file;
    (void)ppos;

    if (count == 0) {
        return 0;
    }

    ret = wait_event_interruptible(pir_wq, pir_event || !pir_enabled);
    if (ret) {
        return ret;
    }
    if (!pir_enabled) {
        return -EAGAIN;
    }

    pir_event = 0;
    len = scnprintf(tmp, sizeof(tmp), "%d\n", pir_value);
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
    unsigned int mask = 0;

    (void)file;

    poll_wait(file, &pir_wq, wait);
    if (pir_event) {
        mask |= POLLIN | POLLRDNORM;
    }
    if (!pir_enabled) {
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
    (void)dev;
    (void)attr;
    return scnprintf(buf, PAGE_SIZE, "%d\n", pir_enabled);
}

static ssize_t enable_store(struct device *dev,
                            struct device_attribute *attr,
                            const char *buf, size_t count)
{
    unsigned int val;
    int ret;

    (void)dev;
    (void)attr;

    ret = kstrtouint(buf, 0, &val);
    if (ret) {
        return ret;
    }
    pir_enabled = val ? 1 : 0;
    if (pir_enabled) {
        wake_up_interruptible(&pir_wq);
    }
    return count;
}

static DEVICE_ATTR_RW(enable);

static int __init pir_init(void)
{
    int ret;

    if (!gpio_is_valid(gpio_num)) {
        pr_err("pir: invalid gpio %d\n", gpio_num);
        return -EINVAL;
    }

    ret = gpio_request(gpio_num, "pir-out");
    if (ret) {
        pr_err("pir: gpio_request(%d) failed: %d\n", gpio_num, ret);
        return ret;
    }
    gpio_direction_input(gpio_num);
    pir_level = gpio_get_value(gpio_num);
    pir_value = pir_level;
    last_event_jiffies = jiffies - msecs_to_jiffies(holdoff_ms ? holdoff_ms : 1) - 1;

    irq_num = gpio_to_irq(gpio_num);
    if (irq_num < 0) {
        ret = irq_num;
        pr_err("pir: gpio_to_irq failed: %d\n", ret);
        goto err_gpio;
    }

    init_waitqueue_head(&pir_wq);
    setup_timer(&pir_debounce, pir_debounce_fn, 0);

    pir_misc.minor = MISC_DYNAMIC_MINOR;
    pir_misc.name = PIR_NAME;
    pir_misc.fops = &pir_fops;

    ret = misc_register(&pir_misc);
    if (ret) {
        pr_err("pir: misc_register failed: %d\n", ret);
        goto err_gpio;
    }

    ret = device_create_file(pir_misc.this_device, &dev_attr_enable);
    if (ret) {
        pr_err("pir: create enable attr failed: %d\n", ret);
        goto err_misc;
    }

    ret = request_irq(irq_num, pir_irq, IRQF_TRIGGER_RISING,
                      "pir-out", NULL);
    if (ret) {
        pr_err("pir: request_irq %d failed: %d\n", irq_num, ret);
        goto err_attr;
    }

    pr_info("pir: registered, gpio=%d irq=%d, initial=%d\n",
            gpio_num, irq_num, pir_value);
    return 0;

err_attr:
    device_remove_file(pir_misc.this_device, &dev_attr_enable);
err_misc:
    misc_deregister(&pir_misc);
err_gpio:
    gpio_free(gpio_num);
    return ret;
}

static void __exit pir_exit(void)
{
    del_timer_sync(&pir_debounce);
    free_irq(irq_num, NULL);
    device_remove_file(pir_misc.this_device, &dev_attr_enable);
    misc_deregister(&pir_misc);
    gpio_free(gpio_num);
    pr_info("pir: unloaded\n");
}

module_init(pir_init);
module_exit(pir_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("project A");
MODULE_DESCRIPTION("HC-SR501 PIR character device");
