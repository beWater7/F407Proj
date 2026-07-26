# Auto-generated Makefile for STM32 project
PROJECT = app
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

CFLAGS = -Wall -Os -ffunction-sections -fdata-sections -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=softfp $(MACRO_SUPPORT) \
	-Wno-incompatible-pointer-types -Wno-implicit-function-declaration -Wno-int-conversion -Wno-return-type
LDFLAGS = -T link.ld -Wl,--gc-sections -Wl,-Map=build/$(PROJECT).map --specs=nano.specs -lm

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
	-I $(PROJECT_DIR)/FATFS

# 自动收集 User/h、User/app 下所有子目录（适配重组后的头文件布局）
INCLUDE_PATH += $(addprefix -I,$(shell find $(PROJECT_DIR)/User/h $(PROJECT_DIR)/User/app -type d 2>/dev/null))

ASM_SRCS = $(PROJECT_DIR)/Libraries/CMSIS/Device/ST/STM32F4xx/Source/Templates/gcc/startup_stm32f40xx.s \
           $(PROJECT_DIR)/Libraries/CMSIS/Device/ST/STM32F4xx/Source/Templates/gcc/reset_handler.s

ifeq ($(STM32F4XX_CHIP),STM32F407xx)
$(info ==> STM32F407xx selected)
endif

# 排除：生成的网页镜像、单元测试、易冲突源文件（node_tree 已接入协议，不再排除）
EXCLUDE_SRC_RE = 'fsdata\.c|(^|/)test\.c|altcp_proxyconnect\.c|cJSON_Utils\.c|/MQTT/|/MQTTclient/'





# C源文件
C_SRCS := \
	$(PROJECT_DIR)/Libraries/CMSIS/Device/ST/STM32F4xx/Source/Templates/system_stm32f4xx.c \
	$(shell find $(PROJECT_DIR)/Libraries/STM32F4xx_StdPeriph_Driver/src -name "*.c" | grep -vE 'stm32f4xx_dma2d\.c|stm32f4xx_fmc\.c|stm32f4xx_ltdc\.c') \
	$(shell find $(PROJECT_DIR)/freeRTOS/port/gcc -name "*.c") \
	$(shell find $(PROJECT_DIR)/freeRTOS/src -name "*.c") \
	$(shell find $(PROJECT_DIR)/lwip-2.1.2/port/Standalone -name "*.c") \
	$(shell find $(PROJECT_DIR)/lwip-2.1.2/src/api -name "*.c") \
	$(shell find $(PROJECT_DIR)/lwip-2.1.2/src/core -name "*.c") \
	$(shell find $(PROJECT_DIR)/lwip-2.1.2/src/core/ipv4 -name "*.c") \
	$(shell find $(PROJECT_DIR)/lwip-2.1.2/src/core/ipv6 -name "*.c") \
	$(shell find $(PROJECT_DIR)/lwip-2.1.2/src/netif -name "*.c") \
	$(shell find $(PROJECT_DIR)/User/bsp -name "*.c" | grep -vE $(EXCLUDE_SRC_RE)) \
	$(shell find $(PROJECT_DIR)/User/app -name "*.c" | grep -vE $(EXCLUDE_SRC_RE)) \
	$(shell find $(PROJECT_DIR)/User/malloc -name "*.c" 2>/dev/null) \
	$(PROJECT_DIR)/FATFS/ff.c \
	$(PROJECT_DIR)/FATFS/diskio.c

# 目标文件
ASM_OBJS := $(patsubst %.s,%.o,$(ASM_SRCS))
C_OBJS   := $(patsubst %.c,%.o,$(C_SRCS))
OBJS     := $(ASM_OBJS) $(C_OBJS)

# V=1 显示完整编译命令；默认只打印一行 CC/AS/LD/BIN
ifeq ($(V),1)
Q :=
else
Q := @
endif

# 默认目标
all: $(BUILD_DIR)/$(PROJECT).elf $(BUILD_DIR)/$(PROJECT).bin

# 链接生成 elf
$(BUILD_DIR)/$(PROJECT).elf: $(OBJS)
	@mkdir -p $(BUILD_DIR)
	@echo "LD  $@"
	$(Q)$(LD) $(CFLAGS) $(INCLUDE_PATH) $^ $(LDFLAGS) -o $@

# 生成 bin 文件
$(BUILD_DIR)/$(PROJECT).bin: $(BUILD_DIR)/$(PROJECT).elf
	@echo "BIN $@"
	$(Q)$(OBJCOPY) -O binary $< $@

# 编译 C 文件
%.o: %.c
	@echo "CC  $<"
	$(Q)$(CC) $(CFLAGS) $(INCLUDE_PATH) -c $< -o $@

# 编译汇编文件
%.o: %.s
	@echo "AS  $<"
	$(Q)$(AS) $(CFLAGS) $(INCLUDE_PATH) -c $< -o $@

# 清理目标
clean:
	rm -f $(OBJS) $(BUILD_DIR)/*

.PHONY: all clean
