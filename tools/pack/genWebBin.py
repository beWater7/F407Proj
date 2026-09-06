import struct
from pathlib import Path

# 文件映射表：索引即资源ID
"""
┌────────────────────────────┐
│ uint32 file_count          │
├────────────────────────────┤
│ 文件1: type(uint32)        │
│ 文件1: size(uint32)        │
│ 文件1: offset(uint32)      │
│ ... N个文件条目            │
├───────────────────────────┤
│ 文件1数据 (size字节)       │
│ 文件2数据 (size字节)       │
│ ...                        │
└────────────────────────────┘
"""

# ============ 配置区域 ============
SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parents[1]
WEB_DIR = REPO_ROOT / "web"
OUTPUT_FILE = REPO_ROOT / "web.bin"


# 文件类型映射表（你可以随意扩展）
TYPE_MAP = {
    ".html": 0,
    ".css":  1,
    ".js":   2,
    ".png":  3,
    ".jpg":  4,
    ".ico":  5,
    ".gif":  6,
    ".gz":   7,
}
# =================================


# 顺序列表：html -> gz -> css -> js -> 图片
EXT_ORDER = [".html", ".ico", ".gz", ".css", ".js", ".png", ".jpg", ".jpeg"]

# =====================================================

def get_file_type(path: Path) -> int:
    return TYPE_MAP.get(path.suffix.lower(), 0xFFFFFFFF)

def pack_web_resources():
    if not WEB_DIR.exists():
        raise FileNotFoundError(f"❌ 资源目录不存在: {WEB_DIR}")

    # 收集文件，按 EXT_ORDER 排序
    files = []
    for ext in EXT_ORDER:
        for path in sorted(WEB_DIR.glob(f"*{ext}")):
            ftype = get_file_type(path)
            size = path.stat().st_size
            files.append((path, ftype, size))

    if not files:
        raise RuntimeError("❌ 没有找到要打包的资源文件！")

    file_count = len(files)

    # header 大小 = 文件数量(4B) + 每个条目(12B)
    header_size = 4 + file_count * 12

    # 计算每个文件 offset
    entries = []
    current_offset = header_size
    for path, ftype, size in files:
        entries.append((ftype, size, current_offset))
        current_offset += size

    # =====================================================
    # 写入 web.bin
    # =====================================================
    with open(OUTPUT_FILE, "wb") as f:
        # 文件数量
        f.write(struct.pack("<I", file_count))
        # 每个条目
        for ftype, size, offset in entries:
            f.write(struct.pack("<III", ftype, size, offset))
        # 写入文件内容
        for path, _, _ in files:
            f.write(path.read_bytes())

    # =====================================================
    # 打印信息
    # =====================================================
    print(f"\n 打包完成: {OUTPUT_FILE}")
    print(f"文件数量: {file_count}")
    print(f"Header 大小: {header_size} 字节\n")
    for i, (path, ftype, size) in enumerate(files):
        print(f"  [{i}] type={ftype:<10} size={size:<8} offset={entries[i][2]:<8} {path.name}")
    print(f"\n总大小: {OUTPUT_FILE.stat().st_size} 字节\n")

if __name__ == "__main__":
    pack_web_resources()


