# Auto-generated Makefile for STM32 project
PROJECT = loader
ifndef PREFIX
#编译链没有指定路径,需要加到环境变量
PREFIX = arm-none-eabi-
endif

PROJECT_DIR = $(CURDIR)
BUILD_DIR = build

# 当前使用的芯片型号宏定义（用于控制兼容性）
STM32F4XX_CHIP = STM32F407xx

CC = $(PREFIX)gcc
AS = $(PREFIX)gcc -x assembler-with-cpp #汇编文件处理
LD = $(PREFIX)gcc
OBJCOPY = $(PREFIX)objcopy
STRIP   = $(PREFIX)strip

MACRO_SUPPORT= -DUSE_STDPERIPH_DRIVER -DSTM32F40_41xxx 

#CFLAGS = -Wall -g -O2 -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=softfp $(MACRO_SUPPORT)
#LDFLAGS = -T link.ld -Wl,--gc-sections -Wl,-Map=build/loader.map

CFLAGS = -Wall -Os -ffunction-sections -fdata-sections -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=softfp $(MACRO_SUPPORT)
LDFLAGS = -T link.ld -Wl,--gc-sections -Wl,-Map=build/loader.map --specs=nano.specs

INCLUDE_PATH = -I $(PROJECT_DIR)/Libraries/STM32F4xx_StdPeriph_Driver/inc     \
			   -I $(PROJECT_DIR)/Libraries/CMSIS/Include                      \
			   -I $(PROJECT_DIR)/Libraries/CMSIS/Device/ST/STM32F4xx/Include  \
			   -I $(PROJECT_DIR)/Libraries/STM32F4xx_StdPeriph_Driver/inc     \
			   -I $(PROJECT_DIR)/User/h/bsp/spiFlash                          \
			   -I $(PROJECT_DIR)/User/h/bsp/internalFlash                     \
			   -I $(PROJECT_DIR)/User/h/bsp/usart                             \
			   -I $(PROJECT_DIR)/User/h/bsp/SRAM                              \
			   -I $(PROJECT_DIR)/User/h/bsp/led                               \
			   -I $(PROJECT_DIR)/User/h/bsp/timer                             \
			   -I $(PROJECT_DIR)/User/h/app/upgrade                           \
			   -I $(PROJECT_DIR)/User/h/app/shell                             \
			   -I $(PROJECT_DIR)/User/h/app/ymodem                            \
			   -I $(PROJECT_DIR)/User/h/app

ASM_SRCS = $(PROJECT_DIR)/Libraries/CMSIS/Device/ST/STM32F4xx/Source/Templates/gcc/startup_stm32f40xx.s \
           $(PROJECT_DIR)/Libraries/CMSIS/Device/ST/STM32F4xx/Source/Templates/gcc/reset_handler.s

BSP_CSRCS_DIR = $(PROJECT_DIR)/Libraries/STM32F4xx_StdPeriph_Driver/src

# F407标准库C源码（硬件驱动）
BSP_CSRCS := $(shell find $(BSP_CSRCS_DIR) -name "*.c")

# 明确排除不兼容 STM32F407 的文件
# 兼容性排除项（根据芯片型号）
ifeq ($(STM32F4XX_CHIP),STM32F407xx)
$(info ==> STM32F407xx selected)
BSP_CSRCS := $(shell find $(BSP_CSRCS_DIR) -name "*.c" | grep -vE 'stm32f4xx_dma2d.c|stm32f4xx_fmc.c|stm32f4xx_ltdc.c')
endif

#$(info BSP_CSRCS after filter: $(BSP_CSRCS))

# C源文件（标准库用已过滤的 BSP_CSRCS，避免编入 F407 不支持的 dma2d/fmc/ltdc）
C_SRCS := \
  $(PROJECT_DIR)/Libraries/CMSIS/Device/ST/STM32F4xx/Source/Templates/system_stm32f4xx.c \
  $(BSP_CSRCS) \
  $(shell find $(PROJECT_DIR)/User/bsp/spiFlash -name "*.c") \
  $(shell find $(PROJECT_DIR)/User/bsp/internalFlash -name "*.c") \
  $(shell find $(PROJECT_DIR)/User/bsp/usart -name "*.c") \
  $(shell find $(PROJECT_DIR)/User/bsp/SRAM -name "*.c") \
  $(shell find $(PROJECT_DIR)/User/bsp/led -name "*.c") \
  $(shell find $(PROJECT_DIR)/User/bsp/timer -name "*.c") \
  $(shell find $(PROJECT_DIR)/User/app -name "*.c")


# 目标文件
ASM_OBJS := $(patsubst %.s,%.o,$(ASM_SRCS))
C_OBJS   := $(patsubst %.c,%.o,$(C_SRCS))
OBJS     := $(ASM_OBJS) $(C_OBJS)

# 默认目标
all: $(BUILD_DIR)/$(PROJECT).elf $(BUILD_DIR)/$(PROJECT).bin

# 链接生成 elf
$(BUILD_DIR)/$(PROJECT).elf: $(OBJS)
	@mkdir -p $(BUILD_DIR)
	$(LD) $(CFLAGS) $(INCLUDE_PATH) $^ $(LDFLAGS) -o $@

# 生成 bin 文件
$(BUILD_DIR)/$(PROJECT).bin: $(BUILD_DIR)/$(PROJECT).elf
	$(OBJCOPY) -O binary $< $@

# 编译 C 文件
%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDE_PATH) -c $< -o $@

# 编译汇编文件
%.o: %.s
	$(AS) $(CFLAGS) $(INCLUDE_PATH) -c $< -o $@

# 清理目标
clean:
	rm -f $(OBJS) $(BUILD_DIR)/*

.PHONY: all clean

