# tools — 脚本说明

本目录是**纳入版本库**的工程工具，给编译/烧录/clangd 用。本机 VM、USB、udev 等一次性部署脚本在 `../localtools/`（默认 gitignore，不提交）。

日常开发一般不用直接进这里：仓库根 `make`、`./flash.sh` 会调用下面的包装。

| `pack/` | `genUpgBin.py`、`genSpiImage.py`、`make_loader_blob.py`、`genWebBin.py`、`gen_fsdata.py` — 出固件/SPI/网页镜像 |
| `serial/` | `serialTerm.py` / `serialTerm.sh`、`xmodem_send.py` — 串口终端与 XMODEM 恢复 |
| `util/` | `fileToHex.py` — 文件/字符串转 C hex 数组 |

Python 脚本主要给 **打包 / 串口调试 / pyOCD** 用；串口烧 `upg.bin` 用 `xfer/` 子工程（C）。

## 本目录

| 文件 | 干什么 |
|------|--------|
| `xfer/` | **xfer 串口烧录工具（独立子工程）**：`flash_upg.c` / `xfer_uart.c` / `xfer.h`（传输层）/ `xfer_config.h`（可调配置）/ `Makefile` → 编出 `xfer/xfer` |
| `Makefile` | 转发到 `xfer/`：`make -C tools` ≡ `make -C tools/xfer` |
| `pyocd.sh` | 包装系统里的 `pyocd`：强制 hidraw、检查野火 CMSIS-DAP 是否在，烧录类命令默认 `--no-wait` |
| `env-pyocd.sh` | 被 `pyocd.sh` / 其它脚本 `source`，设置 `PYOCD_USB_BACKEND` 和 `PYTHONPATH` |
| `hid_hidraw_shim/` | 把 Python 的 `hid` 模块换成 Linux `hidraw`，否则野火 DAP 经常识别不到 |
| `gen-compdb.sh` | 用 bear 分别给 `app/`、`bootloader/` 生成 `compile_commands.json`，给 clangd 跳转用 |

### `xfer/` 子工程与进度条配置

源码 `xfer/flash_upg.c`（主逻辑）、`xfer/xfer_uart.c` + `xfer/xfer.h`（UART 传输层，可扩 USB），
**可调配置集中在 `xfer/xfer_config.h`**（进度条样式、默认波特率/重试次数；协议常量如 SOH/魔数是对端约定值，留在 `.c`）。
`tools/Makefile` 转发到子工程，两种编法等价：

```bash
make -C tools          # 转发, 等价于下一行
make -C tools/xfer     # 单独编, 可带 EXTRA_CFLAGS
```

进度条默认在终端画 `[####----]` 彩条（绿），重定向到文件/管道时自动退化为逐行日志。

一次烧录按三步计时与显示（条/动画实时刷新）：
1. `reset & detect`：复位脉冲 + 等 boot/loader 横幅（无真实总量，显示"进行中 + 秒数"旋转动画，不伪造百分比）；
2. `enter upgrade`：发 `upgrade`/CLI 进入 YMODEM 并握手（同上动画）；
3. `YMODEM transfer`：字节条，附加**实时平均速率 / 已耗时 / ETA**；loader 擦写 Flash 造成 ACK 空窗时条仍按 500ms 周期刷新耗时，不显得卡死。

传输结束自动打印汇总：`upgrade done in 33.6s (3 steps): reset & detect 2.4s + enter upgrade 1.8s + YMODEM transfer 29.4s (12.1 KB/s)`。
`-v/--verbose`（回显板子日志）或非 tty 输出时，步骤退化为逐行文本，动画关闭。

### 高速 YMODEM 握手（双向确认，~4x 提速）

日常 `upg.bin` 的 YMODEM 传输段不是全程 115200：loader（新固件）在 115200 打出
`Waiting for upg.bin ...` 后追加一行 `BAUD 460800`，然后在 115200 **等 xfer 回确认字节
`'B'`（≤1s）**；收到才切 460800 发 'C'，收不到就留在 115200。xfer 解析到 BAUD 行就回
`'B'`、本地同步切速，随后在 460800 完成整包传输。传输结束 loader 把波特率切回 115200。

- **双向确认的意义**：loader 收不到 `'B'` 就不会切速，所以 xfer 单方面"想留 115200"
  （`--no-fast` / 显式 `-b 115200`）**能真正钉住**，不会再被 loader 无脑切走。
- **旧 loader 固件（无 `BAUD` 行）自动兼容**：xfer 嗅探不到就保持 115200，流程与从前一致。
- **失败自动回退**：握手/传输失败 → 复位重试，每次尝试都从基线波特率重新协商；xfer 重试前会把串口切回基线。
- **强制固定速率**：任何**显式 `-b`** 都进 force 模式（自动等效 `--no-fast`）——不回确认字节、
  全程用该速率。`-b 115200` 对新 loader 有效（loader 留 115200）；强制非基线档
  （如 `-b 460800`）仍要求 loader 同样固定/已停在目标速率，日常不要用。
- **USB-TTL 在 460800 不稳时**：加 `--no-fast` 强制全程 115200（loader 留基线，真正生效）。
- **速率常量两端同步**：loader 侧在 `loader/User/app/upgrade/download.c` 的 `UPG_FAST_BAUD`；
  该值必须落在 `xfer_uart.c` `baud_to_speed()` 支持的档位（230400/460800/921600）。
  改其中一端必须同步另一端。

```markdown
| 宏 | 值 | 效果 |
|-----|-----|-----|
| `XFER_PROGRESS_BAR` | `1`(默认) / `0` | 画条 / 退回纯文本百分比 |
| `XFER_PROGRESS_COLOR` | `1`绿(默认) `2`青 `3`黄 `0`无色 | 条的颜色 |
| `XFER_DEFAULT_BAUD` | `115200`(默认) | 未加 `-b` 时波特率 |
| `XFER_DEFAULT_RETRIES` | `3`(默认) | 未加 `--retries` 时自动重试次数 |
```

改法示例（覆盖宏，不必动源文件）：

```bash
make -C tools/xfer EXTRA_CFLAGS=-DXFER_PROGRESS_COLOR=3   # 黄色进度条
make -C tools/xfer EXTRA_CFLAGS=-DXFER_PROGRESS_COLOR=0   # 无色（终端不落地 ANSI 码）
make -C tools/xfer EXTRA_CFLAGS=-DXFER_DEFAULT_BAUD=921600
```

想用任意 ANSI 色可覆盖 `PROG_BAR_COLOR` / `PROG_BAR_RESET`，例如
`make -C tools/xfer EXTRA_CFLAGS='-DPROG_BAR_COLOR="\\x1b[35m"'`（品红）。

### `pyocd.sh`

Linux 下直接跑 `pyocd` 往往找不到野火 CMSIS-DAP lite（`0484:a030`）。本包装会：

1. `source env-pyocd.sh`
2. 对 `load` / `flash` / `erase` / `reset` / `gdbserver` / `commander` 自动加上 `--no-wait`（探针没插上时不会一直卡在 Waiting）
3. 先 `pyocd list`，没有探针就报错退出

```bash
./tools/pyocd.sh list
./tools/pyocd.sh load app/build/app.bin -t stm32f407zgtx --base-address 0x08008000
```

虚拟机里若提示未检测到调试器：VMware「可移动设备」把 DAP 连到本 VM，或跑 `../localtools/vm-dev-setup.sh`。

### `env-pyocd.sh`

不要直接执行，给其它脚本 source：

```bash
. "$PROJ_ROOT/tools/env-pyocd.sh"
```

设置：

- `PYOCD_USB_BACKEND=hidapiusb`
- `PYTHONPATH` 加上 `hid_hidraw_shim/`

### `hid_hidraw_shim/hid.py`

极小的 shim：`import hid` 时实际用 `hidraw`。配合 udev 的 `/dev/hidraw*` 权限，pyOCD 才能打开野火 DAP。不要单独运行。

DAP 权限不够时，在 Ubuntu 跑一次：

```bash
./localtools/fix-cmsis-dap-udev.sh
```

### `gen-compdb.sh`

为 **APP、Boot 各生成一份** `compile_commands.json`，不要合并到仓库根（两边有同名文件，合并后 clangd 会跳错树）。

依赖：`sudo apt install bear`

```bash
./tools/gen-compdb.sh
# 或
make -C app compdb
make -C loader compdb
make -C boot compdb
```

生成后装 clangd 扩展并重载窗口。打开 `app/` 下的文件走 APP 库，打开 `bootloader/` 下的走 Boot 库。

## 仓库根目录相关脚本（不在本目录）

| 脚本 | 干什么 |
|------|--------|
| `../flash.sh` | SWD 只烧录：`boot`（固化 boot.bin）/ `app` / `web` / `reset`。日常升级用 `../tools/xfer/xfer` |
| `../app/buildApp.sh` | 只编 APP；`1`/`flash` 编完调 `flash.sh app` |
| `../loader/buildBoot.sh` | 只编 loader.bin |
| `../serialTerm.sh` | 打开 CH340 串口终端（转发到 `serial/serialTerm.sh`） |

```bash
./flash.sh boot         # SWD 固化 boot.bin（只烧一次）
./flash.sh app
make && ./tools/xfer/xfer /dev/ttyUSB0 dist/upg.bin     # 串口升级
make && ./tools/xfer/xfer /dev/ttyUSB0 dist/f407zg.dtb  # 只烧 dtb（动态分区表）
make +web
./app/buildApp.sh 1
./serialTerm.sh
```

> **串口互斥提示**：`xfer` 与 `serialTerm` 共享同一套独占锁（Linux `flock` + `TIOCEXCL`）。
> 一方占用时另一方会明确提示被占用：`serialTerm` 弹「串口被占用」对话框，
> `xfer` 报 `port busy` 并列出占用进程 pid/名字。
> 对不参与锁协议的裸 open 程序（旧版终端、`minicom`、`cat > /dev/ttyUSB0`），
> `xfer` 打开后还会扫 `/proc` 给出 WARN 提示，避免两边抢字节导致烧录乱码。

## 本机环境（`../localtools/`）

| 脚本 | 干什么 |
|------|--------|
| `vmware-usb-autoconnect.ps1` | 在 **Windows 主机** 改 VMware `.vmx`，开机/插上后自动把 USB 网口、DAP、CH340 连进 Ubuntu |
| `vm-dev-setup.sh` | 进 Ubuntu 后跑一次：等 USB、给 RTL8153 配 `192.168.137.10/24`（不抢默认路由）、检查串口/DAP 权限 |
| `fix-cmsis-dap-udev.sh` | 一次性写 udev，免 sudo 用 DAP |

说明见 `../localtools/VM-DEV-SETUP.md`。
