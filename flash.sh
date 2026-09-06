#!/bin/sh
# SWD 兜底烧录（不编译）。日常升级请用 ./tools/xfer/xfer（SPI 上的 loader 收 upg.bin）。
#
# pyOCD 只能写内部 Flash。当前启动链：
#   stage0 boot @ 0x08000000
#     → SPI PART_LOADER / PART_LOADER_BK
#     → 内部 loader blob @ 0x080C0000
#     → UART XMODEM
#     → 直接跳 APP
# 串口挂了或 SPI loader 坏了：本脚本把 boot + 内部 loader 备份 + APP 双槽写进去，
# stage0 不靠 SPI 也能拉起 SRAM loader，再跳刚烧的 APP。
set -e

PROJ_ROOT="$(cd "$(dirname "$0")" && pwd)"
PYOCD="$PROJ_ROOT/tools/pyocd.sh"
TARGET=stm32f407zgtx

first_existing() {
  for f in "$@"; do
    if [ -f "$f" ]; then
      printf '%s' "$f"
      return 0
    fi
  done
  printf '%s' "$1"
}

BOOT_BIN_DEFAULT="$(first_existing \
  "$PROJ_ROOT/dist/boot.bin" \
  "$PROJ_ROOT/boot/build/boot.bin")"
APP_BIN_DEFAULT="$(first_existing \
  "$PROJ_ROOT/dist/app.bin" \
  "$PROJ_ROOT/app/build/app.bin")"
APP2_BIN_DEFAULT="$(first_existing \
  "$PROJ_ROOT/dist/app2.bin" \
  "$PROJ_ROOT/app/build/app2.bin")"
# build/ 是刚编出来的；dist/ 可能是旧 make 留下的
LOADER_BIN_DEFAULT="$(first_existing \
  "$PROJ_ROOT/loader/build/loader.bin" \
  "$PROJ_ROOT/dist/loader.bin")"
WEB_BIN_DEFAULT="$PROJ_ROOT/web.bin"
WEB_DIR_DEFAULT="$PROJ_ROOT/web"

BOOT_BASE=0x08000000
APP_BASE=0x08008000
APP2_BASE=0x08060000
HOST_FLASH_BASE=0x080A0000
HOST_FLASH_MAGIC=0x534C4648
LOADER_BACKUP_BASE=0x080C0000

BOARD_IP="${BOARD_IP:-192.168.137.122}"
BOARD_WAIT="${BOARD_WAIT:-45}"
SERIAL="${SERIAL:-/dev/ttyUSB0}"

usage() {
  cat <<EOF
Usage: $0 <recover|boot|loader|app|web|spi-img|upg|reset> [cmd...] [bin_path]

SWD 兜底（内部 Flash / pyOCD）。SPI 上的 loader/app 日常请用串口：
  ./tools/xfer/xfer $SERIAL dist/upg.bin

内部 Flash 布局（与 stage0 启动顺序一致）：
  0x08000000  boot          32K   固化 stage0
  0x08008000  app1
  0x08060000  app2
  0x080A0000  HFLS cookie         直烧 APP 后禁止 SPI pending OTA 盖掉
  0x080C0000  loader blob   64K   stage0 第 3 源（SPI 没有 loader 时用）

Commands（可组合，例如: boot loader app）:
  recover              兜底：boot + 内部 loader 备份 + APP 双槽 + cookie + reset
  boot                 stage0 boot.bin @ 0x08000000
  loader               内部 loader 备份：把 loader.bin 打成 2LDR blob @ 0x080C0000
  app                  app1 + app2 + HFLS cookie
  web                  pack web/ → POST /protocol/system/upload（SPI，需 APP 已起来）
  spi-img              生成 W25Q128 全量镜像 spi_image.bin（离线烧 SPI）
  upg                  日常串口：./tools/xfer/xfer \$SERIAL dist/upg.bin
  reset                pyocd reset
  all                  同 recover

Examples:
  $0 recover
  $0 boot
  $0 loader
  $0 app
  $0 upg
  SERIAL=/dev/ttyUSB1 $0 upg

Env:
  SERIAL=$SERIAL
  BOARD_IP=$BOARD_IP
  BOARD_WAIT=$BOARD_WAIT
  WEB_BIN=path
  SKIP_WEB_PACK=1          use existing $WEB_BIN_DEFAULT
  PROJ_ROOT  亦可给 tools/pyocd.sh 用（本脚本已自定）

EOF
  exit 1
}

need_bin() {
  if [ ! -f "$1" ]; then
    echo "ERROR: bin not found: $1" >&2
    echo "  Build first: make" >&2
    exit 1
  fi
}

is_cmd() {
  case "$1" in
    app|application|boot|stage0|bootloader|loader|loader-bk|loader-backup|web|www|all|both|recover|unbrick|spi-img|upg|xfer|serial|reset|-h|--help|help)
      return 0
      ;;
    *)
      return 1
      ;;
  esac
}

flash_one() {
  name="$1"
  bin="$2"
  base="$3"

  need_bin "$bin"
  echo "flash $name: $bin @ $base"
  "$PYOCD" load "$bin" -t "$TARGET" --base-address "$base"
  echo "flash $name ok"
}

# 直烧 APP 后写一次性 cookie，避免 loader 再用 SPI 里残留的 pending OTA 盖掉
write_host_flash_cookie() {
  slot="${1:-0}"
  tmp="$(mktemp --suffix=.bin)"
  python3 -c "import struct,sys; sys.stdout.buffer.write(struct.pack('<II', $HOST_FLASH_MAGIC, int(sys.argv[1])))" \
    "$slot" > "$tmp"
  echo "flash OTA-override cookie: slot=APP$((slot + 1)) @ $HOST_FLASH_BASE"
  "$PYOCD" load --format bin "$tmp" -t "$TARGET" --base-address "$HOST_FLASH_BASE"
  rm -f "$tmp"
  echo "OTA-override cookie ok"
}

flash_app_slots() {
  app1="$1"
  flash_one app1 "$app1" "$APP_BASE"
  if [ "$app1" = "$APP_BIN_DEFAULT" ] && [ -f "$APP2_BIN_DEFAULT" ]; then
    flash_one app2 "$APP2_BIN_DEFAULT" "$APP2_BASE"
  else
    echo "skip APP2 (custom bin or app2.bin missing)"
  fi
  write_host_flash_cookie 0
}

# stage0 从 0x080C0000 读 [loader_header_t][loader.bin]，不是裸 loader.bin
flash_loader_backup() {
  loader_bin="$1"
  tmp="$(mktemp --suffix=.bin)"

  need_bin "$loader_bin"
  echo "pack loader blob from $loader_bin"
  python3 "$PROJ_ROOT/tools/pack/make_loader_blob.py" "$loader_bin" -o "$tmp"
  echo "flash loader backup: $tmp @ $LOADER_BACKUP_BASE"
  "$PYOCD" load --format bin "$tmp" -t "$TARGET" --base-address "$LOADER_BACKUP_BASE"
  rm -f "$tmp"
  echo "flash loader backup ok (boot 兜底源 @ $LOADER_BACKUP_BASE)"
  echo
  echo "NOTE: 这不会改你现在看到的 logo。"
  echo "  boot 启动顺序: 1) SPI PART_LOADER  2) 内部备份  3) UART XMODEM"
  echo "  SPI 上已有合法 loader 时，#2 根本不会跑。"
  echo "  要换 SRAM loader / logo:  ./tools/xfer/xfer \$SERIAL dist/upg.bin"
  echo "  （或 ./flash.sh upg）把新 loader 写进 SPI。"
}

wait_board() {
  ip="$1"
  max="$2"
  echo "wait board $ip (up to ${max}s, web is SPI via HTTP)..."
  i=0
  while [ "$i" -lt "$max" ]; do
    if ping -c 1 -W 1 "$ip" >/dev/null 2>&1; then
      echo "board reachable: $ip"
      sleep 2
      return 0
    fi
    i=$((i + 1))
  done
  echo "ERROR: $ip not reachable after ${max}s" >&2
  echo "  APP/ETH not up? Check USB NIC, or skip web." >&2
  return 1
}

# web 在 W25Q SPI @ PART_WEB，pyOCD 写不了。APP 起来后走 HTTP 上传。
flash_web() {
  echo "flash web: pack → http://${BOARD_IP}/protocol/system/upload"
  wait_board "$BOARD_IP" "$BOARD_WAIT"
  WEB_BIN="${WEB_BIN:-}" SKIP_WEB_PACK="${SKIP_WEB_PACK:-0}" \
    python3 - "$PROJ_ROOT" "$BOARD_IP" "$WEB_BIN_DEFAULT" "$WEB_DIR_DEFAULT" <<'PY'
import os
import sys
import uuid
import urllib.error
import urllib.request
from pathlib import Path

proj = Path(sys.argv[1])
ip = sys.argv[2]
web_out = Path(sys.argv[3])
web_dir = Path(sys.argv[4])
web_bin_env = os.environ.get("WEB_BIN", "").strip()
skip_pack = os.environ.get("SKIP_WEB_PACK", "0") == "1"

sys.path.insert(0, str(proj / "tools" / "pack"))
from genUpgBin import pack_web_bin

if web_bin_env:
    src = Path(web_bin_env)
    if not src.is_file():
        print(f"ERROR: WEB_BIN not found: {src}", file=sys.stderr)
        sys.exit(1)
    data = src.read_bytes()
    print(f"[+] use WEB_BIN {src} ({len(data)} bytes)")
elif skip_pack and web_out.is_file():
    data = web_out.read_bytes()
    print(f"[+] skip pack, use {web_out} ({len(data)} bytes)")
else:
    if not web_dir.is_dir():
        print(f"ERROR: web dir not found: {web_dir}", file=sys.stderr)
        sys.exit(1)
    data = pack_web_bin(web_dir)
    web_out.write_bytes(data)
    print(f"[+] wrote {web_out}")

boundary = "----FlashSh" + uuid.uuid4().hex
head = (
    f"--{boundary}\r\n"
    f'Content-Disposition: form-data; name="file"; filename="web.bin"\r\n'
    f"Content-Type: application/octet-stream\r\n"
    f"\r\n"
).encode("ascii")
tail = f"\r\n--{boundary}--\r\n".encode("ascii")
body = head + data + tail

url = f"http://{ip}/protocol/system/upload"
req = urllib.request.Request(url, data=body, method="POST")
req.add_header("Content-Type", f"multipart/form-data; boundary={boundary}")
req.add_header("Content-Length", str(len(body)))
opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
try:
    with opener.open(req, timeout=180) as resp:
        text = resp.read().decode("utf-8", errors="replace")
        print(text.strip() or f"HTTP {resp.status}")
        if getattr(resp, "status", 200) >= 400:
            sys.exit(1)
except urllib.error.HTTPError as e:
    err = e.read().decode("utf-8", errors="replace")
    print(f"ERROR: HTTP {e.code} {e.reason}: {err}", file=sys.stderr)
    sys.exit(1)
except Exception as e:
    print(f"ERROR: web upload failed: {e}", file=sys.stderr)
    sys.exit(1)
print("flash web ok")
PY
}

spi_img() {
  echo "== genSpiImage.py =="
  web_opt=""
  if [ -f "$WEB_BIN_DEFAULT" ]; then
    web_opt="--web-bin $WEB_BIN_DEFAULT"
  fi
  python3 "$PROJ_ROOT/tools/pack/genSpiImage.py" $web_opt --out "$PROJ_ROOT/spi_image.bin"
  echo
  echo "== 烧录提示 =="
  echo "  SPI 离线烧录器：整片写 $PROJ_ROOT/spi_image.bin"
  echo "  内部 Flash 兜底（不靠 SPI）: $0 recover"
}

flash_upg_serial() {
  xfer="$PROJ_ROOT/tools/xfer/xfer"
  upg="$(first_existing "$PROJ_ROOT/dist/upg.bin" "$PROJ_ROOT/upg.bin")"
  if [ ! -x "$xfer" ]; then
    echo "ERROR: $xfer missing — make -C tools" >&2
    exit 1
  fi
  need_bin "$upg"
  echo "serial upg: $xfer $SERIAL $upg"
  exec "$xfer" "$SERIAL" "$upg"
}

if [ $# -eq 0 ]; then
  usage
fi

DO_APP=0
DO_BOOT=0
DO_LOADER=0
DO_WEB=0
DO_RESET=0
DO_SPIIMG=0
DO_UPG=0
APP_BIN="$APP_BIN_DEFAULT"
BOOT_BIN="$BOOT_BIN_DEFAULT"
LOADER_BIN="$LOADER_BIN_DEFAULT"

while [ $# -gt 0 ]; do
  case "$1" in
    -h|--help|help)
      usage
      ;;
    app|application)
      DO_APP=1
      shift
      if [ $# -gt 0 ] && ! is_cmd "$1"; then
        APP_BIN="$1"
        shift
      fi
      ;;
    boot|stage0|bootloader)
      DO_BOOT=1
      shift
      if [ $# -gt 0 ] && ! is_cmd "$1"; then
        BOOT_BIN="$1"
        shift
      fi
      ;;
    loader|loader-bk|loader-backup)
      DO_LOADER=1
      shift
      if [ $# -gt 0 ] && ! is_cmd "$1"; then
        LOADER_BIN="$1"
        shift
      fi
      ;;
    recover|unbrick|all|both)
      DO_BOOT=1
      DO_LOADER=1
      DO_APP=1
      DO_RESET=1
      shift
      ;;
    web|www)
      DO_WEB=1
      shift
      ;;
    spi-img)
      DO_SPIIMG=1
      shift
      ;;
    upg|xfer|serial)
      DO_UPG=1
      shift
      if [ $# -gt 0 ] && ! is_cmd "$1"; then
        SERIAL="$1"
        shift
      fi
      ;;
    reset)
      DO_RESET=1
      shift
      ;;
    *)
      echo "ERROR: unknown arg '$1'" >&2
      echo
      usage
      ;;
  esac
done

if [ "$DO_UPG" -eq 1 ]; then
  flash_upg_serial
fi

if [ "$DO_BOOT" -eq 1 ]; then
  flash_one boot "$BOOT_BIN" "$BOOT_BASE"
fi
if [ "$DO_LOADER" -eq 1 ]; then
  flash_loader_backup "$LOADER_BIN"
fi
if [ "$DO_APP" -eq 1 ]; then
  flash_app_slots "$APP_BIN"
fi
if [ "$DO_RESET" -eq 1 ]; then
  "$PYOCD" reset -t "$TARGET"
  echo "reset ok"
fi
if [ "$DO_SPIIMG" -eq 1 ]; then
  spi_img
fi
if [ "$DO_WEB" -eq 1 ]; then
  flash_web
fi
if [ "$DO_BOOT" -eq 1 ] && [ "$DO_LOADER" -eq 1 ] && [ "$DO_APP" -eq 1 ]; then
  echo "flash recover ok (boot + internal loader + app)"
fi
