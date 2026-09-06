#!/usr/bin/env python3
"""
gen_dtb.py — 将板级 dts 源文件编译为自描述二进制 dtb + 固件内置 C 数组

产物:
  --bin-out xxx.dtb   二进制, 烧写到 SPI flash dtb 保留区(动态加载用)
  --c-out  xxx.c      内置兜底 C 数组(编译进固件, dtb 缺失/损坏时回落)

二进制布局(与 platform/hal/dts.h 中 dts_header_t / dts_node_t / dts_prop_t 一一对应):

  [dts_header_t 32B][nodes[] 16B/个][props[] 8B/个][strtab]

校验:
  hdr_crc  = CRC16/CCITT-FALSE(blob[0:28])   —— 头部可靠性
  body_crc = zlib.crc32(blob[32:])           —— 与固件 common/crc.c crc32_checksum 一致

支持的 dts 语法子集:
  /dts-v1/;
  #include "soc.dtsi"          (文本展开, 相对当前文件)
  / { prop = value; node { ... }; };
  prop = "string" | <num> | <num num>;
  node-name@addr { ... };      (@addr 仅文档用途, 不参与名称匹配)

扩展新板子: 新建 platform/boards/<board>/<board>.dts, 即可复用同一套驱动。
"""

from __future__ import annotations

import argparse
import re
import struct
import sys
import zlib
from pathlib import Path

DTS_MAGIC = 0xD45B0001
DTS_VERSION = 1

HEADER_SIZE = 32      # dts_header_t
NODE_SIZE = 16        # dts_node_t
PROP_SIZE = 8         # dts_prop_t

PROP_EMPTY = 0
PROP_U32 = 1
PROP_STR = 2


# ---------------------------------------------------------------- CRC16/CCITT-FALSE
def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= (b << 8) & 0xFFFF
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if (crc & 0x8000) else (crc << 1) & 0xFFFF
    return crc & 0xFFFF


# ---------------------------------------------------------------- 预处理: include 展开
def preprocess(path: Path, seen: set | None = None) -> str:
    seen = seen or set()
    text = path.read_text(encoding="utf-8", errors="replace")
    out: list[str] = []
    for line in text.splitlines():
        m = re.match(r'\s*#include\s*"([^"]+)"', line)
        if m:
            inc = (path.parent / m.group(1)).resolve()
            if inc in seen:
                raise SystemExit(f"gen_dtb: 循环 include: {inc}")
            seen.add(inc)
            out.append(preprocess(inc, seen))
            seen.discard(inc)
        else:
            out.append(line)
    return "\n".join(out)


# ---------------------------------------------------------------- 词法
_TOKEN_RE = re.compile(
    r"""
      (?P<WS>\s+)
    | (?P<COMMENT>/\*[\s\S]*?\*/|//[^\n]*)
    | (?P<STR>"(?:[^"\\]|\\.)*")
    | (?P<SYM>;|\{|\}|=|<|>|/|@)
    | (?P<HEX>0[xX][0-9a-fA-F]+)
    | (?P<DEC>[0-9]+)
    | (?P<IDENT>[A-Za-z_][A-Za-z0-9_.\-]*)
    """,
    re.VERBOSE,
)


class Token:
    __slots__ = ("kind", "val")

    def __init__(self, kind: str, val: object):
        self.kind = kind
        self.val = val


def tokenize(text: str) -> list[Token]:
    toks: list[Token] = []
    for m in _TOKEN_RE.finditer(text):
        k = m.lastgroup
        v = m.group()
        if k in ("WS", "COMMENT"):
            continue
        if k == "STR":
            toks.append(Token("STR", v[1:-1]))
        elif k == "HEX":
            toks.append(Token("NUM", int(v, 16)))
        elif k == "DEC":
            toks.append(Token("NUM", int(v)))
        elif k == "IDENT":
            toks.append(Token("IDENT", v))
        else:
            toks.append(Token("SYM", v))
    return toks


# ---------------------------------------------------------------- 语法
class Prop:
    __slots__ = ("key", "value")  # value: None(flag) | str | list[int]

    def __init__(self, key: str, value: object = None):
        self.key = key
        self.value = value


class Node:
    __slots__ = ("name", "props", "children")

    def __init__(self, name: str):
        self.name = name
        self.props: list[Prop] = []
        self.children: list[Node] = []


class Parser:
    def __init__(self, toks: list[Token], src: str):
        self.toks = toks
        self.i = 0
        self.src = src

    def peek(self, off: int = 0) -> Token | None:
        j = self.i + off
        return self.toks[j] if j < len(self.toks) else None

    def next(self) -> Token:
        t = self.toks[self.i]
        self.i += 1
        return t

    def expect_sym(self, s: str) -> None:
        t = self.next()
        if t.kind != "SYM" or t.val != s:
            self.error(f"期望 '{s}', 得到 '{t.val}'")

    def error(self, msg: str) -> None:
        raise SystemExit(f"gen_dtb: {self.src}: {msg}")

    def parse(self) -> Node:
        root: Node | None = None
        while self.peek() is not None:
            t = self.peek()
            if t.kind == "SYM" and t.val == "/":
                nxt = self.peek(1)
                if nxt is not None and nxt.kind == "IDENT" and nxt.val == "dts-v1":
                    self.next()
                    self.next()
                    self.expect_sym("/")
                    self.expect_sym(";")
                    continue
                root = self.parse_root()
            else:
                self.error(f"顶层非法 token: {t.val}")
        if root is None:
            self.error("缺少根节点 '/ { }'")
        return root

    def parse_root(self) -> Node:
        self.next()  # '/'
        self.expect_sym("{")
        node = Node("")
        self.parse_members(node)
        self.expect_sym("}")
        self.expect_sym(";")
        return node

    def parse_members(self, parent: Node) -> None:
        while True:
            t = self.peek()
            if t is None:
                self.error("未闭合的 '{'")
            if t.kind == "SYM" and t.val == "}":
                return
            if t.kind == "IDENT":
                self.parse_member(parent)
            else:
                self.error(f"非法成员: {t.val}")

    def parse_member(self, parent: Node) -> None:
        t = self.next()
        name = t.val
        nxt = self.peek()
        if nxt is not None and nxt.kind == "SYM" and nxt.val == "@":
            self.next()
            addr = self.next()
            if addr.kind not in ("NUM", "IDENT"):
                self.error(f"节点 '{name}' 地址非法: {addr.val}")
            name = f"{name}@{addr.val}"
        if self.peek() is not None and self.peek().kind == "SYM" and self.peek().val == "{":
            self.next()  # '{'
            node = Node(name)
            self.parse_members(node)
            self.expect_sym("}")
            if self.peek() is not None and self.peek().kind == "SYM" and self.peek().val == ";":
                self.next()
            parent.children.append(node)
            return
        prop = Prop(name)
        if self.peek() is not None and self.peek().kind == "SYM" and self.peek().val == "=":
            self.next()
            prop.value = self.parse_value()
            self.expect_sym(";")
        elif self.peek() is not None and self.peek().kind == "SYM" and self.peek().val == ";":
            self.next()
        else:
            self.error(f"属性 '{name}' 缺少 '=' 或 ';'")
        parent.props.append(prop)

    def parse_value(self) -> object:
        t = self.next()
        if t.kind == "STR":
            return t.val
        if t.kind == "SYM" and t.val == "<":
            cells: list[int] = []
            while True:
                nxt = self.peek()
                if nxt is None:
                    self.error("未闭合的 '<'")
                if nxt.kind == "SYM" and nxt.val == ">":
                    self.next()
                    break
                if nxt.kind == "NUM":
                    cells.append(int(self.next().val))
                else:
                    self.error(f"cell 内非法 token: {nxt.val}")
            return cells
        self.error(f"值非法: {t.val}")


# ---------------------------------------------------------------- 扁平化 + 编码
def flatten(root: Node) -> list[dict]:
    nodes: list[dict] = []

    def walk(n: Node, parent: int) -> None:
        idx = len(nodes)
        nodes.append({"name": n.name, "parent": parent, "props": list(n.props)})
        for c in n.children:
            walk(c, idx)

    walk(root, 0xFFFF)
    return nodes


def build_strtab(strings: set[str]) -> tuple[bytes, dict[str, int]]:
    tab = bytearray()
    offs: dict[str, int] = {}
    for s in sorted(strings):  # 排序保证输出确定性(避免 set 哈希随机化导致 dtb 不稳定)
        if s not in offs:
            offs[s] = len(tab)
            tab += s.encode("utf-8") + b"\x00"
    return bytes(tab), offs


def build_dtb(root: Node) -> bytes:
    nodes = flatten(root)
    node_cnt = len(nodes)

    strings: set[str] = set()
    for n in nodes:
        strings.add(n["name"])
        for p in n["props"]:
            strings.add(p.key)
            if isinstance(p.value, str):
                strings.add(p.value)
    strtab, offs = build_strtab(strings)

    props: list[tuple[int, int, int, int]] = []  # (key_off, type, len, val_off)
    for n in nodes:
        n["prop_start"] = len(props)
        n["prop_cnt"] = 0
        reg = None
        for p in n["props"]:
            key_off = offs[p.key]
            if p.key == "reg":
                if not isinstance(p.value, list) or len(p.value) != 2:
                    raise SystemExit(f"gen_dtb: 节点 '{n['name']}': reg 需要 <addr size> 两个 cell")
                reg = (int(p.value[0]) & 0xFFFFFFFF, int(p.value[1]) & 0xFFFFFFFF)
                continue
            if isinstance(p.value, str):
                props.append((key_off, PROP_STR, len(p.value), offs[p.value]))
            elif isinstance(p.value, list):
                if len(p.value) != 1:
                    raise SystemExit(f"gen_dtb: 节点 '{n['name']}' 属性 '{p.key}': 只支持单个 cell")
                props.append((key_off, PROP_U32, 4, int(p.value[0]) & 0xFFFFFFFF))
            else:
                props.append((key_off, PROP_EMPTY, 0, 0))
            n["prop_cnt"] += 1
        n["reg_addr"], n["reg_size"] = (reg if reg is not None else (0, 0))

    strtab_off = HEADER_SIZE + node_cnt * NODE_SIZE + len(props) * PROP_SIZE

    blob = bytearray(HEADER_SIZE)
    for n in nodes:
        blob += struct.pack(
            "<HHHHII",
            offs[n["name"]],
            n["parent"],
            n["prop_start"],
            n["prop_cnt"],
            n["reg_addr"],
            n["reg_size"],
        )
    for key_off, typ, ln, val in props:
        blob += struct.pack("<HBBI", key_off, typ, ln, val)
    blob += strtab

    body_len = len(blob)
    body_crc = zlib.crc32(blob[HEADER_SIZE:]) & 0xFFFFFFFF
    # 先写除 hdr_crc/reserved 外的头字段, 再对前 28B 计算头 CRC
    struct.pack_into(
        "<IIIIIIHHHH",
        blob, 0,
        DTS_MAGIC, DTS_VERSION, body_len, body_crc,
        strtab_off, len(strtab),
        node_cnt, len(props), 0, 0,
    )
    hdr_crc = crc16(bytes(blob[:28]))
    struct.pack_into("<H", blob, 28, hdr_crc)
    return bytes(blob)


# ---------------------------------------------------------------- C 数组导出
def gen_c(blob: bytes, c_out: Path, board: str) -> None:
    lines = [
        "/* 由 tools/pack/gen_dtb.py 自动生成 —— 请勿手改 */",
        f"/* {board} 板级设备树二进制, 固件内置兜底(SPI flash dtb 缺失/损坏时使用) */",
        "#include <stdint.h>",
        "",
        "const uint8_t dts_fallback_blob[] = {",
    ]
    for i in range(0, len(blob), 12):
        chunk = blob[i : i + 12]
        lines.append("    " + ", ".join(f"0x{b:02X}" for b in chunk) + ",")
    lines.append("};")
    lines.append("")
    lines.append(f"const uint32_t dts_fallback_blob_len = {len(blob)}u;")
    c_out.write_text("\n".join(lines) + "\n")


def main() -> int:
    p = argparse.ArgumentParser(description="编译板级 dts → dtb + 内置 C 数组")
    p.add_argument("dts", type=Path, help="dts 源文件 (platform/boards/<board>/<board>.dts)")
    p.add_argument("--bin-out", type=Path, default=None, help="输出 .dtb 二进制路径")
    p.add_argument("--c-out", type=Path, default=None, help="输出内置兜底 .c 路径")
    args = p.parse_args()

    dts_path: Path = args.dts
    if not dts_path.is_file():
        print(f"错误: dts 不存在: {dts_path}", file=sys.stderr)
        return 1

    text = preprocess(dts_path)
    toks = tokenize(text)
    root = Parser(toks, str(dts_path)).parse()
    blob = build_dtb(root)

    # 自检: 回读校验
    hdr = struct.unpack_from("<IIIIIIHHHH", blob, 0)
    if hdr[0] != DTS_MAGIC or hdr[1] != DTS_VERSION:
        print("自检失败: magic/version", file=sys.stderr)
        return 1
    if crc16(blob[:28]) != hdr[8] or zlib.crc32(blob[HEADER_SIZE:]) & 0xFFFFFFFF != hdr[3]:
        print("自检失败: crc 不匹配", file=sys.stderr)
        return 1

    board = dts_path.stem
    print(f"[+] dts  : {dts_path}")
    print(f"[+] dtb  : {len(blob)}B  nodes={hdr[6]} props={hdr[7]} strtab={hdr[5]}B")
    if args.bin_out is not None:
        args.bin_out.parent.mkdir(parents=True, exist_ok=True)
        args.bin_out.write_bytes(blob)
        print(f"[+] bin  : {args.bin_out} ({len(blob)}B)")
    if args.c_out is not None:
        args.c_out.parent.mkdir(parents=True, exist_ok=True)
        gen_c(blob, args.c_out, board)
        print(f"[+] c    : {args.c_out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
