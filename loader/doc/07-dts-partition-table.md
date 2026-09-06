# DTS 板级设备树与动态分区表

工程引入**数据驱动的设备树(dts)**机制：板级硬件/分区信息写在文本 `.dts` 源里，
编译成紧凑二进制 `.dtb`，既烧进 SPI flash 动态加载，也编译进固件作为兜底。

## 一、这套机制解决什么

- **硬件/驱动解耦**：驱动按 `compatible` 等属性匹配，换板子不用改驱动代码。
- **统一分区表**：SPI flash 分区布局（addr/size/flags）由 `.dts` 单一来源定义，
  不再散落在 app/loader 各处的数组里（app 侧代码里的表被 dts 覆盖）。
- **易扩展**：新增板子 = 新增一个 `.dts`，复用同一套驱动与工具链。

## 二、文件与产物

| 文件 | 说明 |
| --- | --- |
| `platform/boards/f407zg/f407zg.dts` | 板级描述 + 分区表（**改分区就改这里**） |
| `tools/pack/gen_dtb.py` | 纯 Python dts 编译器：`dts → dtb 二进制 + C 数组`（无 dtc 依赖） |
| `dist/f407zg.dtb` | 编译产物，烧 SPI flash dtb 保留区用 |
| `app/User/app/dts/dts_fallback.c` | 自动生成的固件内置兜底表（自动编进 app） |
| `platform/hal/dts.h` / `platform/stm32f4/drivers/hal/dts_f4.c` | 设备端零拷贝解析库 |
| `app/User/app/flashMng/flash_manage.c` | `dts_apply_partitions()`：用 dtb 覆盖分区表 |

**dtb 二进制布局**（与 `dts.h` 结构一一对应）：

```
[dts_header_t 32B][dts_node_t 16B×N][dts_prop_t 8B×M][strtab]
hdr_crc  = CRC16/CCITT-FALSE(blob[0:28])   —— 头部可靠性
body_crc = CRC32(zlib, blob[32:])          —— 与 common/crc.c 一致
```

## 三、修改分区表 / 硬件描述流程

```bash
# 1. 编辑 platform/boards/f407zg/f407zg.dts（改 reg / 增节点 / 改属性）

# 2. 重新生成 dtb + 内置兜底表 + 编译固件
make app                 # 产出 app/build/f407zg.dtb, dist/f407zg.dtb

# 3. 烧 dtb 到 SPI flash 主/备槽（纯串口 YMODEM，无需 SWD）
make                     # 先确保 tools/xfer/xfer 是最新的
./tools/xfer/xfer /dev/ttyUSB0 dist/f407zg.dtb
#   -> loader 识别 DTS_MAGIC(0xD45B0001) 自动写入 0xF00000 主槽 + 0xF01000 备份槽
#   -> 写完后自动重启，app 启动即读取新分区表
```

> 也可以只重新生成不烧 app 固件：`./tools/xfer/xfer` 只烧 dtb，分区表即刻生效。

### 烧写原理（loader 侧）

`tools/xfer/xfer` 对非 upg 文件走 **YMODEM 整包发送**；loader 的 `Ymodem_Receive`
按文件头魔数分流：

| 魔数 | 去向 |
| --- | --- |
| `0x55475023` (upg) | loader+APP1+APP2+web 常规升级 |
| `0xD45B0001` (dtb) | 擦除 SPI 主/备槽 → 逐包写入两个槽 → 自动重启 |
| 其它 | 裸固件 → 内部 Flash APP1 |

## 四、加载与自愈策略（app 侧）

`dts_load_default()` 按优先级加载，全部带 CRC 校验：

```
SPI flash 主槽(0xF00000) → 备份槽(0xF01000) → 固件内置静态表(兜底)
```

- 主槽损坏、备份有效 → 备份自愈主槽
- 双槽都损坏/空白 → 内置静态表兜底，并把主槽写回（首次上电自愈）
- 因此即使 dtb 从未烧过，分区表也永远可用（= 内置静态表）

## 五、运行时验证（app shell）

```text
dts           # 基本信息: model/version/src/blob大小
dts list      # 当前生效的分区表（name/addr/size/crc）
dts reload    # 重新从 flash 加载 + 应用到分区表（调试用，无需重启）
```

## 六、新增一块板子

```bash
# 1. 新建 platform/boards/<board>/<board>.dts（复用 f407zg.dts 的节点写法）
# 2. 顶层构建指定 BOARD
make BOARD=<board> app
./tools/xfer/xfer /dev/ttyUSB0 dist/<board>.dtb
```

驱动代码无需改动；相同节点名/属性即自动生效。

## 七、boot contract（勿随意改动）

- SPI flash `0xF00000 .. 0xF03FFF` 为 **dtb 保留区**（主槽 `0xF00000`、备份槽 `0xF01000`，各 4KB），
  与 `platform/hal/dts.h` 的 `DTS_FLASH_PRIMARY/DTS_FLASH_BACKUP/DTS_FLASH_SLOT_MAX` 保持一致。
- 分区 `custom` 从 `0xF04000` 开始（loader 侧静态表里的 `PART_CUSTOM=0xF00000` 为旧布局，仅作边界占位）。
- 改保留区布局需要同步：`f407zg.dts` 注释、`dts.h`、loader `ymodem.c` 里的 `DTS_FLASH_*`。
