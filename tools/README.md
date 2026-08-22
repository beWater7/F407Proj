# tools — 脚本说明

本目录是**纳入版本库**的工程工具，给编译/烧录/clangd 用。本机 VM、USB、udev 等一次性部署脚本在 `../localtools/`（默认 gitignore，不提交）。

日常开发一般不用直接进这里：`./app/buildApp.sh`、`./bootloader/buildBoot.sh`、`./flash.sh` 会自己调用下面的包装。

## 本目录

| 文件 | 干什么 |
|------|--------|
| `pyocd.sh` | 包装系统里的 `pyocd`：强制 hidraw、检查野火 CMSIS-DAP 是否在，烧录类命令默认 `--no-wait` |
| `env-pyocd.sh` | 被 `pyocd.sh` / 其它脚本 `source`，设置 `PYOCD_USB_BACKEND` 和 `PYTHONPATH` |
| `hid_hidraw_shim/` | 把 Python 的 `hid` 模块换成 Linux `hidraw`，否则野火 DAP 经常识别不到 |
| `gen-compdb.sh` | 用 bear 分别给 `app/`、`bootloader/` 生成 `compile_commands.json`，给 clangd 跳转用 |

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
make -C bootloader compdb
```

生成后装 clangd 扩展并重载窗口。打开 `app/` 下的文件走 APP 库，打开 `bootloader/` 下的走 Boot 库。

## 仓库根目录相关脚本（不在本目录）

| 脚本 | 干什么 |
|------|--------|
| `../flash.sh` | 只烧录不编译：`app` / `boot` / `web` / `all` / `reset`，可组合如 `app web`。APP/Boot 走 SWD；web 在 SPI，等板子 ping 通后 POST `/protocol/system/upload`。烧 APP 时写内部 Flash cookie，避免 Boot 用 SPI 残留 OTA 盖掉刚烧的镜像 |
| `../app/buildApp.sh` | 只编 APP；`1`/`flash` 编完调 `flash.sh app`（不烧 web）。SPI 网页用 `../flash.sh web`；片内 ROM 页来自 `web/rom` → `fsdata.c` |
| `../bootloader/buildBoot.sh` | 只编 Boot；`1`/`flash` 编完调 `flash.sh boot` |
| `../serialTerm.sh` | 打开 CH340 串口终端，日志默认写 `~/gitProj/log` |

```bash
./flash.sh app          # 已有 app/build/app.bin
./flash.sh web          # 打包 web/，HTTP 烧 SPI
./flash.sh app web
./flash.sh boot
./flash.sh all
./app/buildApp.sh 1     # 编译并 flash.sh app（ROM 页随固件；完整 UI 用 flash.sh web）
./bootloader/buildBoot.sh 1  # 编译并 flash.sh boot
./serialTerm.sh
```

## 本机环境（`../localtools/`）

| 脚本 | 干什么 |
|------|--------|
| `vmware-usb-autoconnect.ps1` | 在 **Windows 主机** 改 VMware `.vmx`，开机/插上后自动把 USB 网口、DAP、CH340 连进 Ubuntu |
| `vm-dev-setup.sh` | 进 Ubuntu 后跑一次：等 USB、给 RTL8153 配 `192.168.137.10/24`（不抢默认路由）、检查串口/DAP 权限 |
| `fix-cmsis-dap-udev.sh` | 一次性写 udev，免 sudo 用 DAP |

说明见 `../localtools/VM-DEV-SETUP.md`。
