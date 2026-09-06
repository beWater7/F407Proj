# Host-side C tools (Linux). Python scripts in this directory are for pyOCD.
# xfer 已独立成 tools/xfer/ 子工程, 这里只转发:
#   make -C tools            => 编出 tools/xfer/xfer
#   make -C tools/xfer       => 单独编 xfer(带 EXTRA_CFLAGS 等)

.PHONY: all clean

all:
	$(MAKE) -C xfer

clean:
	$(MAKE) -C xfer clean
