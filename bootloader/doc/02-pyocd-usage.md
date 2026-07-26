# pyOCD 使用方法（野火 CMSIS-DAP 烧录 / 在线调试）

## 为什么用 pyOCD

本工程是纯 Makefile 工程，没有 Keil 工程文件。如果用 Keil 烧录 `.bin`，需要先建一个
"空壳"工程只用来配置调试器和 Flash 算法，再用调试器命令窗口手动 `LOAD` 命令，步骤繁琐。

**pyOCD** 是一个开源的 Python 命令行调试/烧录工具，原生支持 CMSIS-DAP 协议
（野火 DAP 普通版正是 CMSIS-DAP 协议兼容设备），装好之后一条命令就能把 `.bin` 写到指定地址，
非常适合配合 Makefile 工作流使用。

## 安装

```powershell
pip install -U pyocd
```

安装完成后，第一次针对某个芯片型号使用时，需要下载对应的 CMSIS Pack（里面包含该芯片的
Flash 编程算法），只需要装一次，之后会缓存在本地：

```powershell
pyocd pack find stm32f407          # 查找匹配的芯片型号（会先下载一次 pack 索引，比较慢，耐心等）
pyocd pack install stm32f407zgtx   # 按实际芯片型号安装对应 pack（本工程用的是 STM32F407ZGT6）
```

## 常用命令

### 1. 查看已连接的调试器

```powershell
pyocd list
```

正常能看到类似输出（野火 DAP 会被识别为 CMSIS-DAP 设备）：

```
#   Probe/Board                     Unique ID                  Target
-------------------------------------------------------------------------
0   Embedfire fire CMSIS-DAP lite   057d270f5c13000023580bc8   n/a
```

如果这里看不到设备，先检查 USB 连接、驱动是否正常安装。

### 2. 烧录 bin 文件

```powershell
pyocd load "build/loader.bin" -t stm32f407zgtx --base-address 0x08000000
```

- `-t` 指定目标芯片型号（对应 `pyocd list --targets` 里的名字，或者用 `pack find` 查到的名字）。
- `--base-address` 指定 `.bin` 文件要写入的起始地址。**必须跟链接脚本 `link.ld` 里
  `FLASH` 区的 `ORIGIN` 保持一致**（本工程是 `0x08000000`），否则地址不对程序跑不起来。
- 默认按扇区擦除（只擦本次要写入范围覆盖到的扇区），不会动到其它没有涉及的 Flash 区域
  （比如本工程的 APP1/APP2 分区）。

典型输出：

```
Erased 32768 bytes (2 sectors), programmed 24576 bytes (24 pages), skipped 0 bytes (0 pages) at 9.38 kB/s
```

### 3. 复位运行

```powershell
pyocd reset -t stm32f407zgtx
```

### 4. 在线调试 / 检查内核状态（Commander）

`pyocd commander` 提供一个可以连上目标、执行调试命令的交互/半交互工具，
用 `-c` 可以在非交互模式下依次执行命令：

```powershell
# 连接方式：attach = 不复位直接连接（保留现场，适合排查"卡死"问题）
pyocd commander -t stm32f407zgtx -M attach -c "status"

# halt 住内核，看核心寄存器（PC/SP/LR等），排查卡在哪里很有用
pyocd commander -t stm32f407zgtx -M attach -c "halt" -c "reg pc sp lr"

# 读内存/寄存器（比如读 Cortex-M 的 ICSR 系统寄存器）
pyocd commander -t stm32f407zgtx -M attach -c "halt" -c "read32 0xE000ED04"

# 读某个全局变量的值（地址从 build/xxx.map 里查）
pyocd commander -t stm32f407zgtx -M attach -c "read32 0x200005a8"

# 复位目标
pyocd commander -t stm32f407zgtx -M attach -c "reset"
```

常用的 `-M`（`--connect`）连接模式：

| 模式 | 说明 |
| --- | --- |
| `attach` | 连接但不复位，保留目标当前运行状态（现场），适合排查"跑到哪儿卡住了" |
| `halt` | 连接后立即halt，程序不会继续跑（默认模式） |
| `pre-reset` | 复位前连接 |
| `under-reset` | 复位期间保持连接（适合有 boot 保护/时序要求苛刻的芯片） |

### 5. 配合 GDB 单步调试（更复杂问题时用）

```powershell
pyocd gdbserver -t stm32f407zgtx
```

然后在另一个终端用 `arm-none-eabi-gdb build/loader.elf`，`target remote localhost:3333`
即可用 GDB 完整单步、下断点调试（比裸读寄存器更适合复杂逻辑问题）。

## 建议：集成进构建脚本

可以把烧录命令加进 `buildBoot.sh`，编译完自动烧录，省得每次手动敲命令：

```sh
#!/bin/sh
make -j 8
pyocd load build/loader.bin -t stm32f407zgtx --base-address 0x08000000
echo "success!"
```
