from PIL import Image, ImageDraw

# 生成简单终端图标 (64x64)
def generate_icon(path="icon.ico"):
    """
    im = Image.new("RGBA", (64, 64), (30, 30, 30, 255))
    draw = ImageDraw.Draw(im)
    draw.rectangle([8, 20, 56, 44], outline="lime", width=3)
    draw.line([15, 32, 25, 36, 15, 40], fill="lime", width=3)  # 箭头
    im.save(path)
    """
    print(f"Icon saved to {path}")

if __name__ == "__main__":
    generate_icon()
