import os
import shutil
from pathlib import Path
#from PIL import Image

# 文件列表（顺序就是索引）
resources = [
    ("index.html", 0),      # 0=html
    ("style.css", 1),       # 1=css
    ("script.js", 2),       # 2=js
    ("esp8266.jpg", 3),     # 3=jpg
]

class WebBinPacker:
    def __init__(self, output_file="web.bin"):
        self.output_file = Path(output_file)
        self.files = []

    def add_file(self, path, compress_jpg=False, target_size_kb=None):
        """添加文件到打包列表
        compress_jpg: 是否压缩 JPG
        target_size_kb: 压缩目标大小 KB
        """
        path = Path(path)
        if not path.exists():
            raise FileNotFoundError(f"{path} 不存在")
        self.files.append((path, compress_jpg, target_size_kb))

    """
    def compress_jpg(self, path, target_size_kb):
        #压缩 JPG 到目标大小
        img = Image.open(path)
        quality = 85
        step = 5
        temp_path = path.parent / f"{path.stem}_small.jpg"
        while True:
            img.save(temp_path, "JPEG", quality=quality)
            size_kb = temp_path.stat().st_size / 1024
            if size_kb <= target_size_kb or quality <= step:
                break
            quality -= step
        return temp_path
    """

    def pack(self):
        with open(self.output_file, "wb") as f_out:
            for path, compress_jpg, target_size_kb in self.files:
                """
                if compress_jpg and path.suffix.lower() in [".jpg", ".jpeg"]:
                    path_to_write = self.compress_jpg(path, target_size_kb or 20)
                else:
                    path_to_write = path
                """
                path_to_write = path
                data = path_to_write.read_bytes()
                name_bytes = path.name.encode("utf-8")
                name_len = len(name_bytes)
                size = len(data)
                # 写入：文件名长度(1B) + 文件名 + 文件大小(4B) + 文件内容
                f_out.write(bytes([name_len]))
                f_out.write(name_bytes)
                f_out.write(size.to_bytes(4, "big"))
                f_out.write(data)
        print(f"[+] 打包完成: {self.output_file} ({self.output_file.stat().st_size} bytes)")

# ---------------------------
# 使用示例
# ---------------------------
if __name__ == "__main__":
    script_dir = Path(__file__).parent

    packer = WebBinPacker(script_dir / "web.bin")
    packer.add_file(script_dir / "index.html")
    packer.add_file(script_dir / "style.css")
    packer.add_file(script_dir / "main.js")
    packer.add_file(script_dir / "esp8266.jpg")
    #packer.add_file(script_dir / "esp8266.jpg", compress_jpg=True, target_size_kb=15)

    packer.pack()

    # 文件路径（假设文件在当前目录）
    src_file = script_dir / "web.bin"

    entries = []
    data_blocks = []
    offset = 0

    for fname, ftype in resources:
        path = script_dir / fname
        data = path.read_bytes()
        entries.append((ftype, len(data), offset))
        data_blocks.append(data)
        offset += len(data)

    # 构建 header
    header = struct.pack("<I", len(entries))  # 文件数量
    for ftype, size, off in entries:
        header += struct.pack("<BII", ftype, size, off)

    # 写入 bin
    with open(src_file, "wb") as f:
        f.write(header)
        for data in data_blocks:
            f.write(data)



    # 目标路径：上一级目录
    dst_dir=os.path.join(os.path.dirname(src_file), os.pardir)

    # 拷贝文件
    shutil.copy(src_file, dst_dir)

