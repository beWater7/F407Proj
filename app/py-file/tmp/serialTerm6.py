import tkinter as tk
from tkinter import ttk, filedialog, messagebox
import serial
import serial.tools.list_ports
import threading
import time
from datetime import datetime
import os
import re
import ctypes

import pyte  # ✅ 别忘了装 pyte

# DPI Aware 提高清晰度
try:
    ctypes.windll.shcore.SetProcessDpiAwareness(1)
except:
    pass

class SerialTerminal(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("串口终端（ANSI 彩色 + 覆盖写）")
        self.geometry("2000x1400")

        self.running = False
        self.auto_scroll = True
        self.timeStamp = False
        self.history = []
        self.max_history = 2000

        self.serial_port = None
        self.logfile = None
        self.paused = False

        self.create_widgets()
        self.init_tags()

        # ✅ 初始化 pyte
        self.screen = pyte.Screen(100, 500)  # 列数行数可调
        self.stream = pyte.Stream(self.screen)

        # ✅ 颜色映射表
        self.color_map = {
            None: 'default',
            '0': 'default',
            '1': 'bold',
            '30': 'black',
            '31': 'red',
            '32': 'green',
            '33': 'yellow',
            '34': 'blue',
            '35': 'magenta',
            '36': 'cyan',
            '37': 'white'
        }

    def create_widgets(self):
        frm_top = ttk.Frame(self)
        frm_top.pack(fill='x', padx=5, pady=5)

        ttk.Label(frm_top, text="串口:").pack(side='left')
        self.cb_ports = ttk.Combobox(frm_top, width=15, values=self.get_ports(), state='readonly')
        self.cb_ports.pack(side='left', padx=5)
        ttk.Button(frm_top, text="刷新", command=self.refresh_ports).pack(side='left')

        ttk.Label(frm_top, text="波特率:").pack(side='left', padx=(20,0))
        self.cb_baud = ttk.Combobox(frm_top, width=10, values=["9600", "115200", "230400"], state='readonly')
        self.cb_baud.set("115200")
        self.cb_baud.pack(side='left', padx=5)

        self.btn_start = ttk.Button(frm_top, text="打开串口", command=self.start)
        self.btn_start.pack(side='left', padx=5)
        self.btn_stop = ttk.Button(frm_top, text="关闭串口", command=self.stop, state='disabled')
        self.btn_stop.pack(side='left', padx=5)

        self.btn_pause = ttk.Button(frm_top, text="暂停显示", command=self.toggle_pause, state='disabled')
        self.btn_pause.pack(side='left', padx=5)
        self.btn_sysTime = ttk.Button(frm_top, text="显示时间戳", command=self.toggle_timestamp, state='disabled')
        self.btn_sysTime.pack(side='left', padx=5)

        frm_log = ttk.Frame(self)
        frm_log.pack(fill='x', padx=5, pady=5)

        ttk.Label(frm_log, text="日志保存:").pack(side='left')
        self.log_path_var = tk.StringVar(value=r"C:\Users\liudayi\Desktop\stm32工程\TEMPLATE_2\log")
        ttk.Entry(frm_log, textvariable=self.log_path_var).pack(side='left', fill='x', expand=True, padx=5)
        ttk.Button(frm_log, text="选择", command=self.browse_folder).pack(side='left')

        frm_terminal = ttk.Frame(self)
        frm_terminal.pack(fill='both', expand=True, padx=5, pady=5)

        self.text = tk.Text(frm_terminal, bg='black', fg='white', insertbackground='white',
                            wrap='none', font=("Consolas", 10))
        self.text.pack(side='left', fill='both', expand=True)

        self.scrollbar = ttk.Scrollbar(frm_terminal, orient='vertical', command=self.text.yview)
        self.scrollbar.pack(side='right', fill='y', ipadx=10)
        self.text.config(yscrollcommand=self.scrollbar.set)

        self.text.bind('<MouseWheel>', self.on_mousewheel)

        frm_input = ttk.Frame(self)
        frm_input.pack(fill='x', padx=5, pady=5)

        self.ent_input = ttk.Entry(frm_input)
        self.ent_input.pack(side='left', fill='x', expand=True)
        self.ent_input.bind('<Return>', self.send_command)
        ttk.Button(frm_input, text="发送", command=self.send_command).pack(side='left', padx=5)

    def init_tags(self):
        self.text.tag_config('default', foreground='white')
        self.text.tag_config('bold', font=("Consolas", 10, 'bold'))
        self.text.tag_config('black', foreground='black')
        self.text.tag_config('red', foreground='red')
        self.text.tag_config('green', foreground='green')
        self.text.tag_config('yellow', foreground='yellow')
        self.text.tag_config('blue', foreground='blue')
        self.text.tag_config('magenta', foreground='magenta')
        self.text.tag_config('cyan', foreground='cyan')
        self.text.tag_config('white', foreground='white')

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

        self.stop()
        try:
            self.serial_port = serial.Serial(port, int(baud), timeout=0.1)
            self.serial_port.setDTR(False)
            self.serial_port.setRTS(False)
        except Exception as e:
            messagebox.showerror("错误", f"打开串口失败: {e}")
            return

        log_filename = f"{port.replace('/', '_')}_{datetime.now().strftime('%Y%m%d_%H%M%S')}.log"
        log_dir = self.log_path_var.get()
        os.makedirs(log_dir, exist_ok=True)
        log_path = os.path.join(log_dir, log_filename)
        self.logfile = open(log_path, "a", encoding="utf-8")

        self.running = True
        self.btn_start.config(state='disabled')
        self.btn_stop.config(state='normal')
        self.btn_sysTime.config(state='normal')

        threading.Thread(target=self.read_serial, daemon=True).start()

    def stop(self):
        self.running = False
        if self.serial_port:
            try: self.serial_port.close()
            except: pass
            self.serial_port = None
        if self.logfile:
            self.logfile.close()
            self.logfile = None

        self.btn_start.config(state='normal')
        self.btn_stop.config(state='disabled')

    def read_serial(self):
        while self.running:
            if self.paused:
                time.sleep(0.1)
                continue
            try:
                raw = self.serial_port.read(1024)
                if not raw:
                    continue
                text = raw.decode(errors='ignore')
                self.stream.feed(text)

                self.history.append(text)
                if len(self.history) > self.max_history:
                    self.history.pop(0)

                if self.logfile:
                    timestamp = datetime.now().strftime('%H:%M:%S')
                    self.logfile.write(f"[{timestamp}] {text}")
                    self.logfile.flush()

                self.after(0, self.update_terminal_from_pyte)
            except Exception as e:
                print(f"串口异常: {e}")
                time.sleep(1)

    def update_terminal_from_pyte(self):
        self.text.configure(state='normal')
        self.text.delete('1.0', tk.END)

        for y in range(self.screen.lines):
            line = self.screen.buffer[y]
            last_attr = None
            segment = ''

            for x in range(self.screen.columns):
                char = line[x]
                c = char.data if char.data else ' '

                attr = self.color_map.get(str(char.fg), 'default')

                if attr != last_attr:
                    if segment:
                        self.text.insert(tk.END, segment, last_attr)
                    segment = ''
                    last_attr = attr

                segment += c

            if segment:
                self.text.insert(tk.END, segment, last_attr)

            self.text.insert(tk.END, '\n')

        if self.auto_scroll:
            self.text.see(tk.END)

        self.text.configure(state='disabled')

    def send_command(self, event=None):
        cmd = self.ent_input.get().strip()
        if cmd and self.serial_port and self.serial_port.is_open:
            self.serial_port.write((cmd + '\r\n').encode())
            self.ent_input.delete(0, tk.END)

    def toggle_timestamp(self):
        self.timeStamp = not self.timeStamp
        self.btn_sysTime.config(text="关闭时间戳" if self.timeStamp else "显示时间戳")

    def on_mousewheel(self, event):
        self.auto_scroll = False

    def toggle_pause(self):
        self.paused = not self.paused
        self.btn_pause.config(text="恢复显示" if self.paused else "暂停显示")

    def on_close(self):
        self.stop()
        self.destroy()

if __name__ == "__main__":
    app = SerialTerminal()
    app.protocol("WM_DELETE_WINDOW", app.on_close)
    app.mainloop()
