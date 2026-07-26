#!/bin/sh
set -e

cd "$(dirname "$0")"

# 用法:
#   ./buildApp.sh        # 默认编译并烧录
#   ./buildApp.sh 1      # 编译并烧录
#   ./buildApp.sh 0      # 只编译不烧录
#   ./buildApp.sh flash / noflash  同上
#
# 也可用环境变量覆盖: FLASH=0 ./buildApp.sh
usage() {
  echo "Usage: $0 [0|1|flash|noflash]"
  echo "  1 / flash    build + pyocd load (default)"
  echo "  0 / noflash  build only"
  exit 1
}

FLASH_ARG="${1:-${FLASH:-1}}"
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

if [ "$DO_FLASH" -eq 1 ]; then
  # APP 链接在 0x08008000（boot 占前 32K）
  pyocd load build/app.bin -t stm32f407zgtx --base-address 0x08008000
  echo "load app success!"
else
  echo "skip flash (arg=$FLASH_ARG)"
fi
