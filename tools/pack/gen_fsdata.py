#!/usr/bin/env python3
"""把 web/rom 打成 lwIP httpd 的 fsdata.c（可读 C 字符串，非 hex dump）。

布局与 makefsdata 一致：
  [4 字节对齐的文件名\\0][HTTP 头][正文]
fs.c #include 这份文件，SPI web 无效时回落到 ROM。
"""
from __future__ import annotations

import argparse
from pathlib import Path

MIME = {
    ".html": "text/html; charset=utf-8",
    ".shtml": "text/html; charset=utf-8",
    ".htm": "text/html; charset=utf-8",
    ".css": "text/css",
    ".js": "application/javascript",
    ".txt": "text/plain; charset=utf-8",
}

SERVER = "lwIP/2.1.2"


def name_prefix(uri: str) -> bytes:
    raw = uri.encode("ascii") + b"\0"
    while len(raw) % 4:
        raw += b"\0"
    return raw


def http_header(status: str, content_type: str, body_len: int) -> bytes:
    return (
        f"HTTP/1.0 {status}\r\n"
        f"Server: {SERVER}\r\n"
        f"Content-Type: {content_type}\r\n"
        f"Content-Length: {body_len}\r\n"
        "Connection: close\r\n"
        "\r\n"
    ).encode("ascii")


def c_string(data: bytes) -> str:
    """Adjacent C string literals; compiler adds a trailing NUL (stripped via sizeof-1)."""
    chunks: list[str] = []
    line = bytearray()
    for b in data:
        line.append(b)
        if b == 0x0A or len(line) >= 80:
            chunks.append(_esc(bytes(line)))
            line.clear()
    if line:
        chunks.append(_esc(bytes(line)))
    return "\n".join(f'  "{c}"' for c in chunks)


def _esc(buf: bytes) -> str:
    out = []
    for b in buf:
        if b == 0:
            out.append("\\0")
        elif b == 0x0D:
            out.append("\\r")
        elif b == 0x0A:
            out.append("\\n")
        elif b == 0x09:
            out.append("\\t")
        elif b == ord("\\"):
            out.append("\\\\")
        elif b == ord('"'):
            out.append('\\"')
        elif 0x20 <= b < 0x7F:
            out.append(chr(b))
        else:
            # 八进制固定 3 位，避免 \\x 吞掉后续 hex 字母
            out.append(f"\\{b:03o}")
    return "".join(out)


def ident(uri: str) -> str:
    return uri.strip("/").replace(".", "_").replace("-", "_").replace("/", "_") or "root"


def main() -> int:
    p = argparse.ArgumentParser(description="Generate lwIP fsdata.c from web/rom")
    repo = Path(__file__).resolve().parents[2]
    p.add_argument("--rom-dir", type=Path, default=repo / "web" / "rom")
    p.add_argument(
        "--out",
        type=Path,
        default=repo / "app" / "User" / "app" / "http" / "fsdata.c",
    )
    args = p.parse_args()

    index_html = (args.rom_dir / "index.html").read_bytes()
    page_404 = (args.rom_dir / "404.html").read_bytes()

    files: list[tuple[str, str, bytes, str]] = [
        ("/index.shtml", "200 OK", index_html, MIME[".html"]),
        ("/index.html", "200 OK", index_html, MIME[".html"]),
        ("/404.html", "404 File not found", page_404, MIME[".html"]),
    ]

    blobs: list[tuple[str, str, int, bytes]] = []
    for uri, status, body, ctype in files:
        prefix = name_prefix(uri)
        hdr = http_header(status, ctype, len(body))
        blobs.append((ident(uri), uri, len(prefix), prefix + hdr + body))

    lines: list[str] = [
        "/*",
        " * 由 tools/pack/gen_fsdata.py 从 web/rom 的 HTML 生成，勿手改。",
        " * SPI PART_WEB 无效时，httpd 回落到这里（片内 .rodata，零拷贝）。",
        " */",
        '#include "lwip/apps/fs.h"',
        '#include "lwip/def.h"',
        "",
        "#define file_NULL ((struct fsdata_file *)0)",
        "",
        "#ifndef FS_FILE_FLAGS_HEADER_INCLUDED",
        "#define FS_FILE_FLAGS_HEADER_INCLUDED 1",
        "#endif",
        "#ifndef FS_FILE_FLAGS_HEADER_PERSISTENT",
        "#define FS_FILE_FLAGS_HEADER_PERSISTENT 0",
        "#endif",
        "",
    ]

    for ident_, uri, name_len, blob in blobs:
        lines.append(f"#define FSDATA_NAME_LEN_{ident_} {name_len}")
        lines.append(f"/* {uri}  name_len={name_len} payload={len(blob) - name_len} */")
        lines.append(f"static const char data__{ident_}[] =")
        lines.append(c_string(blob) + ";")
        lines.append("")

    n = len(blobs)
    # 倒序定义，next 才能指向已声明的结构体（与 makefsdata 相同）
    for i in range(n - 1, -1, -1):
        ident_, uri, name_len, blob = blobs[i]
        nxt = "file_NULL" if i == n - 1 else f"file__{blobs[i + 1][0]}"
        lines.append(f"const struct fsdata_file file__{ident_}[] = {{{{")
        lines.append(f"  {nxt},")
        lines.append(f"  (const unsigned char *)data__{ident_},")
        lines.append(f"  (const unsigned char *)data__{ident_} + FSDATA_NAME_LEN_{ident_},")
        lines.append(
            f"  (int)(sizeof(data__{ident_}) - 1 - FSDATA_NAME_LEN_{ident_}),"
        )
        lines.append(
            "  FS_FILE_FLAGS_HEADER_INCLUDED | FS_FILE_FLAGS_HEADER_PERSISTENT,"
        )
        lines.append("}};")
        lines.append("")

    root = blobs[0][0]
    lines.append(f"#define FS_ROOT file__{root}")
    lines.append(f"#define FS_NUMFILES {n}")
    lines.append("")

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text("\n".join(lines), encoding="utf-8")
    print(f"[+] {args.out}  {n} files, {sum(len(b[3]) for b in blobs)} bytes payload")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
