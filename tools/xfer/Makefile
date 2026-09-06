# xfer — Linux 串口 YMODEM/XMODEM 烧录工具(独立构建)
# 用法: make -C tools/xfer [EXTRA_CFLAGS=...]
CC            ?= gcc
CFLAGS        ?= -O2 -Wall -Wextra -std=c11
EXTRA_CFLAGS  ?=            # 追加定义: -DXFER_PROGRESS_COLOR=3 / -DXFER_DEFAULT_BAUD=921600 ...
LDFLAGS       ?=

.PHONY: all clean

all: xfer

# 产物 tools/xfer/xfer(可执行文件与所在目录同名)
xfer: flash_upg.c xfer_uart.c xfer.h xfer_config.h
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -o $@ flash_upg.c xfer_uart.c $(LDFLAGS)

clean:
	rm -f xfer
