import os

PROJECT_NAME = "main"
MCU = "STM32F407VG"  # 可根据具体芯片型号调整
CFLAGS = "-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=softfp"
LDSCRIPT = "link.ld"  # 假设你已提供

def collect_sources_and_includes(root):
    sources = []
    includes = set()

    for dirpath, _, filenames in os.walk(root):
        for file in filenames:
            if file.endswith(".c"):
                full_path = os.path.join(dirpath, file).replace("\\", "/")
                sources.append(full_path)
                includes.add(dirpath.replace("\\", "/"))
            elif file.endswith(".s") or file.endswith(".S"):
                full_path = os.path.join(dirpath, file).replace("\\", "/")
                sources.append(full_path)
                includes.add(dirpath.replace("\\", "/"))

    return sources, includes

def generate_makefile(project_root):
    sources, includes = collect_sources_and_includes(project_root)
    srcs_str = " \\\n  ".join(sources)
    incs_str = " ".join(f"-I{inc}" for inc in includes)

    makefile_content = f"""# Auto-generated Makefile for STM32 project
PROJECT = {PROJECT_NAME}
BUILD_DIR = build
CC = arm-none-eabi-gcc
AS = arm-none-eabi-gcc -x assembler-with-cpp
LD = arm-none-eabi-gcc
OBJCOPY = arm-none-eabi-objcopy
CFLAGS = -Wall -g -O2 {CFLAGS} {incs_str}
LDFLAGS = -T{LDSCRIPT} -Wl,--gc-sections

SRCS = \\
  {srcs_str}

OBJS = $(SRCS:.c=.o)
OBJS := $(OBJS:.s=.o)
OBJS := $(OBJS:.S=.o)

all: $(BUILD_DIR)/$(PROJECT).elf $(BUILD_DIR)/$(PROJECT).bin

$(BUILD_DIR)/$(PROJECT).elf: $(OBJS)
\tmkdir -p $(BUILD_DIR)
\t$(LD) $(CFLAGS) $(OBJS) $(LDFLAGS) -o $@

$(BUILD_DIR)/$(PROJECT).bin: $(BUILD_DIR)/$(PROJECT).elf
\t$(OBJCOPY) -O binary $< $@

clean:
\trm -f $(OBJS) $(BUILD_DIR)/*

.PHONY: all clean
"""
    with open(os.path.join(project_root, "Makefile"), "w", encoding="utf-8") as f:
        f.write(makefile_content)
    print("✅ Makefile 已生成！")

if __name__ == "__main__":
    project_path = os.path.dirname(os.path.abspath(__file__))
    generate_makefile(project_path)
