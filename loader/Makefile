# loader — 运行于 SRAM 的启动加载器（boot 搬运到 0x20000000 执行）。
#   make          -> build/loader.bin       （SRAM 变体，进 upg.bin / SPI 主 loader）
#   make loader_flash -> build/loader_flash.bin （Flash 变体，旧单级 boot 兼容）
PROJECT = loader
ifndef PREFIX
PREFIX = arm-none-eabi-
endif

PROJECT_DIR = $(CURDIR)
PROJ_ROOT := $(abspath ..)
SOC ?= stm32f4
BOARD ?= f407zg
PLATFORM := $(PROJ_ROOT)/platform
BUILD_DIR = build
OBJ_DIR = $(BUILD_DIR)/obj

CC = $(PREFIX)gcc
AS = $(PREFIX)gcc -x assembler-with-cpp
LD = $(PREFIX)gcc
OBJCOPY = $(PREFIX)objcopy
STRIP   = $(PREFIX)strip

MACRO_SUPPORT= -DUSE_STDPERIPH_DRIVER -DSTM32F40_41xxx

CFLAGS = -Wall -Os -ffunction-sections -fdata-sections -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=softfp $(MACRO_SUPPORT) \
	-Wno-deprecated

INCLUDE_PATH = -I $(PROJECT_DIR)/Libraries/STM32F4xx_StdPeriph_Driver/inc     \
			   -I $(PROJECT_DIR)/Libraries/CMSIS/Include                      \
			   -I $(PROJECT_DIR)/Libraries/CMSIS/Device/ST/STM32F4xx/Include  \
			   -I $(PROJ_ROOT)/common                                         \
			   -I $(PLATFORM)/hal                                             \
			   -I $(PLATFORM)/$(SOC)                                          \
			   -I $(PLATFORM)/boards/$(BOARD)                                 \
			   -I $(PROJECT_DIR)/User/h/app/upgrade                           \
			   -I $(PROJECT_DIR)/User/h/app/shell                             \
			   -I $(PROJECT_DIR)/User/h/app/ymodem                            \
			   -I $(PROJECT_DIR)/User/h/app

INCLUDE_PATH += $(addprefix -I,$(shell find $(PLATFORM)/$(SOC)/include -type d 2>/dev/null))

ASM_SRCS = $(PROJECT_DIR)/Libraries/CMSIS/Device/ST/STM32F4xx/Source/Templates/gcc/startup_stm32f40xx.s \
           $(PROJECT_DIR)/Libraries/CMSIS/Device/ST/STM32F4xx/Source/Templates/gcc/reset_handler.s

BSP_CSRCS_DIR = $(PROJECT_DIR)/Libraries/STM32F4xx_StdPeriph_Driver/src
BSP_CSRCS := $(shell find $(BSP_CSRCS_DIR) -name "*.c" | grep -vE 'stm32f4xx_dma2d.c|stm32f4xx_fmc.c|stm32f4xx_ltdc.c')

# 与 app 共用 platform 驱动；不要链 ETH/ESP/DHT 等
C_SRCS := \
  $(PROJECT_DIR)/Libraries/CMSIS/Device/ST/STM32F4xx/Source/Templates/system_stm32f4xx.c \
  $(BSP_CSRCS) \
  $(PLATFORM)/$(SOC)/drivers/spiFlash/bsp_spi_flash.c \
  $(PLATFORM)/$(SOC)/drivers/internalFlash/bsp_internalFlash.c \
  $(PLATFORM)/$(SOC)/drivers/usart/bsp_debug_usart.c \
  $(PLATFORM)/$(SOC)/drivers/SRAM/bsp_sram.c \
  $(PLATFORM)/$(SOC)/drivers/led/bsp_led.c \
  $(PLATFORM)/$(SOC)/drivers/timer/bsp_timer.c \
  $(PROJ_ROOT)/common/crc.c \
  $(shell find $(PROJECT_DIR)/User/app -name "*.c" | grep -vE '(^|/)crc\.c')

SRC_TO_OBJ = $(addprefix $(OBJ_DIR)/,$(patsubst $(PROJ_ROOT)/%,%,$(patsubst %.c,%.o,$(patsubst %.s,%.o,$(1)))))
ASM_OBJS := $(call SRC_TO_OBJ,$(ASM_SRCS))
C_OBJS   := $(call SRC_TO_OBJ,$(C_SRCS))
OBJS     := $(ASM_OBJS) $(C_OBJS)

ifeq ($(V),1)
Q :=
else
Q := @
endif

all: $(BUILD_DIR)/$(PROJECT).bin

RAM_LDFLAGS = -T $(PLATFORM)/boards/$(BOARD)/loader.ld -Wl,--gc-sections -Wl,-Map=build/loader.map --specs=nano.specs

$(BUILD_DIR)/$(PROJECT).elf: $(OBJS)
	@mkdir -p $(BUILD_DIR)
	@echo "LD  $@ (SRAM 0x20000000)"
	$(Q)$(LD) $(CFLAGS) $(INCLUDE_PATH) $^ $(RAM_LDFLAGS) -o $@

$(BUILD_DIR)/$(PROJECT).bin: $(BUILD_DIR)/$(PROJECT).elf
	@echo "BIN $@"
	$(Q)$(OBJCOPY) -O binary $< $@

FLASH_LDFLAGS = -T $(PLATFORM)/boards/$(BOARD)/loader_flash.ld -Wl,--gc-sections -Wl,-Map=build/loader_flash.map --specs=nano.specs

$(BUILD_DIR)/loader_flash.elf: $(OBJS)
	@mkdir -p $(BUILD_DIR)
	@echo "LD  $@ (FLASH 0x08000000, legacy)"
	$(Q)$(LD) $(CFLAGS) $(INCLUDE_PATH) $^ $(FLASH_LDFLAGS) -o $@

$(BUILD_DIR)/loader_flash.bin: $(BUILD_DIR)/loader_flash.elf
	@echo "BIN $@"
	$(Q)$(OBJCOPY) -O binary $< $@

loader_flash: $(BUILD_DIR)/loader_flash.bin
	@echo "== loader_flash.bin: $(BUILD_DIR)/loader_flash.bin =="
	@$(PREFIX)size $(BUILD_DIR)/loader_flash.elf

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
	@find $(PROJECT_DIR)/Libraries $(PROJECT_DIR)/User \
		-name '*.o' -delete 2>/dev/null || true

.PHONY: all clean compdb loader_flash
compdb:
	@command -v bear >/dev/null 2>&1 || { echo "ERROR: bear not found (sudo apt install bear)"; exit 1; }
	rm -rf $(OBJ_DIR)
	bear --output $(PROJECT_DIR)/compile_commands.json -- $(MAKE) -j$$(nproc) $(BUILD_DIR)/$(PROJECT).elf
