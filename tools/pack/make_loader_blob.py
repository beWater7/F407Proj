#!/usr/bin/env python3
"""make_loader_blob.py — 从 loader.bin 生成 [loader_header_t][loader.bin]。

供 boot UART XMODEM 兜底使用：:
    python3 tools/pack/make_loader_blob.py loader/build/loader.bin loader.blob
    python3 tools/serial/xmodem_send.py /dev/ttyUSB0 loader.blob
"""

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from genSpiImage import build_loader_blob, LOADER_VERSION  # noqa: E402


def main() -> int:
    ap = argparse.ArgumentParser(description="生成 loader blob（XMODEM/离线烧录用）")
    ap.add_argument("loader", type=Path, help="loader.bin (SRAM 变体)")
    ap.add_argument("-o", "--out", type=Path, default=None,
                    help="输出 blob 路径（默认 loader.blob）")
    ap.add_argument("--version", type=lambda s: int(s, 0), default=LOADER_VERSION)
    args = ap.parse_args()

    if not args.loader.is_file():
        raise SystemExit(f"loader not found: {args.loader}")
    blob = build_loader_blob(args.loader.read_bytes(), args.version)
    out = args.out or (args.loader.parent / "loader.blob")
    out.write_bytes(blob)
    print(f"[+] {out} ({len(blob)}B)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
