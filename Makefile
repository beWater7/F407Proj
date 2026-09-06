# F407Proj 顶层构建
#
#   make          -> dist/{boot.bin,loader.bin,app.bin,upg.bin} + tools/xfer/xfer
#   make +web     -> 额外生成 dist/upg_web.bin
#   make boot     -> 只编固化 boot（烧一次）
#
PROJ    := $(abspath .)
DIST    := dist
BOARD   ?= f407zg

.PHONY: all boot loader app tools upg web +web dtb clean help

all: boot loader app tools upg

help:
	@echo "make          编译 boot/loader/app，打包 dist/upg.bin，编 tools/xfer/xfer"
	@echo "make +web     同上并打包 dist/upg_web.bin（含网页）"
	@echo "make boot     只编固化 boot.bin（内部 Flash 0x08000000，基本不再更新）"
	@echo "make dtb      只重新生成 dist/<board>.dtb（改分区表后烧 dtb 用）"
	@echo "make clean"
	@echo
	@echo "串口升级:  ./tools/xfer/xfer /dev/ttyUSB0 dist/upg.bin   （纯串口，无 pyocd/SWD 依赖）"
	@echo "           注意用 tools/xfer/xfer（make 刚编的），不要用 PATH 里旧的 /usr/local/bin/xfer"
	@echo "烧 dtb  :  ./tools/xfer/xfer /dev/ttyUSB0 dist/f407zg.dtb   （动态分区表，详见 loader/doc/07-dts-partition-table.md）"
	@echo "SWD 兜底:  ./flash.sh recover     # boot + 内部 loader 备份 + APP 双槽"

boot:
	$(MAKE) -C boot
	@mkdir -p $(DIST)
	@cp -f boot/build/boot.bin $(DIST)/boot.bin
	@echo "BOOT $(DIST)/boot.bin"

loader:
	$(MAKE) -C loader
	@mkdir -p $(DIST)
	@cp -f loader/build/loader.bin $(DIST)/loader.bin
	@echo "LOADER $(DIST)/loader.bin  ($$(wc -c < loader/build/loader.bin) bytes)"

app:
	$(MAKE) -C app
	@mkdir -p $(DIST)
	@cp -f app/build/app.bin $(DIST)/app.bin
	@cp -f app/build/app1.bin $(DIST)/app1.bin
	@cp -f app/build/app2.bin $(DIST)/app2.bin
	@cp -f app/build/$(BOARD).dtb $(DIST)/$(BOARD).dtb
	@echo "APP $(DIST)/app.bin"
	@echo "DTB $(DIST)/$(BOARD).dtb   (烧 dtb: ./tools/xfer/xfer /dev/ttyUSB0 $(DIST)/$(BOARD).dtb)"

# 只重新生成 dtb 并拷贝到 dist（改 dts 后不用重编整个 app）
dtb:
	$(MAKE) -C app dts
	@mkdir -p $(DIST)
	@cp -f app/build/$(BOARD).dtb $(DIST)/$(BOARD).dtb
	@echo "DTB $(DIST)/$(BOARD).dtb   (烧 dtb: ./tools/xfer/xfer /dev/ttyUSB0 $(DIST)/$(BOARD).dtb)"
	@echo "详见 loader/doc/07-dts-partition-table.md"

tools:
	$(MAKE) -C tools
	@echo "TOOL tools/xfer/xfer"

upg: loader app
	@mkdir -p $(DIST)
	python3 "$(PROJ)/tools/pack/genUpgBin.py" --out-dir "$(DIST)"
	@cp -f "$(DIST)/upg.bin" "$(PROJ)/upg.bin"

+web web: loader app
	@mkdir -p $(DIST)
	python3 "$(PROJ)/tools/pack/genUpgBin.py" --with-web --out-dir "$(DIST)"
	@cp -f "$(DIST)/upg_web.bin" "$(PROJ)/upg_web.bin"

clean:
	$(MAKE) -C boot clean
	$(MAKE) -C loader clean
	$(MAKE) -C app clean
	$(MAKE) -C tools clean
	rm -rf "$(DIST)"
