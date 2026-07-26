#!/usr/bin/env python3
"""
genUpgBin.py — 生成 OTA 组合固件 upg.bin

布局（与 app/User/h/app/upgrade/upgrade.h、post.c 一致）::

    ┌──────────────────────────────┐
    │ struct upg_header (16 bytes) │  magic | fw_len | web_len | crc
    ├──────────────────────────────┤
    │ APP 固件 (app.bin)           │
    ├──────────────────────────────┤
    │ web.bin（打包网页资源）       │
    └──────────────────────────────┘

web.bin 布局（与 fs.c parse_web_bin / genWebBin.py 一致）::

    uint32 file_count
    [type, size, offset] × N   （各 uint32 LE，共 12B）
    文件数据区…

资源顺序必须与 fs.c 中 gs_webFileDesc[] 一致（按索引匹配）。
"""

from __future__ import annotations

import argparse
import struct
import sys
import zlib
from pathlib import Path

# 与 upgrade.h: UPG_HDR_MAGIC / WEB_FILE_TYPE 保持一致
UPG_HDR_MAGIC = 0x55475021  # "UGP!"

WEB_HTML = 0
WEB_CSS = 1
WEB_JS = 2
WEB_PNG = 3
WEB_JPG = 4
WEB_ICO = 5
WEB_GIF = 6
WEB_GZIP = 7

# 与 app/User/app/http/fs.c gs_webFileDesc[] 顺序、类型一致
# (逻辑名用于日志, 源文件候选列表, WEB_* 类型)
# 优先使用列表中第一个存在的文件（通常是 .gz）
WEB_RESOURCES = [
    ("index.shtml", ["index.html", "index.shtml"], WEB_HTML),
    ("chart.js", ["chart.js.gz", "chart.js"], WEB_JS),
    ("chartjs-adapter-moment.min.js",
     ["chartjs-adapter-moment.min.js.gz", "chartjs-adapter-moment.min.js"], WEB_JS),
    ("chartjs-plugin-datalabels.min.js",
     ["chartjs-plugin-datalabels.min.js.gz", "chartjs-plugin-datalabels.min.js"], WEB_JS),
    ("chartjs-plugin-streaming.min.js",
     ["chartjs-plugin-streaming.min.js.gz", "chartjs-plugin-streaming.min.js"], WEB_JS),
    ("favicon.ico", ["favicon.ico.gz", "favicon.ico"], WEB_ICO),
    ("main.js", ["main.js.gz", "main.js"], WEB_JS),
    ("moment.min.js", ["moment.min.js.gz", "moment.min.js"], WEB_JS),
    ("style.css", ["style.css.gz", "style.css"], WEB_CSS),
    ("tailwindcss_3_4_17.js",
     ["tailwindcss_3_4_17.js.gz", "tailwindcss_3_4_17.js"], WEB_JS),
    ("esp8266.jpg", ["esp8266.jpg"], WEB_JPG),
]

MAX_FILES = 16


def resolve_file(web_dir: Path, candidates: list[str]) -> Path:
    for name in candidates:
        p = web_dir / name
        if p.is_file():
            return p
    raise FileNotFoundError(
        f"在 {web_dir} 中找不到任一候选: {', '.join(candidates)}"
    )


def pack_web_bin(web_dir: Path) -> bytes:
    """按 gs_webFileDesc 顺序打包 web.bin（紧凑头：4 + N*12）。"""
    files: list[tuple[str, Path, int, bytes]] = []
    for logical, candidates, ftype in WEB_RESOURCES:
        path = resolve_file(web_dir, candidates)
        data = path.read_bytes()
        files.append((logical, path, ftype, data))

    if len(files) > MAX_FILES:
        raise RuntimeError(f"资源数 {len(files)} 超过 MAX_FILES={MAX_FILES}")

    file_count = len(files)
    header_size = 4 + file_count * 12

    entries: list[tuple[int, int, int]] = []
    offset = header_size
    for _, _, ftype, data in files:
        entries.append((ftype, len(data), offset))
        offset += len(data)

    out = bytearray()
    out += struct.pack("<I", file_count)
    for ftype, size, off in entries:
        out += struct.pack("<III", ftype, size, off)
    for _, _, _, data in files:
        out += data

    print(f"[+] web.bin: {file_count} files, header={header_size}B, total={len(out)}B")
    for i, ((logical, path, ftype, data), (_, size, off)) in enumerate(
        zip(files, entries)
    ):
        print(
            f"    [{i:2d}] type={ftype} size={size:7d} offset={off:7d}  "
            f"{logical} <- {path.name}"
        )
    return bytes(out)


def build_upg(fw_data: bytes, web_data: bytes) -> tuple[bytes, bytes]:
    """返回 (header, upg_blob)。"""
    fw_len = len(fw_data)
    web_len = len(web_data)
    crc_val = zlib.crc32(fw_data + web_data) & 0xFFFFFFFF
    header = struct.pack("<IIII", UPG_HDR_MAGIC, fw_len, web_len, crc_val)
    return header, header + fw_data + web_data


def parse_args() -> argparse.Namespace:
    root = Path(__file__).resolve().parent
    p = argparse.ArgumentParser(
        description="生成 OTA 组合固件 upg.bin（header + APP + web）"
    )
    p.add_argument(
        "--fw",
        type=Path,
        default=root / "app" / "build" / "app.bin",
        help="APP 固件路径（默认 app/build/app.bin）",
    )
    p.add_argument(
        "--web-dir",
        type=Path,
        default=root / "web",
        help="网页资源目录（默认 web/）",
    )
    p.add_argument(
        "--web-bin",
        type=Path,
        default=None,
        help="直接使用已有 web.bin（跳过打包）；默认从 --web-dir 重新打包",
    )
    p.add_argument(
        "--out-dir",
        type=Path,
        default=root,
        help="输出目录（默认仓库根目录）",
    )
    p.add_argument(
        "--name",
        default="upg.bin",
        help="组合固件文件名（默认 upg.bin）",
    )
    p.add_argument(
        "--skip-web-pack",
        action="store_true",
        help="不重新打包，使用 out-dir/web.bin 或 --web-bin",
    )
    return p.parse_args()


def main() -> int:
    args = parse_args()
    out_dir: Path = args.out_dir
    out_dir.mkdir(parents=True, exist_ok=True)

    fw_path: Path = args.fw
    if not fw_path.is_file():
        print(f"错误: 找不到 APP 固件: {fw_path}", file=sys.stderr)
        print("  请先编译 APP，或用 --fw 指定路径", file=sys.stderr)
        return 1

    fw_data = fw_path.read_bytes()
    print(f"[+] fw: {fw_path} ({len(fw_data)} bytes)")

    web_out = out_dir / "web.bin"
    if args.web_bin is not None:
        src = args.web_bin
        if not src.is_file():
            print(f"错误: --web-bin 不存在: {src}", file=sys.stderr)
            return 1
        web_data = src.read_bytes()
        print(f"[+] 使用已有 web.bin: {src} ({len(web_data)} bytes)")
        if src.resolve() != web_out.resolve():
            web_out.write_bytes(web_data)
    elif args.skip_web_pack and web_out.is_file():
        web_data = web_out.read_bytes()
        print(f"[+] 跳过打包，使用 {web_out} ({len(web_data)} bytes)")
    else:
        web_dir: Path = args.web_dir
        if not web_dir.is_dir():
            print(f"错误: web 目录不存在: {web_dir}", file=sys.stderr)
            return 1
        web_data = pack_web_bin(web_dir)
        web_out.write_bytes(web_data)
        print(f"[+] 写入 {web_out}")

    header, upg = build_upg(fw_data, web_data)
    magic, fw_len, web_len, crc_val = struct.unpack("<IIII", header)

    header_file = out_dir / "header.bin"
    header_file.write_bytes(header)
    print(
        f"[+] {header_file.name}: magic=0x{magic:08X} fw_len={fw_len} "
        f"web_len={web_len} crc=0x{crc_val:08X}"
    )

    upg_file = out_dir / args.name
    upg_file.write_bytes(upg)
    print(
        f"[+] 生成 {upg_file} "
        f"({len(header)}+{fw_len}+{web_len}={len(upg)} bytes)"
    )
    print("    格式: [upg_header 16B][APP][web.bin]  →  HTTP OTA / fw_upgrade 可用")
    return 0


if __name__ == "__main__":
    sys.exit(main())
