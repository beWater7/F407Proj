
import serial
import time
import re
import sys

from colorama import init, Fore, Style

init(autoreset=True)

def colorize(line):
    """给串口输出加彩色"""
    # 你可以自定义关键词匹配
    if "ERROR" in line:
        return Fore.RED + line + Style.RESET_ALL
    elif "WARN" in line or "WARNING" in line:
        return Fore.YELLOW + line + Style.RESET_ALL
    elif re.search(r'info', line, re.I):
        return Fore.GREEN + line + Style.RESET_ALL
    else:
        return line

def main():
    port = "COM3"  # 改成你的串口
    baud = 115200  # 改成你的波特率

    try:
        ser = serial.Serial(port, baud, timeout=0.5)
        print(f"Connected to {port} at {baud} baud.")
    except Exception as e:
        print(f"Failed to open port: {e}")
        sys.exit(1)

    # 可选：模拟野火助手的一些初始化
    ser.send_break()
    time.sleep(0.1)
    ser.setDTR(False)
    ser.setRTS(False)
    time.sleep(0.1)
    #ser.setDTR(True)
    #ser.setRTS(True)

    try:
        while True:
            line = ser.readline().decode(errors="ignore").strip()
            if line:
                print(colorize(line))
    except KeyboardInterrupt:
        print("\nBye!")
    finally:
        ser.close()

if __name__ == '__main__':
    main()
