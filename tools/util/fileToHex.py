# 开发人员 :  liudayi
# 开发时间 :  2024/7/12 21:45
# 文件名称 :  file to hex.py /文件/字符串转16进制
# 开发工具 :  PyCharm

#文件转16进制数组
def file_to_hex_array(file_path):
    try:
        with open(file_path, 'rb') as file:
            # 读取文件内容
            file_content = file.read()

        # 将文件内容转换为十六进制表示的数组
        hex_array = [f'0x{byte:02x}' for byte in file_content]

        return hex_array
    except FileNotFoundError:
        print(f"文件 {file_path} 未找到。")
        return []
    except IOError:
        print(f"读取文件 {file_path} 时出错。")
        return []

#按16行打印16进制数组
def print_hex_array(hex_array, items_per_line=16):
    for i in range(0, len(hex_array), items_per_line):
        line = hex_array[i:i+items_per_line]
        print(', '.join(line) + (',' if len(line) == items_per_line else ''))

#字符串转16进制数组
def string_to_hex_array(__string):
    try:
        # 将字符串转换为字节对象
        byte_array = __string.encode('utf-8')
        # 将字节对象转换为十六进制表示的数组
        hex_array = [f'0x{byte:02x}' for byte in byte_array]
        return hex_array
    except Exception as e:
        print(f"转换时出错: {e}")
        return []

# 示例用法
if __name__ == "__main__":
    file_path = 'index_111.html'  # 替换为你的文件路径
    hex_array = file_to_hex_array(file_path)
    if hex_array:
        print("十六进制数组:")
        print_hex_array(hex_array)
    print("--------------------------------------------------------------------------")
    hex_array = string_to_hex_array("/STM32F4x7LED.html ")
    if hex_array:
        print("十六进制数组:")
        print_hex_array(hex_array)