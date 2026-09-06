# "烧录后板子没反应"问题排查过程记录

## 现象

按照 [02-pyocd-usage.md](./02-pyocd-usage.md) 里的方法把 `loader.bin` 烧录进板子、复位后，
板子没有任何反应（串口无输出，看起来像完全没跑起来）。

在此之前已经修复过一版启动代码的 `.data`/`.bss` 初始化问题（见文档1第1节），
但修复后重新编译烧录，问题依旧存在，说明还有其它原因。

## 排查思路

对于"烧录进去了但程序看起来完全没反应"这类问题，比盲目改代码更高效的方式是：
**直接用 SWD 连上正在运行（或已经卡死）的目标，把 CPU 现场"拍下来"看它到底停在哪里**。
Cortex-M 的核心寄存器（尤其是 PC）+ 系统寄存器（ICSR 里的当前活动异常号）
几乎能一次定位问题所在函数/中断。

具体步骤如下。

## 步骤 1：确认调试器能连上，且核心没有进入 HardFault

```powershell
pyocd commander -t stm32f407zgtx -M attach -c "status" -c "reg pc sp lr" -c "read32 0xE000ED28"
```

- `-M attach`：连接但不复位，保留目标当前的运行现场（这一点很关键，如果用默认的
  `halt` 模式连接会先复位，反而破坏了"卡死时的现场"）。
- `0xE000ED28` 是 Cortex-M4 的 `CFSR`（Configurable Fault Status Register），
  如果是硬件异常（HardFault/BusFault/UsageFault等）这里会非0。

结果：

```
Core 0 (Cortex-M4):  Running
e000ed28:  00000000                               |....|
```

`CFSR = 0`，说明**不是**总线错误/内存访问错误/非法指令这类硬件异常，核心还在正常"运行"
（只是不知道在运行什么）。这排除了一大类可能性（比如指针越界、栈溢出踩到非法地址等），
把怀疑范围收窄到"卡在某个死循环里"。

## 步骤 2：halt 住内核，看 PC 停在哪

```powershell
pyocd commander -t stm32f407zgtx -M attach -c "halt" -c "reg pc sp lr" -c "reg r0 r1 r2 r3"
```

结果：

```
pc = 0x08000188
sp = 0x2001ffa8
lr = 0xfffffff9
```

`lr = 0xFFFFFFF9` 是 Cortex-M 的 EXC_RETURN 特征值，说明**当前正处于某个异常/中断处理函数里**
（不是普通的函数调用返回地址），这是个很重要的线索——问题很可能出在中断里，不是普通代码逻辑卡死。

## 步骤 3：查 `.map` 文件，看 PC 地址对应哪个符号

```powershell
# 在 build/loader.map 里搜索 0x08000188 附近的符号
Select-String -Path build\loader.map -Pattern "0x08000188"
```

在 map 文件里查到 `0x08000188` 正好是一大堆外设中断向量（`OTG_HS_IRQHandler`、`USART2_IRQHandler`
……等等）"别名"共同指向的地址，紧接着后面就是 `Reset_Handler`（`0x0800018c`）。
结合启动文件 `startup_stm32f40xx.s` 的源码可以确认：这些外设中断的弱符号全部通过
`.thumb_set XXX_IRQHandler,Default_Handler` 指向同一个地方——`Default_Handler`
的函数体就是一句死循环 `b Infinite_Loop`。

**结论：CPU 卡在了某个外设的默认中断处理函数（死循环）里，说明该外设的中断被使能了，
但对应的中断服务函数没有真正生效（很可能被弱符号覆盖失败）。**

## 步骤 4：确定具体是哪个外设中断——读 ICSR 的 VECTACTIVE 字段

光知道"卡在 Default_Handler"还不知道具体是哪个中断，需要读 Cortex-M 的
`ICSR`（Interrupt Control and State Register，地址 `0xE000ED04`），
它的 bit[8:0]（`VECTACTIVE`）就是当前正在执行的异常/中断编号。

```powershell
pyocd commander -t stm32f407zgtx -M attach -c "halt" -c "read32 0xE000ED04"
```

结果：

```
e000ed04:  0400f82d
```

取低9位：`0x0400F82D` 换算成二进制取 bit[8:0] = `0b0_00101101` = `45`。

Cortex-M 里异常号 0~15 是核心异常（Reset/NMI/HardFault…），从 16 开始才是外部 IRQ，
即 `IRQn = VECTACTIVE - 16 = 45 - 16 = 29`。

对照 `startup_stm32f40xx.s` 里向量表的外设中断顺序数一下（从 IRQ0 开始数到 IRQ29），
第29个正好是 **`TIM3_IRQHandler`**。

## 步骤 5：回到源码，找 `TIM3_IRQHandler` 真正的实现

```powershell
# 在 bootloader/User 目录下搜索 TIM3_IRQHandler 相关定义
Select-String -Path bootloader\User\**\*.c -Pattern "TIM3_IRQHandler"
```

在 `User/bsp/timer/bsp_timer.c` 里发现：

```c
static void TIM3_IRQHandler(void)   /* 问题所在 */
```

**找到根因**：这个函数被声明成了 `static`（C语言里表示只在当前文件内可见的内部符号），
链接的时候它不会作为一个"强符号"去覆盖启动文件里同名的"弱符号" `TIM3_IRQHandler`
（弱符号默认等价于 `Default_Handler`）。于是最终链接进向量表里的 `TIM3_IRQHandler`
地址，实际还是 `Default_Handler` 的死循环，跟 `bsp_timer.c` 里写的那个 `static` 函数完全没关系。

而 `main()` 里第二行就调用了 `TIM3_init()`，会立刻使能 TIM3 的更新中断
（10ms周期）。所以开机大约10~20ms后，第一次 TIM3 中断一触发，CPU 就永远卡死在
`Default_Handler` 里出不来了——这个时间点比串口初始化 `Debug_USART_Config()`
还早，所以连一行开机日志都打印不出来，从外部看就是"烧录后板子完全没反应"。

## 修复

去掉 `static`：

```c
void TIM3_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM3, TIM_IT_Update) == SET) { /* ... */ }
    Tim3Delay_Decrement();
    TIM_ClearITPendingBit(TIM3, TIM_IT_Update);
}
```

同时顺手在整个工程里 grep 了一遍所有 `*_IRQHandler` 的定义，确认没有同类问题。

## 修复效果验证

重新编译烧录后，同样的方法再验证一遍：

```powershell
pyocd commander -t stm32f407zgtx -M attach -c "reset"
Start-Sleep -Seconds 2
pyocd commander -t stm32f407zgtx -M attach -c "halt" -c "reg pc sp lr" -c "read32 0xE000ED04"
```

- `PC` 不再停在 `0x08000188`（`Default_Handler`），而是停在 `SPI_FLASH_Init`/USART 相关的
  正常业务代码里，且 `ICSR = 0`（没有异常/中断处于活动状态，说明是在主线程正常运行，
  而不是卡在某个中断里）。
- 连续两次读取 `current_time`（`SysTick_Handler` 每1ms自增的全局变量，地址从 `.map`
  文件里查到是 `0x200005a8`）：

  ```powershell
  pyocd commander -t stm32f407zgtx -M attach -c "read32 0x200005a8"
  ```

  两次读到的值在几秒间隔内持续增长（如 `0x33cb → 0x3fc6`），跟 1ms 一次的节拍量级吻合，
  证明 `SysTick` 中断、`TIM3` 中断都在正常工作，程序真正跑起来了，问题解决。

## 这套方法的通用性

遇到"烧录后设备没反应/卡死"类问题时，比起凭经验改代码再重新烧录试错，更推荐这个流程：

1. `pyocd commander -M attach` 保留现场连接（别用默认的 halt/复位模式，会破坏卡死现场）。
2. 先看 `CFSR`（`0xE000ED28`）排除硬件异常（HardFault等）。
3. `halt` 后看 `PC`/`LR`：`LR` 是 `0xFFFFFFF9`/`0xFFFFFFFD` 等 EXC_RETURN 特征值，
   说明卡在中断/异常里；否则是普通代码逻辑卡死（死循环、死等某个状态位等）。
4. 查 `.map` 文件确认 `PC` 对应哪个函数/符号。
5. 如果卡在 `Default_Handler`，读 `ICSR`（`0xE000ED04`）的 `VECTACTIVE` 字段
   （bit[8:0]，减16就是 IRQn），对照启动文件的向量表顺序找到具体外设，
   然后重点检查该外设中断服务函数是不是命名错误 / 被 `static` 修饰 / 没有被编译进去。
