#!/bin/sh
set -e

cd "$(dirname "$0")"

make -j$(nproc)

if [ ! -f build/loader.bin ]; then
  echo "ERROR: build/loader.bin not found, compile failed?"
  exit 1
fi

pyocd load build/loader.bin -t stm32f407zgtx --base-address 0x08000000
echo "success!"
