import tkinter as tk
from tkinter import ttk, filedialog, scrolledtext, messagebox
import serial
import serial.tools.list_ports
import threading
import time
from datetime import datetime
import os
import re

# === ANSI 转义序列正则 ===
ANSI_RE = re.compile(r'\x1b\[([0-9;]+)m')


class SerialGUI:
    def __init__(self, master):
        self.master = master
        master.title("串口工具 - ANSI 彩色支持")

        self.serial_port = None
        self.running = False
        self.logfile = None
        self.logpath = tk.StringVar(value=os.getcwd())

        self.create_widgets()

    def create_widgets(self):
        frame = ttk.Frame(self.master)
        frame.pack(padx=10, pady=10, fill='x')

        ttk.Label(frame, text="串口:").grid(row=0, column=0)
        self.port_cb = ttk.Combobox(frame, values=self.get_ports(), width=10)
        self.port_cb.grid(row=0, column=1)
        self.refresh_btn = ttk.Button(frame, text="刷新", command=self.refresh_ports)
        self.refresh_btn.grid(row=0, column=2, padx=5)

        ttk.Label(frame, text="波特率:").grid(row=0, column=3)
        self.baud_cb = ttk.Combobox(frame, values=["9600", "115200", "921600"], width=10)
        self.baud_cb.set("115200")
        self.baud_cb.grid(row=0, column=4)

        ttk.Label(frame, text="日志路径:").grid(row=1, column=0)
        self.path_entry = ttk.Entry(frame, textvariable=self.logpath, width=40)
        self.path_entry.grid(row=1, column=1, columnspan=3, sticky='we')
        self.browse_btn = ttk.Button(frame, text="选择", command=self.browse_path)
        self.browse_btn.grid(row=1, column=4)

        self.start_btn = ttk.Button(frame, text="开始", command=self.start)
        self.start_btn.grid(row=2, column=1, pady=5)
        self.stop_btn = ttk.Button(frame, text="停止", command=self.stop, state="disabled")
        self.stop_btn.grid(row=2, column=2, pady=5)

        self.output = scrolledtext.ScrolledText(self.master, width=100, height=30)
        self.output.pack(padx=10, pady=5)

        # 预定义一些 ANSI tag 样式
        self.output.tag_config('reset', foreground='black')
        self.output.tag_config('red', foreground='red')
        self.output.tag_config('green', foreground='green')
        self.output.tag_config('yellow', foreground='orange')
        self.output.tag_config('blue', foreground='blue')
        self.output.tag_config('magenta', foreground='magenta')
        self.output.tag_config('cyan', foreground='cyan')
        self.output.tag_config('white', foreground='black')

        # 输入框
        self.input_entry = ttk.Entry(self.master)
        self.input_entry.pack(fill='x', padx=10)
        self.input_entry.bind("<Return>", self.send_command)

    def get_ports(self):
        ports = [port.device for port in serial.tools.list_ports.comports()]
        return ports

    def refresh_ports(self):
        self.port_cb['values'] = self.get_ports()

    def browse_path(self):
        path = filedialog.askdirectory()
        if path:
            self.logpath.set(path)

    def start(self):
        port = self.port_cb.get()
        baud = self.baud_cb.get()
        if not port or not baud:
            messagebox.showwarning("提示", "请先选择串口和波特率")
            return

        try:
            self.serial_port = serial.Serial(port, baudrate=int(baud), timeout=0.5)
        except Exception as e:
            messagebox.showerror("错误", f"打开串口失败: {e}")
            return

        # 野火初始化
        try:
            self.serial_port.send_break()
            time.sleep(0.1)
            self.serial_port.setDTR(False)
            self.serial_port.setRTS(False)
            time.sleep(0.1)
            self.serial_port.setDTR(True)
            self.serial_port.setRTS(True)
        except Exception as e:
            print(f"初始化失败: {e}")

        date_str = datetime.now().strftime("%Y%m%d_%H%M%S")
        clean_port = port.replace(":", "").replace("/", "").replace("\\", "")
        log_name = f"{clean_port}_{date_str}.log"
        log_full = os.path.join(self.logpath.get(), log_name)
        self.logfile = open(log_full, "a", encoding="utf-8")

        self.running = True
        self.start_btn.config(state="disabled")
        self.stop_btn.config(state="normal")

        self.thread = threading.Thread(target=self.read_serial)
        self.thread.daemon = True
        self.thread.start()

    def stop(self):
        self.running = False
        if self.serial_port and self.serial_port.is_open:
            self.serial_port.close()
        if self.logfile:
            self.logfile.close()
        self.start_btn.config(state="normal")
        self.stop_btn.config(state="disabled")

    def read_serial(self):
        while self.running:
            try:
                raw_line = self.serial_port.readline().decode(errors="ignore")
                if raw_line:
                    timestamp = datetime.now().strftime("[%Y-%m-%d %H:%M:%S] ")
                    self.insert_ansi(timestamp + raw_line)
                    # 保存纯文本
                    plain = ANSI_RE.sub('', raw_line)
                    self.logfile.write(timestamp + plain)
                    self.logfile.flush()
            except Exception as e:
                print(f"串口读错误: {e}")
                break

    def insert_ansi(self, text):
        """解析 ANSI 序列，按颜色插入"""
        pos = 0
        tag = 'reset'

        for m in ANSI_RE.finditer(text):
            start, end = m.span()
            self.output.insert(tk.END, text[pos:start], tag)
            code = m.group(1)
            tag = self.ansi_to_tag(code)
            pos = end

        self.output.insert(tk.END, text[pos:], tag)
        self.output.see(tk.END)

    def ansi_to_tag(self, code):
        # 简单映射：常见的颜色
        if code in ['0', '39']:
            return 'reset'
        elif code in ['1;31', '31']:
            return 'red'
        elif code in ['1;32', '32']:
            return 'green'
        elif code in ['1;33', '33']:
            return 'yellow'
        elif code in ['1;34', '34']:
            return 'blue'
        elif code in ['1;35', '35']:
            return 'magenta'
        elif code in ['1;36', '36']:
            return 'cyan'
        else:
            return 'reset'

    def send_command(self, event=None):
        if self.serial_port and self.serial_port.is_open:
            cmd = self.input_entry.get().strip()
            if cmd:
                self.serial_port.write((cmd + '\r\n').encode())
                self.input_entry.delete(0, tk.END)
                timestamp = datetime.now().strftime("[%Y-%m-%d %H:%M:%S] ")
                self.output.insert(tk.END, timestamp + f"[TX] {cmd}\n", 'blue')
                self.output.see(tk.END)


if __name__ == "__main__":
    root = tk.Tk()
    app = SerialGUI(root)
    root.mainloop()
