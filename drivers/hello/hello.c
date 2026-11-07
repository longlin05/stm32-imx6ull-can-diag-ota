#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>

static int __init hello_init(void)
{
    pr_info("hello_ko: loaded, kernel=%s\n", utsname()->release);
    return 0;
}

static void __exit hello_exit(void)
{
    pr_info("hello_ko: unloaded\n");
}

module_init(hello_init);
module_exit(hello_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("project A");
MODULE_DESCRIPTION("vermagic / Module.symvers smoke test");
