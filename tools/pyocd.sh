#!/usr/bin/env bash
# 包装 pyocd：Linux 下强制 hidapiusb + hidraw，识别野火 CMSIS-DAP lite
set -euo pipefail
TOOLS_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=env-pyocd.sh
. "$TOOLS_DIR/env-pyocd.sh"

# 烧录类命令默认 --no-wait，避免探针未连接时无限卡在 Waiting...
cmd="${1:-}"
case "$cmd" in
  load|flash|erase|reset|gdbserver|commander)
    has_no_wait=0
    for a in "$@"; do
      if [ "$a" = "-W" ] || [ "$a" = "--no-wait" ]; then
        has_no_wait=1
        break
      fi
    done
    if [ "$has_no_wait" -eq 0 ]; then
      set -- "$1" --no-wait "${@:2}"
    fi
    ;;
esac

if ! pyocd list 2>/dev/null | grep -q 'Unique ID'; then
  echo "ERROR: 未检测到 CMSIS-DAP 调试器。" >&2
  echo "  lsusb 当前无 0484:a030（野火 fire CMSIS-DAP lite）。" >&2
  echo "  若在 VMware/虚拟机中：把 DAP 的 USB 设备连接到本虚拟机后重试。" >&2
  echo "  检查: lsusb | grep 0484 ; ./tools/pyocd.sh list" >&2
  exit 1
fi

exec pyocd "$@"
