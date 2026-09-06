#!/usr/bin/env python3
"""
genUpgBin.py — 生成 OTA 组合固件

现行布局（magic=0x55475023）::

    [upg_header_ldr 24B][loader_blob][APP1][APP2][web.bin?]

默认不含 web，输出 upg.bin；`+web` / `--with-web` 输出 upg_web.bin。

旧包 magic 0x55475021 / 0x55475022 仍可被 APP HTTP OTA 识别。
"""

from __future__ import annotations

import argparse
import gzip
import io
import struct
import sys
import zlib
from pathlib import Path

# 与 upgrade.h: UPG_HDR_MAGIC / UPG_HDR_MAGIC_DUAL / UPG_HDR_MAGIC_LDR 保持一致
UPG_HDR_MAGIC = 0x55475021  # "UGP!" 旧单槽
UPG_HDR_MAGIC_DUAL = 0x55475022  # 旧双槽
UPG_HDR_MAGIC_LDR = 0x55475023  # 现行：loader + APP1 + APP2 + 可选 web

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


def build_upg_ldr(
    loader_blob: bytes, fw1: bytes, fw2: bytes, web_data: bytes
) -> tuple[bytes, bytes]:
    """现行包：[24B hdr][loader_blob][APP1][APP2][web?]。"""
    payload = loader_blob + fw1 + fw2 + web_data
    crc_val = zlib.crc32(payload) & 0xFFFFFFFF
    header = struct.pack(
        "<IIIIII",
        UPG_HDR_MAGIC_LDR,
        len(loader_blob),
        len(fw1),
        len(fw2),
        len(web_data),
        crc_val,
    )
    return header, header + payload


def repo_root() -> Path:
    # tools/pack/genUpgBin.py -> 仓库根
    return Path(__file__).resolve().parents[2]


def parse_args() -> argparse.Namespace:
    root = repo_root()
    # 允许 `python3 genUpgBin.py +web`，与 `make +web` 对齐
    argv = [a for a in sys.argv[1:] if a != "+web"]
    with_web_token = "+web" in sys.argv[1:]

    p = argparse.ArgumentParser(
        description="生成升级包：默认 upg.bin（loader+APP1+APP2）；+web / --with-web 生成 upg_web.bin"
    )
    p.add_argument(
        "--loader",
        type=Path,
        default=root / "loader" / "build" / "loader.bin",
        help="SRAM 变体 loader.bin（默认 loader/build/loader.bin）",
    )
    p.add_argument(
        "--fw1",
        type=Path,
        default=root / "app" / "build" / "app1.bin",
        help="APP1 镜像（ORIGIN=0x08008000）",
    )
    p.add_argument(
        "--fw2",
        type=Path,
        default=root / "app" / "build" / "app2.bin",
        help="APP2 镜像（ORIGIN=0x08060000）",
    )
    p.add_argument(
        "--with-web",
        action="store_true",
        help="打包 web 资源，输出 upg_web.bin",
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
        help="直接使用已有 web.bin（跳过打包）",
    )
    p.add_argument(
        "--out-dir",
        type=Path,
        default=root,
        help="输出目录（默认仓库根目录）",
    )
    p.add_argument(
        "--name",
        default=None,
        help="输出文件名（默认 upg.bin / 带 web 时 upg_web.bin）",
    )
    p.add_argument(
        "--skip-web-pack",
        action="store_true",
        help="不重新打包，使用 out-dir/web.bin 或 --web-bin",
    )
    args = p.parse_args(argv)
    if with_web_token:
        args.with_web = True
    return args


def main() -> int:
    args = parse_args()
    out_dir: Path = args.out_dir
    out_dir.mkdir(parents=True, exist_ok=True)

    from genSpiImage import build_loader_blob, LOADER_VERSION  # noqa: E402

    loader_path: Path = args.loader
    fw1_path: Path = args.fw1
    fw2_path: Path = args.fw2
    for pth, label in ((loader_path, "loader"), (fw1_path, "APP1"), (fw2_path, "APP2")):
        if not pth.is_file():
            print(f"错误: 找不到 {label}: {pth}", file=sys.stderr)
            print("  请先在仓库根执行 make（生成 loader.bin / app1.bin / app2.bin）", file=sys.stderr)
            return 1

    loader_bin = loader_path.read_bytes()
    fw1 = fw1_path.read_bytes()
    fw2 = fw2_path.read_bytes()
    loader_blob = build_loader_blob(loader_bin, LOADER_VERSION)
    print(f"[+] loader: {loader_path} ({len(loader_bin)} bytes) -> blob {len(loader_blob)}B")
    print(f"[+] fw1: {fw1_path} ({len(fw1)} bytes) @ 0x08008000")
    print(f"[+] fw2: {fw2_path} ({len(fw2)} bytes) @ 0x08060000")

    web_data = b""
    if args.with_web:
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

    header, upg = build_upg_ldr(loader_blob, fw1, fw2, web_data)
    magic, loader_len, fw1_len, fw2_len, web_len, crc_val = struct.unpack(
        "<IIIIII", header
    )
    print(
        f"[+] header: magic=0x{magic:08X} loader={loader_len} fw1={fw1_len} "
        f"fw2={fw2_len} web={web_len} crc=0x{crc_val:08X}"
    )

    header_file = out_dir / "header.bin"
    header_file.write_bytes(header)
    print(f"[+] 写入 {header_file}")

    default_name = "upg_web.bin" if args.with_web else "upg.bin"
    upg_file = out_dir / (args.name if args.name else default_name)
    upg_file.write_bytes(upg)
    web_note = "+web" if web_len else "no web"
    print(
        f"[+] 生成 {upg_file} [{len(header)}B hdr][loader][APP1][APP2]"
        f"{'[web]' if web_len else ''} = {len(upg)} bytes ({web_note})"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
