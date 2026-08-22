# Auto-generated Makefile for STM32 project
PROJECT = app
ifndef PREFIX
#编译链没有指定路径,需要加到环境变量
PREFIX = arm-none-eabi-
endif

PROJECT_DIR = $(CURDIR)
BUILD_DIR = build
OBJ_DIR = $(BUILD_DIR)/obj

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
LDFLAGS_APP1 = -T link_app1.ld -Wl,-Map=$(BUILD_DIR)/app1.map $(LDFLAGS_COMMON)
LDFLAGS_APP2 = -T link_app2.ld -Wl,-Map=$(BUILD_DIR)/app2.map $(LDFLAGS_COMMON)
# 兼容旧目标名：默认链到 APP1
LDFLAGS = $(LDFLAGS_APP1)

# 库 / CMSIS / FreeRTOS / lwIP 固定头路径
INCLUDE_PATH = 	\
	-I $(PROJECT_DIR)/Libraries/STM32F4xx_StdPeriph_Driver/inc     \
	-I $(PROJECT_DIR)/Libraries/CMSIS/Include                      \
	-I $(PROJECT_DIR)/Libraries/CMSIS/Device/ST/STM32F4xx/Include  \
	                                                               \
	-I $(PROJECT_DIR)/freeRTOS/include                             \
	-I $(PROJECT_DIR)/freeRTOS/port/gcc                            \
	                                                               \
	-I $(PROJECT_DIR)/lwip-2.1.2/port			                   \
	-I $(PROJECT_DIR)/lwip-2.1.2/port/arch                         \
	-I $(PROJECT_DIR)/lwip-2.1.2/port/Standalone                   \
	-I $(PROJECT_DIR)/lwip-2.1.2/src/include                       \
	-I $(PROJECT_DIR)/lwip-2.1.2/src/include/compat/posix          \
	-I $(PROJECT_DIR)/lwip-2.1.2/src/include/compat/stdc           \
	-I $(PROJECT_DIR)/lwip-2.1.2/src/include/lwip                  \
	-I $(PROJECT_DIR)/lwip-2.1.2/src/include/netif                 \
	-I $(PROJECT_DIR)/User/malloc                                  \
	-I $(PROJECT_DIR)/include/generated

ifeq ($(CONFIG_APP_FATFS),y)
INCLUDE_PATH += -I $(PROJECT_DIR)/FATFS
endif

# 自动收集 User/h、User/app 下所有子目录（适配重组后的头文件布局）
INCLUDE_PATH += $(addprefix -I,$(shell find $(PROJECT_DIR)/User/h $(PROJECT_DIR)/User/app -type d 2>/dev/null))

ASM_SRCS = $(PROJECT_DIR)/Libraries/CMSIS/Device/ST/STM32F4xx/Source/Templates/gcc/startup_stm32f40xx.s \
           $(PROJECT_DIR)/Libraries/CMSIS/Device/ST/STM32F4xx/Source/Templates/gcc/reset_handler.s

ifeq ($(STM32F4XX_CHIP),STM32F407xx)
endif

# 排除：生成的网页镜像、单元测试、易冲突源文件 + Kconfig 可裁模块
EXCLUDE_SRC_RE = fsdata\.c|(^|/)test\.c|altcp_proxyconnect\.c|cJSON_Utils\.c|/MQTT/|/MQTTclient/
ifneq ($(CONFIG_APP_ESP8266),y)
EXCLUDE_SRC_RE := $(EXCLUDE_SRC_RE)|/ESP8266/
endif
ifneq ($(CONFIG_APP_DHT11),y)
EXCLUDE_SRC_RE := $(EXCLUDE_SRC_RE)|/dht11/
endif
ifneq ($(CONFIG_APP_MULTIBUTTON),y)
EXCLUDE_SRC_RE := $(EXCLUDE_SRC_RE)|/button/
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

# C源文件
C_SRCS := \
	$(PROJECT_DIR)/Libraries/CMSIS/Device/ST/STM32F4xx/Source/Templates/system_stm32f4xx.c \
	$(STDPERIPH_SRCS) \
	$(shell find $(PROJECT_DIR)/freeRTOS/port/gcc -name "*.c") \
	$(shell find $(PROJECT_DIR)/freeRTOS/src -name "*.c") \
	$(shell find $(PROJECT_DIR)/lwip-2.1.2/port/Standalone -name "*.c") \
	$(shell find $(PROJECT_DIR)/lwip-2.1.2/src/api -name "*.c") \
	$(shell find $(PROJECT_DIR)/lwip-2.1.2/src/core -maxdepth 1 -name "*.c") \
	$(shell find $(PROJECT_DIR)/lwip-2.1.2/src/core/ipv4 -name "*.c") \
	$(PROJECT_DIR)/lwip-2.1.2/src/netif/ethernet.c \
	$(shell find $(PROJECT_DIR)/User/bsp -name "*.c" | grep -vE '$(EXCLUDE_SRC_RE)') \
	$(shell find $(PROJECT_DIR)/User/app -name "*.c" | grep -vE '$(EXCLUDE_SRC_RE)') \
	$(shell find $(PROJECT_DIR)/User/malloc -name "*.c" 2>/dev/null)

ifeq ($(CONFIG_APP_FATFS),y)
C_SRCS += \
	$(PROJECT_DIR)/FATFS/ff.c \
	$(PROJECT_DIR)/FATFS/diskio.c
endif

# 目标文件全部进 build/obj/，镜像源码相对路径
SRC_TO_OBJ = $(addprefix $(OBJ_DIR)/,$(patsubst $(PROJECT_DIR)/%,%,$(patsubst %.c,%.o,$(patsubst %.s,%.o,$(1)))))
ASM_OBJS := $(call SRC_TO_OBJ,$(ASM_SRCS))
C_OBJS   := $(call SRC_TO_OBJ,$(C_SRCS))
OBJS     := $(ASM_OBJS) $(C_OBJS)

# V=1 显示完整编译命令；默认只打印一行 CC/AS/LD/BIN
ifeq ($(V),1)
Q :=
else
Q := @
endif

PROJ_ROOT := $(abspath ..)
GEN_UPG   := $(PROJ_ROOT)/genUpgBin.py
GEN_FSDATA := $(PROJ_ROOT)/web/gen_fsdata.py
FSDATA_C   := $(PROJECT_DIR)/User/app/http/fsdata.c
ROM_WEB    := $(PROJ_ROOT)/web/rom/index.html $(PROJ_ROOT)/web/rom/404.html

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

# 默认：双槽镜像 + app.bin(APP1 别名，供 flash.sh) + upg.bin
all: syncconfig $(BUILD_DIR)/app1.bin $(BUILD_DIR)/app2.bin $(BUILD_DIR)/$(PROJECT).bin
	@$(MAKE) --no-print-directory upg

# APP1/APP2 bin 变更后同步生成双槽 OTA 包
.PHONY: fsdata
fsdata: $(FSDATA_C)

$(FSDATA_C): $(ROM_WEB) $(GEN_FSDATA)
	@echo "FSDATA $@"
	python3 "$(GEN_FSDATA)" --rom-dir "$(PROJ_ROOT)/web/rom" --out "$@"

$(OBJ_DIR)/User/app/http/fs.o: $(FSDATA_C)

.PHONY: upg
upg: $(BUILD_DIR)/app1.bin $(BUILD_DIR)/app2.bin
	@if [ -f "$(GEN_UPG)" ]; then \
		echo "UPG $(PROJ_ROOT)/upg.bin (dual-slot)"; \
		python3 "$(GEN_UPG)" --fw1 "$(abspath $(BUILD_DIR)/app1.bin)" \
			--fw2 "$(abspath $(BUILD_DIR)/app2.bin)" \
			--web-dir "$(PROJ_ROOT)/web" --out-dir "$(PROJ_ROOT)"; \
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
$(OBJ_DIR)/Libraries/%.o: CFLAGS += -Wno-deprecated -Wno-unused-parameter
$(OBJ_DIR)/lwip-2.1.2/%.o: CFLAGS += -Wno-unused-variable -Wno-unused-function -Wno-unused-but-set-variable
$(OBJ_DIR)/User/app/cJSON/cJSON.o: CFLAGS += -Wno-misleading-indentation

# 编译 C 文件（依赖 autoconf），产物在 build/obj/
$(OBJ_DIR)/%.o: $(PROJECT_DIR)/%.c $(AUTOCONF_H)
	@mkdir -p $(dir $@)
	@echo "CC  $<"
	$(Q)$(CC) $(CFLAGS) $(INCLUDE_PATH) -c $< -o $@

# 编译汇编文件
$(OBJ_DIR)/%.o: $(PROJECT_DIR)/%.s
	@mkdir -p $(dir $@)
	@echo "AS  $<"
	$(Q)$(AS) $(CFLAGS) $(INCLUDE_PATH) -c $< -o $@

# 清理目标：build/ + 历史残留在源码树里的 .o
clean:
	rm -rf $(BUILD_DIR)
	@find $(PROJECT_DIR)/Libraries $(PROJECT_DIR)/freeRTOS $(PROJECT_DIR)/lwip-2.1.2 \
		$(PROJECT_DIR)/User $(PROJECT_DIR)/FATFS $(PROJECT_DIR)/malloc \
		-name '*.o' -delete 2>/dev/null || true

# clangd：本目录独立 compile_commands.json（勿与 bootloader 合并）
.PHONY: compdb
compdb:
	@command -v bear >/dev/null 2>&1 || { echo "ERROR: bear not found (sudo apt install bear)"; exit 1; }
	rm -rf $(OBJ_DIR)
	bear --output $(PROJECT_DIR)/compile_commands.json -- $(MAKE) -j$$(nproc) $(BUILD_DIR)/app1.elf
