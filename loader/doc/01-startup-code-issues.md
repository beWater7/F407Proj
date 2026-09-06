# 启动代码 / 链接脚本 / Makefile 问题清单

工程：`bootloader/`（STM32F407ZGT6，Makefile + arm-none-eabi-gcc）

现象：`make` 能正常编译出 `build/loader.bin`，但烧录到板子上运行异常（不打印开机日志、或分区表/固件跳转逻辑错乱）。

排查后发现下面几个问题，均已修复。

## 1.（核心问题）`Reset_Handler` 缺少 `.data` / `.bss` 初始化

**文件**：`Libraries/CMSIS/Device/ST/STM32F4xx/Source/Templates/gcc/reset_handler.s`

工程里有两份 `Reset_Handler`：

- `startup_stm32f40xx.s` 里的是**弱符号**版本（标准 CMSIS 实现，包含正确的 `.data`/`.bss` 初始化）。
- `reset_handler.s` 里的是**强符号**版本（会覆盖前者），只做了：设置栈指针 → `SystemInit` → `FSMC_SRAM_Init` → `main`。

问题：**完全没有把 `.data` 段的初值从 Flash 拷贝到 RAM，也没有清零 `.bss` 段**。

后果：所有带初值的全局变量（比如 `main.c` 里的分区表 `gstFlashManage`、`spi_flash_table`、
`g_stSpiFlashPart` 等结构体）在 RAM 里的值其实是上电时的随机垃圾数据；未初始化的静态变量
也不是 0。这类问题不会导致编译/链接失败，只在运行时表现为随机性的错乱行为，非常隐蔽。

### 修复

在 `Reset_Handler` 里补上标准的拷贝/清零循环（源地址用链接脚本里的 `_etext`，
即 `.data` 段在 Flash 里的加载地址）：

```asm
Reset_Handler:
    ldr   r0, =_estack
    mov   sp, r0

    /* .data: 从 FLASH(_etext) 拷贝到 RAM(_sdata~_edata) */
    ldr   r0, =_sdata
    ldr   r1, =_edata
    ldr   r2, =_etext
CopyDataLoop:
    cmp   r0, r1
    bcs   CopyDataDone
    ldr   r3, [r2], #4
    str   r3, [r0], #4
    b     CopyDataLoop
CopyDataDone:

    /* .bss: 清零 (_sbss~_ebss) */
    ldr   r0, =_sbss
    ldr   r1, =_ebss
ZeroBssLoop:
    cmp   r0, r1
    bcs   ZeroBssDone
    movs  r2, #0
    str   r2, [r0], #4
    b     ZeroBssLoop
ZeroBssDone:

    bl    SystemInit
    bl    FSMC_SRAM_Init
    bl    main
1:  b     1b
```

## 2. `Makefile` 里 `PROJECT_DIR` 写死了旧的绝对路径

**文件**：`Makefile`

```makefile
PROJECT_DIR = E:/4project/f407-boot/bootloader   # 旧路径，工程已经挪到了 stm32f407 子目录下
```

工程目录后来挪到了 `E:/4project/stm32f407/f407-boot/bootloader`，但 `Makefile` 里的绝对路径
没有跟着更新。`$(shell find $(PROJECT_DIR)/... -name "*.c")` 在一个不存在的目录下会静默返回空，
可能导致源码收集不全甚至链接失败。

### 修复

```makefile
PROJECT_DIR = $(CURDIR)
```

用 `$(CURDIR)`（make 自动变量，表示当前工作目录）代替硬编码路径，不再受目录搬迁影响，
只要在 `bootloader/` 目录下执行 `make` 就能正确解析所有路径。

## 3. `_sbrk`（malloc 堆）用的是内部 SRAM，而不是链接脚本划分好的外部 PSRAM

**文件**：`User/app/syscalls.c`

`link.ld` 特意在外部 1MB PSRAM（`0x6C000000`）划了一段 `.heap` 区域，并定义了
`_heap_start` / `_heap_end` 符号，本意是给大块内存分配（比如 OTA 固件缓冲区）用。

但原来的 `_sbrk` 实现用的是内部 RAM 的 `_end` 符号：

```c
caddr_t _sbrk(int incr) {
    extern char _end;
    static char *heap_end;
    ...
}
```

而 `main.c` 里 `fw_upgrade()` 会 `malloc(stOtaFlag.len)`，最大可能到 `APP_FLASH_SIZE`（256KB），
内部 RAM 总共只有 128KB（还要装栈、`.data`、`.bss`），一旦真的分配这么大内存，
必然导致堆越界踩到栈或其它内存，造成随机崩溃。

### 修复

改用链接脚本里的 PSRAM 堆区，并加了越界保护：

```c
caddr_t _sbrk(int incr) {
    extern char _heap_start;
    extern char _heap_end;
    static char *heap_ptr = 0;
    char *prev_heap_ptr;

    if (heap_ptr == 0) heap_ptr = &_heap_start;
    prev_heap_ptr = heap_ptr;

    if (heap_ptr + incr > &_heap_end) {
        errno = ENOMEM;
        return (caddr_t) -1;
    }
    heap_ptr += incr;
    return (caddr_t) prev_heap_ptr;
}
```

## 4.（加固）`link.ld` 里 `FLASH` 区大小和实际分区设计不一致

**文件**：`link.ld`

项目文档（`iap.h` 里的 `BOOT_FLASH_SIZE`）明确 bootloader 只占用 `0x08000000~0x08008000`
（32KB），APP1 从 `0x08008000` 开始。但链接脚本把 `FLASH` 区写成了 `128K`：

```ld
FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 128K   /* 改之前 */
```

当前编译出的 bootloader 大小（约24KB）还没有超过32K，所以暂时没有触发问题，但只要以后
bootloader 代码增大超过32K，链接器不会报错，会直接把代码链接到 APP1 的地址空间，
物理烧录时会破坏 APP 固件分区，且很难第一时间发现。

### 修复

按文档设计收紧为32K，做边界保护：

```ld
FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 32K   /* 与 iap.h 的 BOOT_FLASH_SIZE 保持一致 */
```

## 5.（真正导致"烧录后板子没反应"的直接原因）`TIM3_IRQHandler` 被声明成了 `static`

**文件**：`User/bsp/timer/bsp_timer.c`

```c
static void TIM3_IRQHandler(void)   /* 问题代码 */
```

`main()` 里第二步就调用 `TIM3_init()`，会使能 TIM3 更新中断。但这个中断服务函数被声明成
`static`（仅文件内部可见），链接器没办法用它覆盖启动文件里的**弱符号** `TIM3_IRQHandler`
（默认指向 `Default_Handler`，就是一个死循环 `b Infinite_Loop`）。

后果：开机大约10~20ms后，TIM3中断一触发，CPU 就跳进 `Default_Handler` 死循环，永远出不来。
这个时间点还在串口初始化 `Debug_USART_Config()` **之前**，所以连一行开机日志都打不出来，
表现出来就是"烧录后板子完全没反应"。

排查这个问题用到的具体指令和思路见 [03-debug-process.md](./03-debug-process.md)。

### 修复

去掉 `static`，让它成为可以覆盖弱符号的全局符号：

```c
void TIM3_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM3, TIM_IT_Update) == SET) { /* ... */ }
    Tim3Delay_Decrement();
    TIM_ClearITPendingBit(TIM3, TIM_IT_Update);
}
```

同时检查了工程里其它所有 `*_IRQHandler`，确认没有同类问题。

## 排查/修复效果验证

用 pyOCD + 野火 CMSIS-DAP 直接连板子验证（无需重新烧录旧固件即可复现问题现场）：

- 修复前：halt 后 `PC = 0x08000188`（`Default_Handler` 死循环入口），`ICSR` 显示当前活动
  异常号为 45（即 `TIM3_IRQn`）。
- 修复后：重新编译烧录后，`PC` 停在 `SPI_FLASH_Init`/USART 相关代码里，且
  `current_time`（`SysTick_Handler` 每 1ms 自增的全局变量）在数秒内持续增长，
  证明中断系统和全局变量初始化都恢复正常，程序真正跑起来了。
