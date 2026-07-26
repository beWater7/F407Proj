import tkinter as tk
from tkinter import ttk, filedialog, scrolledtext, messagebox
import serial
import serial.tools.list_ports
import threading
import time
from datetime import datetime
import os
import re

class SerialGUI:
    def __init__(self, master):
        self.master = master
        master.title("串口彩色日志工具 (支持高亮)")

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

        # 定义标签样式
        self.output.tag_config("ERROR", foreground="red")
        self.output.tag_config("WARN", foreground="orange")
        self.output.tag_config("INFO", foreground="green")
        self.output.tag_config("TX", foreground="blue")

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

        # ✅ 野火初始化
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
                line = self.serial_port.readline().decode(errors="ignore").strip()
                if line:
                    timestamp = datetime.now().strftime("[%Y-%m-%d %H:%M:%S] ")
                    self.insert_colored_line(timestamp + line)
                    self.logfile.write(timestamp + line + '\n')
                    self.logfile.flush()
            except Exception as e:
                print(f"串口读错误: {e}")
                break

    def insert_colored_line(self, line):
        self.output.insert(tk.END, line + "\n")
        # 根据关键词打 tag
        if "ERROR" in line:
            self.highlight_line("ERROR")
        elif "WARN" in line or "WARNING" in line:
            self.highlight_line("WARN")
        elif re.search(r'info', line, re.I):
            self.highlight_line("INFO")
        self.output.see(tk.END)

    def highlight_line(self, tag):
        # 从 Text 最后一行找到行首到行尾，打上 tag
        index_start = self.output.index("end-2l linestart")
        index_end = self.output.index("end-2l lineend")
        self.output.tag_add(tag, index_start, index_end)

    def send_command(self, event=None):
        if self.serial_port and self.serial_port.is_open:
            cmd = self.input_entry.get().strip()
            if cmd:
                self.serial_port.write((cmd + '\r\n').encode())
                self.input_entry.delete(0, tk.END)
                timestamp = datetime.now().strftime("[%Y-%m-%d %H:%M:%S] ")
                self.output.insert(tk.END, timestamp + f"[TX] {cmd}\n")
                self.highlight_line("TX")
                self.output.see(tk.END)

if __name__ == "__main__":
    root = tk.Tk()
    app = SerialGUI(root)
    root.mainloop()
