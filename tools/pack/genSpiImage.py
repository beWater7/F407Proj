#!/usr/bin/env python3
"""
genSpiImage.py — 生成 W25Q128 全量 SPI NOR 镜像（离线烧录器直烧）

布局（与 loader/app flash_manage.h 的 spi_flash_table 必须一致）::

    0x000000  PART_LOADER    64K  [PartitionHeader][loader_header_t][loader.bin]
    0x010000  PART_LOADER_BK 64K  [PartitionHeader][loader_header_t][loader.bin]
    0x600000  PART_APP1       2M   [PartitionHeader][app1.bin]
    0x800000  PART_APP2       2M   [PartitionHeader][app2.bin]
    0xA00000  PART_OTA        1M   [PartitionHeader][ota_flag_t(默认 active=APP1)]
    0xB00000  PART_LOG        2M   [PartitionHeader](空)
    0xD00000  PART_WEB        1M   [PartitionHeader][web.bin]（可选）
    0xE00000  PART_CONFIG     1M   [PartitionHeader](空)
    0xF00000  PART_CUSTOM     1M   [PartitionHeader](空)

PartitionHeader {magic=0x55AA55AA, used_size, crc32(前8字节)}，其余填充 0xFF。
loader blob 头 loader_header_t {magic=0x52444C32, version, size, crc32(loader.bin)}。

stage0 启动流程只依赖本镜像：loader + app1/app2 一次烧进 SPI，
内部 Flash 只烧一次 boot.bin。loader 升级由 APP OTA / 串口直接覆盖 PART_LOADER 主区。
"""

from __future__ import annotations

import argparse
import struct
import sys
import zlib
from pathlib import Path

# 与 loader_meta.h / flash_manage.h 保持一致
PARTITION_MAGIC = 0x55AA55AA
LOADER_HDR_MAGIC = 0x52444C32  # "2LDR"
OTA_FLAG_MAGIC = 0xA5A5A5A5
OTA_FLAG_UPGRADE_PENDING = 1


def load_loader_version() -> int:
    """从 common/loader_meta.h 读取 LOADER_VERSION，保持版本号单一来源。"""
    import re

    here = Path(__file__).resolve()
    meta = here.parents[2] / "common" / "loader_meta.h"
    m = re.search(r"#define\s+LOADER_VERSION\s+(0x[0-9A-Fa-f]+)u?", meta.read_text())
    if not m:
        raise RuntimeError(f"cannot parse LOADER_VERSION from {meta}")
    return int(m.group(1), 16)


LOADER_VERSION = load_loader_version()

PART_HEADER_SIZE = 0x1000  # 分区头单独占一个 4K 扇区
SPI_SIZE = 16 * 1024 * 1024  # W25Q128
LOADER_SIZE_LIMIT = 64 * 1024

PARTS = {
    # name: (base, size)
    "loader":     (0x000000, 64 * 1024),
    "loader_bk":  (0x010000, 64 * 1024),
    "app1":       (0x600000, 2 * 1024 * 1024),
    "app2":       (0x800000, 2 * 1024 * 1024),
    "ota":        (0xA00000, 1 * 1024 * 1024),
    "log":        (0xB00000, 2 * 1024 * 1024),
    "web":        (0xD00000, 1 * 1024 * 1024),
    "config":     (0xE00000, 1 * 1024 * 1024),
    "custom":     (0xF00000, 1 * 1024 * 1024),
}

PARTITION_MAGIC_HEX = f"0x{PARTITION_MAGIC:08X}"
LOADER_HDR_MAGIC_HEX = f"0x{LOADER_HDR_MAGIC:08X}"


def partition_header(used_size: int) -> bytes:
    """12 字节头（magic/used_size/crc），crc 覆盖前 8 字节。"""
    hdr = struct.pack("<II", PARTITION_MAGIC, used_size)
    crc = zlib.crc32(hdr) & 0xFFFFFFFF
    return hdr + struct.pack("<I", crc)


def write_partition(img: bytearray, base: int, size: int, name: str, data: bytes | None = None):
    """在分区 base 写头，base+0x1000 写数据；超出分区大小即报错。"""
    used = len(data) if data is not None else 0
    if used > size - PART_HEADER_SIZE:
        raise RuntimeError(
            f"partition {name}: data {used} > capacity {size - PART_HEADER_SIZE}"
        )
    hdr = partition_header(used)
    if base + size > len(img):
        raise RuntimeError(f"partition {name}: base 0x{base:X} out of image")
    img[base:base + PART_HEADER_SIZE] = hdr.ljust(PART_HEADER_SIZE, b"\xff")
    if data is not None:
        img[base + PART_HEADER_SIZE:base + PART_HEADER_SIZE + len(data)] = data
    print(f"[+] {name:<12} @0x{base:07X}  used_size={used}")


def build_loader_blob(loader_bin: bytes, version: int) -> bytes:
    """[loader_header_t][loader.bin]，并校验二进制基址在 SRAM。"""
    if len(loader_bin) > LOADER_SIZE_LIMIT:
        raise RuntimeError(
            f"loader.bin {len(loader_bin)}B > {LOADER_SIZE_LIMIT}B limit"
        )
    if len(loader_bin) < 8:
        raise RuntimeError("loader.bin too small (<8B)")
    msp, reset = struct.unpack_from("<II", loader_bin, 0)
    if not (0x20000000 <= msp <= 0x20020000):
        raise RuntimeError(f"loader MSP 0x{msp:08X} not in SRAM")
    if reset & 1 == 0:
        raise RuntimeError(f"loader Reset_Handler 0x{reset:08X} not Thumb (bit0=0)")
    reset_addr = reset & ~1
    if not (0x20000000 <= reset_addr <= 0x20000000 + LOADER_SIZE_LIMIT):
        raise RuntimeError(f"loader Reset_Handler 0x{reset_addr:08X} not in RAM region")
    crc = zlib.crc32(loader_bin) & 0xFFFFFFFF
    hdr = struct.pack(
        "<IIII", LOADER_HDR_MAGIC, version, len(loader_bin), crc
    )
    return hdr + loader_bin


def build_ota_flag() -> bytes:
    """默认 ota_flag_t：无 pending，active=APP1。"""
    flag = struct.pack(
        "<IIIIIII",
        0,          # state UPDATE_IDLE
        0,          # active_app = APP1
        0,          # upgrade_flag = 0 (无 pending)
        0,          # target_app
        0,          # len
        0,          # crc32
        OTA_FLAG_MAGIC,
    )
    return flag


def repo_root() -> Path:
    # tools/pack/genSpiImage.py -> 仓库根
    return Path(__file__).resolve().parents[2]


def parse_args() -> argparse.Namespace:
    root = repo_root()
    p = argparse.ArgumentParser(
        description="生成 W25Q128 全量 SPI NOR 镜像（离线烧录器直烧）"
    )
    p.add_argument("--loader", type=Path,
                   default=root / "loader" / "build" / "loader.bin",
                   help="loader SRAM 变体 bin（默认 loader/build/loader.bin）")
    p.add_argument("--loader-version", type=lambda s: int(s, 0),
                   default=LOADER_VERSION, help="loader_header_t.version")
    p.add_argument("--app1", type=Path,
                   default=root / "app" / "build" / "app1.bin",
                   help="APP1 镜像（默认 app/build/app1.bin）")
    p.add_argument("--app2", type=Path,
                   default=root / "app" / "build" / "app2.bin",
                   help="APP2 镜像（默认 app/build/app2.bin）")
    p.add_argument("--web-bin", type=Path, default=None,
                   help="web.bin（可选，写入 PART_WEB）")
    p.add_argument("--out", type=Path,
                   default=root / "spi_image.bin",
                   help="输出镜像路径（默认仓库根/spi_image.bin）")
    p.add_argument("--size", type=lambda s: int(s, 0),
                   default=SPI_SIZE, help="镜像总大小，默认 16MB")
    return p.parse_args()


def main() -> int:
    args = parse_args()
    root = args.out.parent

    loader_bin = args.loader
    app1 = args.app1
    app2 = args.app2
    missing = [p for p in (loader_bin, app1, app2) if not p.is_file()]
    if missing:
        print("错误：找不到以下产物，请先 make：", file=sys.stderr)
        for p in missing:
            print(f"  {p}", file=sys.stderr)
        print("  boot:   cd boot && make", file=sys.stderr)
        print("  loader: cd loader && make", file=sys.stderr)
        print("  app:    cd app && make", file=sys.stderr)
        return 1

    img = bytearray(b"\xff" * args.size)

    blob = build_loader_blob(loader_bin.read_bytes(), args.loader_version)
    print(f"[+] loader.bin: {loader_bin} ({loader_bin.stat().st_size}B) -> blob {len(blob)}B")
    write_partition(img, *PARTS["loader"], "loader", blob)
    write_partition(img, *PARTS["loader_bk"], "loader_bk", blob)

    app1_data = app1.read_bytes()
    app2_data = app2.read_bytes()
    print(f"[+] app1: {app1} ({len(app1_data)}B)")
    print(f"[+] app2: {app2} ({len(app2_data)}B)")
    write_partition(img, *PARTS["app1"], "app1", app1_data)
    write_partition(img, *PARTS["app2"], "app2", app2_data)

    write_partition(img, *PARTS["ota"], "ota", build_ota_flag())
    write_partition(img, *PARTS["log"], "log", None)

    web_data = None
    if args.web_bin is not None:
        web_data = args.web_bin.read_bytes()
        print(f"[+] web: {args.web_bin} ({len(web_data)}B)")
    write_partition(img, *PARTS["web"], "web", web_data)

    write_partition(img, *PARTS["config"], "config", None)
    write_partition(img, *PARTS["custom"], "custom", None)

    root.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(img)
    print(f"[+] 生成 {args.out} ({len(img)}B)")
    print()
    print("烧录（离线烧录器/W25Q128 直写整片），或按分区偏移直烧：")
    for name, (base, _) in PARTS.items():
        print(f"    {name:<12} @ 0x{base:07X}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
