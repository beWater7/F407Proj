# boot Makefile — 固化 boot（原 stage0），多介质加载 loader 到 SRAM 并跳转
# 产物 build/boot.bin，烧一次不再更新。
PROJECT = boot
ifndef PREFIX
PREFIX = arm-none-eabi-
endif

PROJECT_DIR = $(CURDIR)
PROJ_ROOT := $(abspath ..)
SOC ?= stm32f4
BOARD ?= f407zg
PLATFORM := $(PROJ_ROOT)/platform
# vendor 暂用 loader 侧 StdPeriph/CMSIS（合并到 platform/stm32f4/vendor 可另一步）
VENDOR_DIR := $(PROJ_ROOT)/loader/Libraries
BUILD_DIR = build
OBJ_DIR = $(BUILD_DIR)/obj

CC = $(PREFIX)gcc
AS = $(PREFIX)gcc -x assembler-with-cpp
LD = $(PREFIX)gcc
OBJCOPY = $(PREFIX)objcopy
SIZE = $(PREFIX)size

MACRO_SUPPORT = -DUSE_STDPERIPH_DRIVER -DSTM32F40_41xxx

CFLAGS = -Wall -Os -ffunction-sections -fdata-sections -mcpu=cortex-m4 -mthumb \
	-mfpu=fpv4-sp-d16 -mfloat-abi=softfp $(MACRO_SUPPORT) -Wno-deprecated
LDFLAGS = -T $(PLATFORM)/boards/$(BOARD)/boot.ld -Wl,--gc-sections -Wl,-Map=build/boot.map --specs=nano.specs

INCLUDE_PATH = -I $(VENDOR_DIR)/STM32F4xx_StdPeriph_Driver/inc \
			   -I $(VENDOR_DIR)/CMSIS/Include \
			   -I $(VENDOR_DIR)/CMSIS/Device/ST/STM32F4xx/Include \
			   -I $(PROJ_ROOT)/common \
			   -I $(PLATFORM)/hal \
			   -I $(PLATFORM)/$(SOC) \
			   -I $(PLATFORM)/boards/$(BOARD) \
			   -I $(PLATFORM)/$(SOC)/include/bsp/spiFlash \
			   -I $(PLATFORM)/$(SOC)/include/bsp/usart \
			   -I $(PLATFORM)/$(SOC)/include/bsp/led \
			   -I $(PLATFORM)/$(SOC)/include/bsp/internalFlash \
			   -I $(PROJECT_DIR)

ASM_SRCS = $(VENDOR_DIR)/CMSIS/Device/ST/STM32F4xx/Source/Templates/gcc/startup_stm32f40xx.s \
           $(VENDOR_DIR)/CMSIS/Device/ST/STM32F4xx/Source/Templates/gcc/reset_handler.s

STDPERIPH_SRCS := $(shell find $(VENDOR_DIR)/STM32F4xx_StdPeriph_Driver/src -name "*.c" \
             | grep -vE 'stm32f4xx_dma2d.c|stm32f4xx_fmc.c|stm32f4xx_ltdc.c')

# 最小驱动子集：不要把 ETH/ESP 等链进 32K boot
C_SRCS := \
  $(VENDOR_DIR)/CMSIS/Device/ST/STM32F4xx/Source/Templates/system_stm32f4xx.c \
  $(STDPERIPH_SRCS) \
  $(PLATFORM)/$(SOC)/drivers/spiFlash/bsp_spi_flash.c \
  $(PLATFORM)/$(SOC)/drivers/usart/bsp_debug_usart.c \
  $(PLATFORM)/$(SOC)/drivers/led/bsp_led.c \
  $(PROJ_ROOT)/common/crc.c \
  $(PROJECT_DIR)/main.c \
  $(PROJECT_DIR)/syscalls.c

SRC_TO_OBJ = $(addprefix $(OBJ_DIR)/,$(patsubst $(PROJ_ROOT)/%,%,$(patsubst %.c,%.o,$(patsubst %.s,%.o,$(1)))))
ASM_OBJS := $(call SRC_TO_OBJ,$(ASM_SRCS))
C_OBJS   := $(call SRC_TO_OBJ,$(C_SRCS))
OBJS     := $(ASM_OBJS) $(C_OBJS)

ifeq ($(V),1)
Q :=
else
Q := @
endif

all: $(BUILD_DIR)/$(PROJECT).elf $(BUILD_DIR)/$(PROJECT).bin
	@echo "== boot.bin built =="
	@$(SIZE) $(BUILD_DIR)/$(PROJECT).elf

$(BUILD_DIR)/$(PROJECT).elf: $(OBJS)
	@mkdir -p $(BUILD_DIR)
	@echo "LD  $@"
	$(Q)$(LD) $(CFLAGS) $(INCLUDE_PATH) $^ $(LDFLAGS) -o $@

$(BUILD_DIR)/$(PROJECT).bin: $(BUILD_DIR)/$(PROJECT).elf
	@echo "BIN $@"
	$(Q)$(OBJCOPY) -O binary $< $@

$(OBJ_DIR)/loader/Libraries/%.o: CFLAGS += -Wno-deprecated -Wno-unused-parameter -Wno-misleading-indentation
$(OBJ_DIR)/platform/%.o: CFLAGS += -Wno-unused-variable -Wno-unused-function -Wno-unused-but-set-variable

$(OBJ_DIR)/%.o: $(PROJ_ROOT)/%.c
	@mkdir -p $(dir $@)
	@echo "CC  $<"
	$(Q)$(CC) $(CFLAGS) $(INCLUDE_PATH) -c $< -o $@

$(OBJ_DIR)/%.o: $(PROJ_ROOT)/%.s
	@mkdir -p $(dir $@)
	@echo "AS  $<"
	$(Q)$(AS) $(CFLAGS) $(INCLUDE_PATH) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean
