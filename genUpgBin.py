#!/usr/bin/env python3
"""
genUpgBin.py — 生成 OTA 组合固件 upg.bin

单槽布局（magic=0x55475021）::

    [upg_header 16B][APP][web.bin]

双槽布局（magic=0x55475022，默认）::

    [upg_header_dual 20B][APP1@0x08008000][APP2@0x08060000][web.bin]

web.bin 布局（与 fs.c parse_web_bin / genWebBin.py 一致）::

    uint32 file_count
    [type, size, offset] × N   （各 uint32 LE，共 12B）
    文件数据区…

资源顺序必须与 fs.c 中 gs_webFileDesc[] 一致（按索引匹配）。
"""

from __future__ import annotations

import argparse
import gzip
import io
import struct
import sys
import zlib
from pathlib import Path

# 与 upgrade.h: UPG_HDR_MAGIC / UPG_HDR_MAGIC_DUAL / WEB_FILE_TYPE 保持一致
UPG_HDR_MAGIC = 0x55475021  # "UGP!"
UPG_HDR_MAGIC_DUAL = 0x55475022

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


def gzip_bytes(raw: bytes) -> bytes:
    buf = io.BytesIO()
    with gzip.GzipFile(fileobj=buf, mode="wb", mtime=0, compresslevel=9) as zf:
        zf.write(raw)
    return buf.getvalue()


def resolve_file(web_dir: Path, candidates: list[str]) -> Path:
    gz_path = None
    raw_path = None
    for name in candidates:
        p = web_dir / name
        if not p.is_file():
            continue
        if name.endswith(".gz"):
            if gz_path is None:
                gz_path = p
        elif raw_path is None:
            raw_path = p
    if raw_path is not None and gz_path is not None:
        if raw_path.stat().st_mtime >= gz_path.stat().st_mtime:
            gz_path.write_bytes(gzip_bytes(raw_path.read_bytes()))
            print(f"[+] refresh gzip {gz_path.name} <- {raw_path.name}")
        return gz_path
    if gz_path is not None:
        return gz_path
    if raw_path is not None:
        return raw_path
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


def build_upg_single(fw_data: bytes, web_data: bytes) -> tuple[bytes, bytes]:
    """返回 (header, upg_blob) — 单槽。"""
    fw_len = len(fw_data)
    web_len = len(web_data)
    crc_val = zlib.crc32(fw_data + web_data) & 0xFFFFFFFF
    header = struct.pack("<IIII", UPG_HDR_MAGIC, fw_len, web_len, crc_val)
    return header, header + fw_data + web_data


def build_upg_dual(fw1: bytes, fw2: bytes, web_data: bytes) -> tuple[bytes, bytes]:
    """返回 (header, upg_blob) — 双槽。"""
    fw1_len = len(fw1)
    fw2_len = len(fw2)
    web_len = len(web_data)
    crc_val = zlib.crc32(fw1 + fw2 + web_data) & 0xFFFFFFFF
    header = struct.pack(
        "<IIIII", UPG_HDR_MAGIC_DUAL, fw1_len, fw2_len, web_len, crc_val
    )
    return header, header + fw1 + fw2 + web_data


def parse_args() -> argparse.Namespace:
    root = Path(__file__).resolve().parent
    p = argparse.ArgumentParser(
        description="生成 OTA 组合固件 upg.bin（单槽或无感双槽）"
    )
    p.add_argument(
        "--fw",
        type=Path,
        default=None,
        help="单槽 APP 固件（与 --fw1/--fw2 互斥）",
    )
    p.add_argument(
        "--fw1",
        type=Path,
        default=root / "app" / "build" / "app1.bin",
        help="APP1 镜像（ORIGIN=0x08008000，默认 app/build/app1.bin）",
    )
    p.add_argument(
        "--fw2",
        type=Path,
        default=root / "app" / "build" / "app2.bin",
        help="APP2 镜像（ORIGIN=0x08060000，默认 app/build/app2.bin）",
    )
    p.add_argument(
        "--single",
        action="store_true",
        help="强制单槽格式（需 --fw；默认双槽）",
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

    use_single = args.single or (args.fw is not None)
    if use_single:
        fw_path = args.fw if args.fw is not None else (out_dir / "app" / "build" / "app.bin")
        # 兼容：未传 --fw 时尝试旧路径
        if args.fw is None:
            cand = Path(__file__).resolve().parent / "app" / "build" / "app.bin"
            fw_path = cand
        if not fw_path.is_file():
            print(f"错误: 找不到 APP 固件: {fw_path}", file=sys.stderr)
            return 1
        fw_data = fw_path.read_bytes()
        print(f"[+] fw: {fw_path} ({len(fw_data)} bytes)")
        header, upg = build_upg_single(fw_data, web_data)
        magic, fw_len, web_len, crc_val = struct.unpack("<IIII", header)
        print(
            f"[+] header.bin: magic=0x{magic:08X} fw_len={fw_len} "
            f"web_len={web_len} crc=0x{crc_val:08X}"
        )
        fmt_note = f"[{len(header)}B hdr][APP][web] = {len(upg)} bytes"
    else:
        fw1_path: Path = args.fw1
        fw2_path: Path = args.fw2
        if not fw1_path.is_file():
            print(f"错误: 找不到 APP1: {fw1_path}", file=sys.stderr)
            print("  请先 make（生成 app1.bin / app2.bin）", file=sys.stderr)
            return 1
        if not fw2_path.is_file():
            print(f"错误: 找不到 APP2: {fw2_path}", file=sys.stderr)
            return 1
        fw1 = fw1_path.read_bytes()
        fw2 = fw2_path.read_bytes()
        print(f"[+] fw1: {fw1_path} ({len(fw1)} bytes) @ 0x08008000")
        print(f"[+] fw2: {fw2_path} ({len(fw2)} bytes) @ 0x08060000")
        header, upg = build_upg_dual(fw1, fw2, web_data)
        magic, fw1_len, fw2_len, web_len, crc_val = struct.unpack("<IIIII", header)
        print(
            f"[+] header.bin: magic=0x{magic:08X} fw1={fw1_len} fw2={fw2_len} "
            f"web={web_len} crc=0x{crc_val:08X}"
        )
        fmt_note = (
            f"[{len(header)}B hdr][APP1][APP2][web] = {len(upg)} bytes "
            "(seamless A/B)"
        )

    header_file = out_dir / "header.bin"
    header_file.write_bytes(header)
    print(f"[+] 写入 {header_file}")

    upg_file = out_dir / args.name
    upg_file.write_bytes(upg)
    print(f"[+] 生成 {upg_file} {fmt_note}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
