#!/bin/sh
set -e

cd "$(dirname "$0")"
PROJ_ROOT="$(cd .. && pwd)"

# 用法:
#   ./buildBoot.sh 0          # 只编译 loader.bin（SRAM 变体，进 upg.bin）
#   ./buildBoot.sh 1          # 同上（不再 SWD 烧 0x08000000，以免覆盖固化 boot）
#
usage() {
  cat <<EOF
Usage: $0 <0|1|flash|noflash>

Compile loader (SRAM). Daily upgrade is upg.bin via tools/xfer/xfer.
Hardened boot is ../boot (./flash.sh boot), not this loader.

Examples:
  $0 0         # build loader.bin only
  $0 1         # same (SWD flash of loader is disabled)

EOF
  exit 1
}

if [ $# -eq 0 ]; then
  usage
fi

FLASH_ARG="$1"
case "$FLASH_ARG" in
  0|no|n|nof|noflash|NOFLASH|1|yes|y|flash|FLASH)
    ;;
  -h|--help|help)
    usage
    ;;
  *)
    echo "ERROR: unknown arg '$FLASH_ARG'"
    echo
    usage
    ;;
esac

make -j$(nproc)

if [ ! -f build/loader.bin ]; then
  echo "ERROR: build/loader.bin not found, compile failed?"
  exit 1
fi
echo "build success: build/loader.bin"
echo "Pack upgrade:  make -C $PROJ_ROOT upg"
echo "Serial flash:  $PROJ_ROOT/tools/xfer/xfer /dev/ttyUSB0 $PROJ_ROOT/dist/upg.bin"
echo "SWD boot only: $PROJ_ROOT/flash.sh boot"
