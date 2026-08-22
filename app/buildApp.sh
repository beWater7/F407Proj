#!/bin/sh
set -e

cd "$(dirname "$0")"
PROJ_ROOT="$(cd .. && pwd)"
FLASH_SH="$PROJ_ROOT/flash.sh"

# 用法:
#   ./buildApp.sh 1          # 编译后调 flash.sh 烧 APP（不烧 web）
#   ./buildApp.sh flash      # 同上
#   ./buildApp.sh 0          # 只编译不烧录
#   ./buildApp.sh noflash    # 同上
#
usage() {
  cat <<EOF
Usage: $0 <0|1|flash|noflash>

Compile only here; flashing APP is ../flash.sh app.
Web (SPI) is separate: ../flash.sh web

Examples:
  $0 1         # build, then flash.sh app
  $0 flash     # same as 1
  $0 0         # build only, skip flash
  $0 noflash   # same as 0

Options:
  1 / flash / yes / y     build then flash APP
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

# 如果 build 目录存在，先清理
if [ -d build ]; then
  echo "Removing existing build directory..."
  rm -rf build
fi

make clean || true

# 编译（失败则立即退出，不再烧录）
make -j$(nproc)

if [ ! -f build/app.bin ]; then
  echo "ERROR: build/app.bin not found, compile failed?"
  exit 1
fi
echo "build success!"
# upg.bin 由 Makefile all→upg 在编译后自动生成

if [ "$DO_FLASH" -eq 1 ]; then
  "$FLASH_SH" app
else
  echo "skip flash (arg=$FLASH_ARG)"
fi
