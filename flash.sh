#!/bin/sh
# 只烧录，不编译。SWD 走 tools/pyocd.sh；web 在 SPI Flash，走 HTTP POST。
set -e

PROJ_ROOT="$(cd "$(dirname "$0")" && pwd)"
PYOCD="$PROJ_ROOT/tools/pyocd.sh"
TARGET=stm32f407zgtx

APP_BIN_DEFAULT="$PROJ_ROOT/app/build/app.bin"
APP2_BIN_DEFAULT="$PROJ_ROOT/app/build/app2.bin"
BOOT_BIN_DEFAULT="$PROJ_ROOT/bootloader/build/loader.bin"
WEB_BIN_DEFAULT="$PROJ_ROOT/web.bin"
WEB_DIR_DEFAULT="$PROJ_ROOT/web"
APP_BASE=0x08008000
APP2_BASE=0x08060000
BOOT_BASE=0x08000000
# 内部 PART_RES：Boot 看到 HFLS cookie 后取消 SPI pending OTA（pyocd 改不了 SPI）
HOST_FLASH_BASE=0x080A0000
HOST_FLASH_MAGIC=0x534C4648

BOARD_IP="${BOARD_IP:-192.168.137.122}"
BOARD_WAIT="${BOARD_WAIT:-45}"

usage() {
  cat <<EOF
Usage: $0 <app|boot|web|all|reset> [cmd...] [bin_path]

Flash only (no make). Internal Flash via pyOCD; web via HTTP (SPI NOR).

Commands (can combine, e.g. app web):
  boot                 loader.bin  @ 0x08000000 (SWD)
  app                  app1.bin @ 0x08008000 + app2.bin @ 0x08060000 + cookie
  web                  pack web/ → POST /protocol/system/upload
  all                  boot then app (no web)
  reset                pyocd reset only

Examples:
  $0 boot
  $0 app
  $0 web
  $0 app web
  $0 all
  $0 app /path/to/x.bin
  $0 boot /path/to/y.bin web

Env:
  BOARD_IP=$BOARD_IP     board IPv4 for web upload
  BOARD_WAIT=$BOARD_WAIT           seconds to wait for ping before web
  WEB_BIN=path             use this web.bin instead of packing web/
  SKIP_WEB_PACK=1          use existing $WEB_BIN_DEFAULT

EOF
  exit 1
}

need_bin() {
  if [ ! -f "$1" ]; then
    echo "ERROR: bin not found: $1" >&2
    echo "  Build first: ./app/buildApp.sh 0   or  ./bootloader/buildBoot.sh 0" >&2
    exit 1
  fi
}

is_cmd() {
  case "$1" in
    app|application|boot|bootloader|loader|web|www|all|both|reset|-h|--help|help)
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

# 直烧 APP 后写一次性 cookie，避免 Boot 再用 SPI 里残留的网页 OTA 把刚烧的镜像盖掉
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
  # 只烧 APP1 时，Boot 若 SPI active_app=APP2 会继续跑旧镜像（build time 不变）
  if [ "$app1" = "$APP_BIN_DEFAULT" ] && [ -f "$APP2_BIN_DEFAULT" ]; then
    flash_one app2 "$APP2_BIN_DEFAULT" "$APP2_BASE"
  else
    echo "skip APP2 (custom bin or app2.bin missing)"
  fi
  write_host_flash_cookie 0
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
  echo "  APP/ETH not up? Check USB NIC, or skip web: FLASH_WEB=0 ./app/buildApp.sh 1" >&2
  return 1
}

# web 在 W25Q SPI @ PART_WEB，pyOCD 写不了。APP 起来后走现有 HTTP 上传。
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

sys.path.insert(0, str(proj))
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

if [ $# -eq 0 ]; then
  usage
fi

DO_APP=0
DO_BOOT=0
DO_WEB=0
DO_RESET=0
APP_BIN="$APP_BIN_DEFAULT"
BOOT_BIN="$BOOT_BIN_DEFAULT"

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
    boot|bootloader|loader)
      DO_BOOT=1
      shift
      if [ $# -gt 0 ] && ! is_cmd "$1"; then
        BOOT_BIN="$1"
        shift
      fi
      ;;
    web|www)
      DO_WEB=1
      shift
      ;;
    all|both)
      DO_BOOT=1
      DO_APP=1
      shift
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

if [ "$DO_BOOT" -eq 1 ]; then
  flash_one boot "$BOOT_BIN" "$BOOT_BASE"
fi
if [ "$DO_APP" -eq 1 ]; then
  flash_app_slots "$APP_BIN"
fi
if [ "$DO_RESET" -eq 1 ]; then
  "$PYOCD" reset -t "$TARGET"
  echo "reset ok"
fi
if [ "$DO_WEB" -eq 1 ]; then
  flash_web
fi
if [ "$DO_BOOT" -eq 1 ] && [ "$DO_APP" -eq 1 ]; then
  echo "flash all ok"
fi
