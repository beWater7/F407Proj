#!/bin/sh
set -e

cd "$(dirname "$0")"
PROJ_ROOT="$(cd .. && pwd)"
FLASH_SH="$PROJ_ROOT/flash.sh"

# 用法:
#   ./buildBoot.sh 1          # 编译后调 flash.sh 烧 Boot
#   ./buildBoot.sh flash      # 同上
#   ./buildBoot.sh 0          # 只编译不烧录
#   ./buildBoot.sh noflash    # 同上
#
usage() {
  cat <<EOF
Usage: $0 <0|1|flash|noflash>

Compile only here; flashing is ../flash.sh.

Examples:
  $0 1         # build, then flash.sh boot
  $0 flash     # same as 1
  $0 0         # build only, skip flash
  $0 noflash   # same as 0

Options:
  1 / flash / yes / y     build then flash bootloader
  0 / noflash / no / n    build only
  -h / --help / help      show this message

EOF
  exit 1
}

if [ $# -eq 0 ]; then
  usage
fi

FLASH_ARG="$1"
case "$FLASH_ARG" in
  0|no|n|nof|noflash|NOFLASH)
    DO_FLASH=0
    ;;
  1|yes|y|flash|FLASH)
    DO_FLASH=1
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
echo "build success!"

if [ "$DO_FLASH" -eq 1 ]; then
  "$FLASH_SH" boot
else
  echo "skip flash (arg=$FLASH_ARG)"
fi
