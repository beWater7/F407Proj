# Auto-generated Makefile for STM32 project
PROJECT = app
ifndef PREFIX
#编译链没有指定路径,需要加到环境变量
PREFIX = arm-none-eabi-
endif

PROJECT_DIR = $(CURDIR)
BUILD_DIR = build
OBJ_DIR = $(BUILD_DIR)/obj
PROJ_ROOT := $(abspath ..)
SOC ?= stm32f4
BOARD ?= f407zg
COMPONENT := $(PROJ_ROOT)/component
PLATFORM  := $(PROJ_ROOT)/platform

# 当前使用的芯片型号宏定义（用于控制兼容性）
STM32F4XX_CHIP = STM32F407xx

CC = $(PREFIX)gcc
AS = $(PREFIX)gcc -x assembler-with-cpp #汇编文件处理
LD = $(PREFIX)gcc
OBJCOPY = $(PREFIX)objcopy
STRIP   = $(PREFIX)strip

# Kconfig：可选功能裁减（Boot 不参与）
AUTOCONF_H = $(PROJECT_DIR)/include/generated/autoconf.h
-include $(PROJECT_DIR)/.config

MACRO_SUPPORT= -DUSE_STDPERIPH_DRIVER -DSTM32F40_41xxx 

CFLAGS = -Wall -Os -ffunction-sections -fdata-sections -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=softfp $(MACRO_SUPPORT) \
	-Werror=implicit-function-declaration -Werror=return-type -Wno-incompatible-pointer-types -Wno-int-conversion \
	-include $(AUTOCONF_H)

# 无感双槽：同一套 .o 分别链到 APP1(0x08008000) / APP2(0x08060000)
LDFLAGS_COMMON = -Wl,--gc-sections --specs=nano.specs -lm
LDFLAGS_APP1 = -T $(PLATFORM)/boards/$(BOARD)/link_app1.ld -Wl,-Map=$(BUILD_DIR)/app1.map $(LDFLAGS_COMMON)
LDFLAGS_APP2 = -T $(PLATFORM)/boards/$(BOARD)/link_app2.ld -Wl,-Map=$(BUILD_DIR)/app2.map $(LDFLAGS_COMMON)
# 兼容旧目标名：默认链到 APP1
LDFLAGS = $(LDFLAGS_APP1)

# 库 / CMSIS / 开源组件 / 工程配置
INCLUDE_PATH = 	\
	-I $(PROJECT_DIR)/Libraries/STM32F4xx_StdPeriph_Driver/inc     \
	-I $(PROJECT_DIR)/Libraries/CMSIS/Include                      \
	-I $(PROJECT_DIR)/Libraries/CMSIS/Device/ST/STM32F4xx/Include  \
	                                                               \
	-I $(COMPONENT)/FreeRTOS/include                             \
	-I $(COMPONENT)/FreeRTOS/port/gcc                            \
	                                                               \
	-I $(PLATFORM)/$(SOC)/net/lwip_port			                   \
	-I $(PLATFORM)/$(SOC)/net/lwip_port/arch                         \
	-I $(PLATFORM)/$(SOC)/net/lwip_port/Standalone                   \
	-I $(COMPONENT)/lwip-2.1.2/src/include                       \
	-I $(COMPONENT)/lwip-2.1.2/src/include/compat/posix          \
	-I $(COMPONENT)/lwip-2.1.2/src/include/compat/stdc           \
	-I $(COMPONENT)/lwip-2.1.2/src/include/lwip                  \
	-I $(COMPONENT)/lwip-2.1.2/src/include/netif                 \
	-I $(COMPONENT)/shell                                        \
	-I $(COMPONENT)/cJSON                                        \
	-I $(COMPONENT)/mqtt                                         \
	-I $(COMPONENT)/multi_button                                 \
	-I $(PROJECT_DIR)/config                                     \
	-I $(PROJ_ROOT)/common                                       \
	-I $(PLATFORM)/hal                                           \
	-I $(PLATFORM)/$(SOC)                                        \
	-I $(PLATFORM)/boards/$(BOARD)                               \
	-I $(PROJECT_DIR)/User/malloc                                  \
	-I $(PROJECT_DIR)/include/generated

# 自动收集 User/h、User/app 与 platform 驱动头
INCLUDE_PATH += $(addprefix -I,$(shell find $(PROJECT_DIR)/User/h $(PROJECT_DIR)/User/app $(PLATFORM)/$(SOC)/include -type d 2>/dev/null))

ASM_SRCS = $(PROJECT_DIR)/Libraries/CMSIS/Device/ST/STM32F4xx/Source/Templates/gcc/startup_stm32f40xx.s \
           $(PROJECT_DIR)/Libraries/CMSIS/Device/ST/STM32F4xx/Source/Templates/gcc/reset_handler.s

# 排除：生成的网页镜像、单元测试、易冲突源文件 + Kconfig 可裁模块
EXCLUDE_SRC_RE = fsdata\.c|(^|/)test\.c|altcp_proxyconnect\.c|cJSON_Utils\.c|/MQTT/|/MQTTclient/|bsp_debug_usart|User/app/crc/crc\.c|/User/app/hal/
ifneq ($(CONFIG_APP_ESP8266),y)
EXCLUDE_SRC_RE := $(EXCLUDE_SRC_RE)|/ESP8266/
endif
ifneq ($(CONFIG_APP_DHT11),y)
EXCLUDE_SRC_RE := $(EXCLUDE_SRC_RE)|/dht11/
endif
ifneq ($(CONFIG_APP_MULTIBUTTON),y)
EXCLUDE_SRC_RE := $(EXCLUDE_SRC_RE)|/multi_button/
endif
ifneq ($(CONFIG_APP_TELNET),y)
EXCLUDE_SRC_RE := $(EXCLUDE_SRC_RE)|(^|/)telnet\.c
endif
# 仅裁 SNTP 客户端协议栈；sntp_api.c 含 RTC print_timestamp，始终编译
ifneq ($(CONFIG_APP_SNTP),y)
EXCLUDE_SRC_RE := $(EXCLUDE_SRC_RE)|(^|/)sntp\.c
endif
ifneq ($(CONFIG_APP_CAN),y)
EXCLUDE_SRC_RE := $(EXCLUDE_SRC_RE)|/can/
endif
# diskio.c 在 User/app/fatfs，仅 FATFS 开启时编
EXCLUDE_SRC_RE := $(EXCLUDE_SRC_RE)|/fatfs/
# USB Host 库（OTG_FS 驱动 + Host 栈 + MSC 类），未开启时整体不编
ifneq ($(CONFIG_APP_USB_HOST),y)
EXCLUDE_SRC_RE := $(EXCLUDE_SRC_RE)|/usb/|/usbHost/
else
# usbh_msc_fatfs.c 的 disk_* 与 User/app/fatfs/diskio.c 冲突，USB 盘的
# FatFs 底层由 diskio.c 的 CONFIG_APP_USB_HOST 分支提供，故排除该文件
# usb_dcd*.c 是 device mode 专用；usb_otg.c 是 OTG 双角色专用（USE_OTG_MODE），host 模式均不编
EXCLUDE_SRC_RE := $(EXCLUDE_SRC_RE)|usbh_msc_fatfs\.c|usb_dcd\.c|usb_dcd_int\.c|usb_otg\.c
endif

INCLUDE_PATH += -I $(COMPONENT)/fatfs

# StdPeriph 白名单：本板实际用到的外设（ETH DMA 走 stm32f4x7_eth.c，不编 CRYP/HASH/DCMI 等）
STDPERIPH_SRC_DIR = $(PROJECT_DIR)/Libraries/STM32F4xx_StdPeriph_Driver/src
STDPERIPH_SRCS = \
	$(STDPERIPH_SRC_DIR)/misc.c \
	$(STDPERIPH_SRC_DIR)/stm32f4xx_rcc.c \
	$(STDPERIPH_SRC_DIR)/stm32f4xx_gpio.c \
	$(STDPERIPH_SRC_DIR)/stm32f4xx_usart.c \
	$(STDPERIPH_SRC_DIR)/stm32f4xx_spi.c \
	$(STDPERIPH_SRC_DIR)/stm32f4xx_tim.c \
	$(STDPERIPH_SRC_DIR)/stm32f4xx_flash.c \
	$(STDPERIPH_SRC_DIR)/stm32f4xx_dma.c \
	$(STDPERIPH_SRC_DIR)/stm32f4xx_exti.c \
	$(STDPERIPH_SRC_DIR)/stm32f4xx_syscfg.c \
	$(STDPERIPH_SRC_DIR)/stm32f4xx_pwr.c \
	$(STDPERIPH_SRC_DIR)/stm32f4xx_rtc.c \
	$(STDPERIPH_SRC_DIR)/stm32f4xx_fsmc.c \
	$(STDPERIPH_SRC_DIR)/stm32f4xx_iwdg.c
ifeq ($(CONFIG_APP_CAN),y)
STDPERIPH_SRCS += $(STDPERIPH_SRC_DIR)/stm32f4xx_can.c
endif

# C源文件（路径均在仓库根下，obj 用相对仓库根的路径）
C_SRCS := \
	$(PROJECT_DIR)/Libraries/CMSIS/Device/ST/STM32F4xx/Source/Templates/system_stm32f4xx.c \
	$(STDPERIPH_SRCS) \
	$(shell find $(COMPONENT)/FreeRTOS/port/gcc -name "*.c") \
	$(shell find $(COMPONENT)/FreeRTOS/src -name "*.c") \
	$(shell find $(PLATFORM)/$(SOC)/net/lwip_port/Standalone -name "*.c") \
	$(shell find $(COMPONENT)/lwip-2.1.2/src/api -name "*.c") \
	$(shell find $(COMPONENT)/lwip-2.1.2/src/core -maxdepth 1 -name "*.c") \
	$(shell find $(COMPONENT)/lwip-2.1.2/src/core/ipv4 -name "*.c") \
	$(COMPONENT)/lwip-2.1.2/src/netif/ethernet.c \
	$(COMPONENT)/shell/shell_src.c \
	$(COMPONENT)/shell/ustdio.c \
	$(COMPONENT)/shell/avltree.c \
	$(COMPONENT)/cJSON/cJSON.c \
	$(PROJ_ROOT)/common/crc.c \
	$(PLATFORM)/$(SOC)/stm32f4xx_it.c \
	$(shell find $(PLATFORM)/$(SOC)/drivers -name "*.c" | grep -vE '$(EXCLUDE_SRC_RE)') \
	$(shell find $(PROJECT_DIR)/User/app -name "*.c" | grep -vE '$(EXCLUDE_SRC_RE)') \
	$(shell find $(PROJECT_DIR)/User/malloc -name "*.c" 2>/dev/null)

ifeq ($(CONFIG_APP_FATFS),y)
C_SRCS += \
	$(COMPONENT)/fatfs/ff.c \
	$(PROJECT_DIR)/User/app/fatfs/diskio.c
endif
# USB Host 依赖 FatFs（U 盘以 FatFs 卷方式挂载），仅开 USB 时也一并编译
ifeq ($(CONFIG_APP_USB_HOST),y)
ifeq ($(CONFIG_APP_FATFS),)
C_SRCS += \
	$(COMPONENT)/fatfs/ff.c \
	$(PROJECT_DIR)/User/app/fatfs/diskio.c
endif
endif
ifeq ($(CONFIG_APP_MULTIBUTTON),y)
C_SRCS += $(COMPONENT)/multi_button/multi_button.c
endif

# 目标文件全部进 build/obj/，镜像源码相对仓库根路径
SRC_TO_OBJ = $(addprefix $(OBJ_DIR)/,$(patsubst $(PROJ_ROOT)/%,%,$(patsubst %.c,%.o,$(patsubst %.s,%.o,$(1)))))
ASM_OBJS := $(call SRC_TO_OBJ,$(ASM_SRCS))
C_OBJS   := $(call SRC_TO_OBJ,$(C_SRCS))
OBJS     := $(ASM_OBJS) $(C_OBJS)

# V=1 显示完整编译命令；默认只打印一行 CC/AS/LD/BIN
ifeq ($(V),1)
Q :=
else
Q := @
endif

GEN_UPG   := $(PROJ_ROOT)/tools/pack/genUpgBin.py
GEN_FSDATA := $(PROJ_ROOT)/tools/pack/gen_fsdata.py
GEN_DTB    := $(PROJ_ROOT)/tools/pack/gen_dtb.py
FSDATA_C   := $(PROJECT_DIR)/User/app/http/fsdata.c
ROM_WEB    := $(PROJ_ROOT)/web/rom/index.html $(PROJ_ROOT)/web/rom/404.html

# 板级设备树: dts 源 → 二进制 dtb(烧 SPI flash 用) + 内置 C 数组(固件兜底)
BOARD_DTS      := $(PLATFORM)/boards/$(BOARD)/$(BOARD).dts
DTB_FALLBACK_C := $(PROJECT_DIR)/User/app/dts/dts_fallback.c
DTB_BIN        := $(BUILD_DIR)/$(BOARD).dtb

.DEFAULT_GOAL := all

# 无 .config 时用 defconfig（可选功能全关）
$(PROJECT_DIR)/.config:
	@echo "==> creating default .config from defconfig"
	@cp -f $(PROJECT_DIR)/defconfig $@

$(AUTOCONF_H): $(PROJECT_DIR)/.config $(PROJECT_DIR)/Kconfig
	@mkdir -p $(dir $@)
	@echo "GEN $@"
	@PATH="$(HOME)/.local/bin:$$PATH" KCONFIG_CONFIG=$(PROJECT_DIR)/.config \
		genconfig --header-path $@

.PHONY: menuconfig nconfig guiconfig syncconfig all clean upg compdb fsdata
menuconfig:
	@chmod +x $(PROJECT_DIR)/scripts/menuconfig.sh
	@$(PROJECT_DIR)/scripts/menuconfig.sh menuconfig

nconfig:
	@chmod +x $(PROJECT_DIR)/scripts/menuconfig.sh
	@$(PROJECT_DIR)/scripts/menuconfig.sh nconfig

guiconfig:
	@chmod +x $(PROJECT_DIR)/scripts/menuconfig.sh
	@$(PROJECT_DIR)/scripts/menuconfig.sh guiconfig

syncconfig: $(AUTOCONF_H)

# 默认：双槽镜像 + app.bin(APP1 别名)。升级包由仓库根 `make` / `make +web` 生成。
all: syncconfig dts $(BUILD_DIR)/app1.bin $(BUILD_DIR)/app2.bin $(BUILD_DIR)/$(PROJECT).bin

# 编译板级 dts → dtb + 固件内置兜底表
.PHONY: dts
dts: $(DTB_FALLBACK_C) $(DTB_BIN)

$(DTB_FALLBACK_C) $(DTB_BIN): $(BOARD_DTS) $(GEN_DTB)
	@mkdir -p $(dir $@) $(dir $(DTB_BIN))
	@echo "DTB $(BOARD_DTS) -> $(DTB_BIN)"
	python3 "$(GEN_DTB)" "$(BOARD_DTS)" --bin-out "$(DTB_BIN)" --c-out "$(DTB_FALLBACK_C)"

# APP1/APP2 bin 变更后同步生成双槽 OTA 包
.PHONY: fsdata
fsdata: $(FSDATA_C)

$(FSDATA_C): $(ROM_WEB) $(GEN_FSDATA)
	@echo "FSDATA $@"
	python3 "$(GEN_FSDATA)" --rom-dir "$(PROJ_ROOT)/web/rom" --out "$@"

$(OBJ_DIR)/app/User/app/http/fs.o: $(FSDATA_C)

.PHONY: upg
upg: $(BUILD_DIR)/app1.bin $(BUILD_DIR)/app2.bin
	@if [ -f "$(GEN_UPG)" ]; then \
		echo "UPG via tools/pack/genUpgBin.py (needs loader/build/loader.bin)"; \
		python3 "$(GEN_UPG)" --fw1 "$(abspath $(BUILD_DIR)/app1.bin)" \
			--fw2 "$(abspath $(BUILD_DIR)/app2.bin)" \
			--loader "$(PROJ_ROOT)/loader/build/loader.bin" \
			--out-dir "$(PROJ_ROOT)/dist"; \
	else \
		echo "WARN: $(GEN_UPG) missing, skip upg.bin"; \
	fi

$(BUILD_DIR)/app1.elf: $(OBJS)
	@mkdir -p $(BUILD_DIR)
	@echo "LD  $@ (APP1 @ 0x08008000)"
	$(Q)$(LD) $(CFLAGS) $(INCLUDE_PATH) $^ $(LDFLAGS_APP1) -o $@

$(BUILD_DIR)/app2.elf: $(OBJS)
	@mkdir -p $(BUILD_DIR)
	@echo "LD  $@ (APP2 @ 0x08060000)"
	$(Q)$(LD) $(CFLAGS) $(INCLUDE_PATH) $^ $(LDFLAGS_APP2) -o $@

$(BUILD_DIR)/app1.bin: $(BUILD_DIR)/app1.elf
	@echo "BIN $@"
	$(Q)$(OBJCOPY) -O binary $< $@

$(BUILD_DIR)/app2.bin: $(BUILD_DIR)/app2.elf
	@echo "BIN $@"
	$(Q)$(OBJCOPY) -O binary $< $@

# flash.sh / 调试仍用 app.bin → APP1 链接产物
$(BUILD_DIR)/$(PROJECT).bin: $(BUILD_DIR)/app1.bin
	@cp -f $< $@
	@echo "BIN $@ (alias of app1.bin)"

$(BUILD_DIR)/$(PROJECT).elf: $(BUILD_DIR)/app1.elf
	@cp -f $< $@

# 第三方源码警告不刷屏（User 代码单独修）
$(OBJ_DIR)/app/Libraries/%.o: CFLAGS += -Wno-deprecated -Wno-unused-parameter
$(OBJ_DIR)/component/lwip-2.1.2/%.o: CFLAGS += -Wno-unused-variable -Wno-unused-function -Wno-unused-but-set-variable
$(OBJ_DIR)/platform/%.o: CFLAGS += -Wno-unused-variable -Wno-unused-function -Wno-unused-but-set-variable
$(OBJ_DIR)/component/cJSON/cJSON.o: CFLAGS += -Wno-misleading-indentation
$(OBJ_DIR)/component/shell/%.o: CFLAGS += -Wno-unused-parameter -Wno-unused-variable

# 编译 C 文件（依赖 autoconf），产物在 build/obj/<相对仓库根>
$(OBJ_DIR)/%.o: $(PROJ_ROOT)/%.c $(AUTOCONF_H)
	@mkdir -p $(dir $@)
	@echo "CC  $<"
	$(Q)$(CC) $(CFLAGS) $(INCLUDE_PATH) -c $< -o $@

# 编译汇编文件
$(OBJ_DIR)/%.o: $(PROJ_ROOT)/%.s
	@mkdir -p $(dir $@)
	@echo "AS  $<"
	$(Q)$(AS) $(CFLAGS) $(INCLUDE_PATH) -c $< -o $@

# 清理目标：build/ + 历史残留在源码树里的 .o
clean:
	rm -rf $(BUILD_DIR)
	@find $(PROJECT_DIR)/Libraries $(PROJECT_DIR)/User \
		$(COMPONENT) $(PLATFORM) \
		-name '*.o' -delete 2>/dev/null || true

# clangd：本目录独立 compile_commands.json（勿与 bootloader 合并）
.PHONY: compdb
compdb:
	@command -v bear >/dev/null 2>&1 || { echo "ERROR: bear not found (sudo apt install bear)"; exit 1; }
	rm -rf $(OBJ_DIR)
	bear --output $(PROJECT_DIR)/compile_commands.json -- $(MAKE) -j$$(nproc) $(BUILD_DIR)/app1.elf
