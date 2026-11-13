# 嵌入式实习通用八股（C / OS / Linux / 总线 / 手写）

> 用法：面试前一天过一遍，答不上来的标记出来。
> 这是"通用地基"，项目相关的追问看 `project-a-question-bank.md` 和 `project-b-question-bank.md`。

---

## 一、C 语言（15）

**1. `volatile` 的作用？**
告诉编译器每次都要从内存读，不能优化掉。用于：硬件寄存器、中断里改的变量、多线程共享标志。
追问：`volatile` 不保证原子性，也不解决竞态。

**2. `const` 放在不同位置的含义？**

```c
const char *p;        // 指向的内容不能改
char * const p;       // 指针本身不能改
const char * const p; // 都不能改
```

**3. 指针和数组的区别？**
数组名是地址常量，`sizeof` 给整个数组大小；指针是变量，`sizeof` 给指针大小。数组传参会退化成指针。

**4. 结构体内存对齐规则？**
每个成员按自身大小对齐，结构体总大小是最宽成员大小的整数倍。可以用 `__attribute__((packed))` 取消，但会牺牲访问效率。

**5. `sizeof` 和 `strlen` 的区别？**
`sizeof` 是编译期运算符（含 `\0`），`strlen` 是运行期函数（不含 `\0`）。

**6. 野指针和悬空指针？**
野指针：未初始化。悬空指针：指向已释放内存。防范：初始化为 NULL，释放后置 NULL。

**7. 内存分区？**
栈（局部变量，自动回收）、堆（malloc）、全局/静态区、常量区、代码区。

**8. `static` 的三个作用？**
函数内：延长生命周期；全局变量/函数：限制在本文件；C++ 类内：属于类。

**9. 位操作写出置位/清位/取反/取某位？**

```c
reg |=  (1u << n);    // 置位
reg &= ~(1u << n);    // 清位
reg ^=  (1u << n);    // 翻转
bit = (reg >> n) & 1; // 取值
```

**10. 大小端怎么判断？**

```c
int x = 1;
((char *)&x)[0] == 1 ? 小端 : 大端
```

**11. 函数指针有什么用？**
回调、状态机表、驱动 ops 结构体。本项目 ISO-TP 的发送回调就是函数指针。

**12. 宏和 inline 的区别？**
宏是文本替换（无类型检查），inline 是编译期展开的函数（有类型检查）。

**13. 环形缓冲区怎么实现？**
读写索引对容量取模，判空 `w == r`，判满 `(w+1) % N == r`（留一个空位）。单生产者单消费者可无锁。

**14. 中断里为什么不能 `printf`/`malloc`？**
都可能阻塞或不可重入。用 flag + 主循环处理。

**15. 编译报 "multiple definition" 怎么查？**
头文件里定义了变量/函数实体，应改成 `extern` 声明 + 单个 .c 定义。

---

## 二、操作系统（8）

**1. 进程和线程的区别？**
进程有独立地址空间，线程共享。切换线程更便宜。通信方式不同（IPC vs 共享内存）。

**2. 线程同步的方式？**
互斥锁、信号量、条件变量、读写锁、原子操作。

**3. 死锁的四个条件？**
互斥、占有且等待、不可剥夺、循环等待。破坏任意一个即可。

**4. 中断上下文为什么不能睡眠？**
中断栈没有 task_struct，没法调度；睡眠会让系统卡死。要延迟处理用 workqueue/threaded IRQ。

**5. 自旋锁和互斥锁怎么选？**

```text
临界区短、不能睡（中断上下文）-> 自旋锁
可能睡眠、临界区长 -> 互斥锁
```

**6. 优先级反转是什么？**
低优先级持锁，高优先级等锁，中优先级抢占低优先级 → 高优先级被中优先级阻塞。解决：优先级继承。

**7. 内存管理：虚拟内存的好处？**
隔离、按需分配、可换出、地址连续假象。

**8. 用户态和内核态怎么切换？**
系统调用、中断、异常。切换有开销（保存上下文）。

---

## 三、Linux 驱动（8）

**1. 字符设备注册流程？**
`alloc_chrdev_region` → `cdev_init` + `cdev_add` → 实现 `file_operations` → 创建 `/dev` 节点。

**2. `file_operations` 常用成员？**
`open/release/read/write/ioctl/poll/mmap/llseek`。

**3. `copy_to_user` 为什么不能直接赋值？**
用户空间指针不能在内核直接解引用（地址空间不同、可能非法）。必须用 copy_* 并检查返回值。

**4. 内核模块的 `__init`/`__exit` 有什么用？**
让 init 代码在初始化后释放内存（`__init` 段），`__exit` 在不能卸载时也省内存。

**5. 内核里怎么分配内存？**
`kmalloc`（小、连续）、`vmalloc`（大、虚拟连续）、`kmem_cache`（固定大小高频）。

**6. 中断号和 GPIO 号的关系？**
GPIO 控制器把 GPIO 映射成中断号，`gpiod_to_irq()` 拿中断号再 `request_irq`。

**7. 设备树里 `status = "okay"/"disabled"` 意味着什么？**
disabled 的节点不会被创建成设备（驱动不会 probe）。

**8. 模块 vermagic 不一致会怎样？**
`insmod` 直接拒绝："version magic ... should be ..."。要求内核版本、SMP/PREEMPT、modversions 等一致。

---

## 四、总线与协议（6）

**1. I2C 和 SPI 的区别？**
I2C 两线、多设备寻址、速度低；SPI 四线、片选、全双工、速度高。

**2. CAN 的位定时怎么算？**
`tq = 1 / (fclk / brp)`，一个位 = sync + prop + phase1 + phase2；采样点 = (1+prop+phase1)/总 tq。

**3. 采样点不一致会怎样？**
接收方采样时刻落在边沿附近 → 误码 → ACK error / 总线错误。本项目 Linux 0.866 vs STM32 0.75 就是这个坑。

**4. UDS 的 NRC 0x78 是什么？**
responsePending，告诉测试端"还在处理，别超时"。

**5. 为什么诊断要用 ISO-TP？**
CAN 单帧只有 8 字节，诊断报文可能几百字节，需要分段重组 + 流控。

**6. CAN 总线终端电阻？**
两端各 120Ω，并联后 60Ω；缺一端会反射、误码率上升。

---

## 五、手写代码（8 题，15 分钟内）

**1. 反转单链表（迭代）**

```c
struct node *rev(struct node *h) {
    struct node *p = NULL, *n;
    while (h) { n = h->next; h->next = p; p = h; h = n; }
    return p;
}
```

**2. 判断链表有环（快慢指针）**

```c
while (fast && fast->next) {
    slow = slow->next; fast = fast->next->next;
    if (slow == fast) return 1;
}
return 0;
```

**3. 字符串反转（原地）**

```c
for (i = 0, j = len - 1; i < j; i++, j--) { c = s[i]; s[i] = s[j]; s[j] = c; }
```

**4. memcpy（考虑重叠用 memmove 语义）**

```c
if (dst < src) for (i = 0; i < n; i++) d[i] = s[i];
else           for (i = n; i > 0; i--) d[i-1] = s[i-1];
```

**5. 统计整数二进制里 1 的个数**

```c
while (x) { x &= x - 1; n++; }
```

**6. 大小端转换**

```c
v = ((v & 0xFF) << 24) | ((v & 0xFF00) << 8) | ((v >> 8) & 0xFF00) | ((v >> 24) & 0xFF);
```

**7. 环形缓冲区（读写 + 判满）**

```c
#define N 64
static uint8_t buf[N]; static volatile uint32_t w, r;
int full(void)  { return ((w + 1) & (N - 1)) == r; }
int empty(void) { return w == r; }
void put(uint8_t c) { if (!full()) { buf[w] = c; w = (w + 1) & (N - 1); } }
uint8_t get(void)   { uint8_t c = buf[r]; r = (r + 1) & (N - 1); return c; }
```

**8. 冒泡/快排（写一个能过的）**

```c
void qsort_(int *a, int l, int r) {
    int i = l, j = r, p = a[(l + r) / 2], t;
    while (i <= j) {
        while (a[i] < p) i++;
        while (a[j] > p) j--;
        if (i <= j) { t = a[i]; a[i] = a[j]; a[j] = t; i++; j--; }
    }
    if (l < j) qsort_(a, l, j);
    if (i < r) qsort_(a, i, r);
}
```

---

## 六、面试前 30 分钟过一遍

```text
1. 项目两句话介绍（30 秒版）
2. 三个"为什么"：采样点 0.75 / 为什么 input 不选字符设备 / 为什么反编译 DTB
3. 一个"没做"的诚实回答：24h/50 次 OTA、签名验签、断点续传
4. 反问：团队用什么芯片/RTOS、实习生负责哪部分
```
