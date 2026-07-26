import ctypes
import tkinter as tk
from tkinter import ttk, scrolledtext, messagebox, filedialog
import serial
import serial.tools.list_ports
import threading
import os
import re
import time
from datetime import datetime

try:
    ctypes.windll.shcore.SetProcessDpiAwareness(1)
except:
    pass

ANSI_RE = re.compile(r'\x1b\[([0-9;]+)m')

class SerialMonitor(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("串口监控器（自动重连版）")
        self.geometry("1200x800")
        self.configure(bg="#2d2d2d")

        self.serial_port = None
        self.running = False
        self.logfile = None

        self.create_widgets()
        self.protocol("WM_DELETE_WINDOW", self.on_close)

    def create_widgets(self):
        style = ttk.Style(self)
        style.theme_use('clam')
        style.configure("TLabel", background="#2d2d2d", foreground="#d4d4d4", font=("Consolas", 11))
        style.configure("TButton", font=("Consolas", 11))
        style.configure("TEntry", font=("Consolas", 11))
        style.configure("TCombobox", font=("Consolas", 11))

        frm_top = ttk.Frame(self)
        frm_top.pack(fill='x', padx=10, pady=8)

        ttk.Label(frm_top, text="串口:").pack(side='left')
        self.cb_ports = ttk.Combobox(frm_top, width=12, values=self.get_ports(), state='readonly')
        self.cb_ports.pack(side='left', padx=6)
        self.btn_refresh = ttk.Button(frm_top, text="刷新", command=self.refresh_ports)
        self.btn_refresh.pack(side='left')

        ttk.Label(frm_top, text="波特率:").pack(side='left', padx=(20, 0))
        self.cb_baud = ttk.Combobox(frm_top, width=12, values=["9600", "19200", "38400", "57600", "115200"], state='readonly')
        self.cb_baud.set("115200")
        self.cb_baud.pack(side='left', padx=6)

        self.btn_start = ttk.Button(frm_top, text="打开串口", command=self.start)
        self.btn_start.pack(side='left', padx=(30, 6))
        self.btn_stop = ttk.Button(frm_top, text="关闭串口", command=self.stop, state='disabled')
        self.btn_stop.pack(side='left')

        frm_log = ttk.Frame(self)
        frm_log.pack(fill='x', padx=10)
        ttk.Label(frm_log, text="日志保存路径:").pack(side='left')
        self.log_path_var = tk.StringVar(value=r"C:\Users\liudayi\Desktop\stm32工程\TEMPLATE_2\log")
        self.ent_log_path = ttk.Entry(frm_log, textvariable=self.log_path_var)
        self.ent_log_path.pack(side='left', fill='x', expand=True, padx=6)
        self.btn_browse = ttk.Button(frm_log, text="选择目录", command=self.browse_folder)
        self.btn_browse.pack(side='left')

        self.txt_output = scrolledtext.ScrolledText(self, font=("Consolas", 10), bg="#1e1e1e", fg="#d4d4d4", insertbackground="white")
        self.txt_output.pack(fill='both', expand=True, padx=10, pady=10)

        for color in ['reset', 'red', 'green', 'yellow', 'blue', 'magenta', 'cyan']:
            self.txt_output.tag_config(color, foreground={'reset': '#d4d4d4', 'red': '#f44747',
                                                          'green': '#608b4e', 'yellow': '#dcdcaa',
                                                          'blue': '#569cd6', 'magenta': '#c586c0',
                                                          'cyan': '#4ec9b0'}[color])

        frm_input = ttk.Frame(self)
        frm_input.pack(fill='x', padx=10, pady=(0, 10))
        self.ent_input = ttk.Entry(frm_input, font=("Consolas", 12))
        self.ent_input.pack(side='left', fill='x', expand=True)
        self.ent_input.bind('<Return>', self.send_command)
        self.btn_send = ttk.Button(frm_input, text="发送", command=self.send_command)
        self.btn_send.pack(side='left', padx=6)

    def get_ports(self):
        return [p.device for p in serial.tools.list_ports.comports()]

    def refresh_ports(self):
        self.cb_ports['values'] = self.get_ports()

    def browse_folder(self):
        folder = filedialog.askdirectory()
        if folder:
            self.log_path_var.set(folder)

    def start(self):
        port = self.cb_ports.get()
        baud = self.cb_baud.get()
        if not port or not baud:
            messagebox.showwarning("提示", "请选择串口和波特率")
            return

        if self.serial_port:
            try:
                if self.serial_port.is_open:
                    self.serial_port.close()
            except:
                pass
            self.serial_port = None

        try:
            self.serial_port = serial.Serial(port, int(baud), timeout=0.5)
        except Exception as e:
            messagebox.showerror("错误", f"打开串口失败: {e}")
            return

        date_str = datetime.now().strftime("%Y%m%d_%H%M%S")
        filename = f"{port.replace(':','').replace('/','')}_{date_str}.log"
        log_path = os.path.join(self.log_path_var.get(), filename)
        try:
            self.logfile = open(log_path, "a", encoding="utf-8")
        except Exception as e:
            messagebox.showerror("错误", f"日志文件打开失败: {e}")
            self.serial_port.close()
            self.serial_port = None
            return

        self.running = True
        self.btn_start.config(state='disabled')
        self.btn_stop.config(state='normal')
        self.print_text(f"[系统] 串口 {port} 已打开，波特率 {baud}\n", 'green')

        self.thread = threading.Thread(target=self.read_from_port, daemon=True)
        self.thread.start()

        self.monitor_thread = threading.Thread(target=self.monitor_port, daemon=True)
        self.monitor_thread.start()

    def read_from_port(self):
        while self.running:
            try:
                raw = self.serial_port.readline()
                if not raw:
                    continue
                line = raw.decode(errors='ignore').rstrip('\r\n')
                timestamp = datetime.now().strftime("[%Y-%m-%d %H:%M:%S] ")
                self.insert_ansi(timestamp + line + '\n')
                plain = ANSI_RE.sub('', line)
                self.logfile.write(timestamp + plain + '\n')
                self.logfile.flush()
            except Exception as e:
                self.print_text(f"[错误] 串口异常: {e}\n", 'red')
                try:
                    self.serial_port.close()
                except:
                    pass
                self.serial_port = None
                break

    def monitor_port(self):
        while self.running:
            if self.serial_port is None:
                port = self.cb_ports.get()
                baud = self.cb_baud.get()
                ports = self.get_ports()
                if port in ports:
                    try:
                        self.serial_port = serial.Serial(port, int(baud), timeout=0.5)
                        self.print_text(f"[系统] 自动重连 {port} 成功\n", 'green')
                        self.thread = threading.Thread(target=self.read_from_port, daemon=True)
                        self.thread.start()
                    except Exception as e:
                        self.print_text(f"[错误] 自动重连失败: {e}\n", 'red')
                time.sleep(1)
            else:
                time.sleep(1)

    def stop(self):
        self.running = False
        if self.serial_port and self.serial_port.is_open:
            self.serial_port.close()
        self.serial_port = None
        if self.logfile:
            self.logfile.close()
            self.logfile = None
        self.btn_start.config(state='normal')
        self.btn_stop.config(state='disabled')
        self.print_text("[系统] 串口已关闭\n", 'yellow')

    def insert_ansi(self, text):
        pos = 0
        tag = 'reset'
        for m in ANSI_RE.finditer(text):
            start, end = m.span()
            self.txt_output.insert(tk.END, text[pos:start], tag)
            tag = self.ansi_code_to_tag(m.group(1))
            pos = end
        self.txt_output.insert(tk.END, text[pos:], tag)
        self.txt_output.see(tk.END)

    def ansi_code_to_tag(self, code):
        return {
            '0': 'reset', '39': 'reset',
            '1;31': 'red', '31': 'red',
            '1;32': 'green', '32': 'green',
            '1;33': 'yellow', '33': 'yellow',
            '1;34': 'blue', '34': 'blue',
            '1;35': 'magenta', '35': 'magenta',
            '1;36': 'cyan', '36': 'cyan'
        }.get(code, 'reset')

    def send_command(self, event=None):
        if not self.serial_port or not self.serial_port.is_open:
            messagebox.showwarning("提示", "串口未打开")
            return
        cmd = self.ent_input.get().strip()
        if not cmd:
            return
        try:
            self.serial_port.write((cmd + '\r\n').encode())
            self.print_text(f"[TX] {cmd}\n", 'blue')
            self.ent_input.delete(0, tk.END)
        except Exception as e:
            self.print_text(f"[错误] 发送失败: {e}\n", 'red')

    def print_text(self, text, tag='reset'):
        self.txt_output.insert(tk.END, text, tag)
        self.txt_output.see(tk.END)

    def on_close(self):
        self.stop()
        self.destroy()

if __name__ == "__main__":
    app = SerialMonitor()
    app.mainloop()
