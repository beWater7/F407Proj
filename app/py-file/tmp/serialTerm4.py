import tkinter as tk
from tkinter import ttk, filedialog, messagebox
import serial
import serial.tools.list_ports
import threading
import pyte
import time
from datetime import datetime
import ctypes

# DPI Aware 提高清晰度
try:
    ctypes.windll.shcore.SetProcessDpiAwareness(1)
except:
    pass

class PyteSerialMonitor(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("pyte 串口终端")
        self.geometry("2000x1400")
        self.configure(bg="#2d2d2d")

        self.running = False
        self.paused = False
        self.auto_scroll = True
        self.timeStamp = False

        self.serial_port = None
        self.logfile = None

        self.cols = 120
        self.rows = 40
        self.screen = pyte.Screen(self.cols, self.rows)
        self.stream = pyte.Stream(self.screen)

        self.history = []
        self.max_history = 1000

        self.create_widgets()

        self.font = ("Consolas", 10)
        self.char_width = 16
        self.char_height = 32
        self.visible_lines = 80

        self.text_items = []
        for i in range(self.visible_lines):
            item = self.canvas.create_text(2, i * self.char_height, anchor="nw",
                                           text="", fill="white", font=self.font)
            self.text_items.append(item)

        self.after(100, self.refresh_screen)

    def create_widgets(self):
        style = ttk.Style(self)
        style.theme_use('clam')

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

        # 日志路径区
        frm_log = ttk.Frame(self)
        frm_log.pack(fill='x', padx=10, pady=5)
        ttk.Label(frm_log, text="日志保存:").pack(side='left')
        #self.log_path_var = tk.StringVar(value=os.path.join(os.path.expanduser("~"), "Desktop", "serial_logs"))
        
        #默认保存路径 
        self.log_path_var = tk.StringVar(value=r"C:\Users\liudayi\Desktop\stm32工程\TEMPLATE_2\log")
        ttk.Entry(frm_log, textvariable=self.log_path_var).pack(side='left', fill='x', expand=True, padx=5)
        ttk.Button(frm_log, text="选择", command=self.browse_folder).pack(side='left')

        # 输入区
        frm_input = ttk.Frame(self)
        frm_input.pack(fill='x', padx=10, pady=(0,10))
        self.ent_input = ttk.Entry(frm_input)
        self.ent_input.pack(side='left', fill='x', expand=True)
        self.ent_input.bind('<Return>', self.send_command)
        ttk.Button(frm_input, text="发送", command=self.send_command).pack(side='left', padx=5)

        self.canvas = tk.Canvas(self, bg='black')
        self.canvas.pack(side='left', fill='both', expand=True)

        self.scrollbar = ttk.Scrollbar(self, orient='vertical', command=self.on_scroll)
        self.scrollbar.pack(side='right', fill='y')
        self.canvas.configure(yscrollcommand=self.scrollbar.set)

        # 状态变量
        self.paused = False
        self.timeStamp = False
        self.serial_port = None
        self.logfile = None

    def get_ports(self):
        return [p.device for p in serial.tools.list_ports.comports()]

    def refresh_ports(self):
        self.cb_ports['values'] = self.get_ports()
    
    def browse_folder(self):
        """选择日志文件夹（主线程执行）"""
        try:
            folder = filedialog.askdirectory()
            if folder:
                self.log_path_var.set(folder)
                log(f"日志路径设置为: {folder}")
        except Exception as e:
            log(f"选择文件夹失败: {str(e)}")
    
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
            self.serial_port = None
            return
        
        # 打开日志文件
        date_str = datetime.now().strftime("%Y%m%d_%H%M%S")
        log_filename = f"{port.replace('/', '_')}_{date_str}.log"
        log_path = os.path.join(log_dir, log_filename)
        self.logfile = open(log_path, "a", encoding="utf-8")
        log(f"日志文件打开: {log_path}")

        self.running = True
        self.btn_start.config(state='disabled')
        self.btn_stop.config(state='normal')
        self.btn_pause.config(state='normal')

        threading.Thread(target=self.read_serial, daemon=True).start()

    def stop(self):
        self.running = False
        if self.serial_port:
            try: self.serial_port.close()
            except: pass
            self.serial_port = None
        # 关闭日志文件
        if self.logfile:
            self.logfile.close()
            log("日志文件已关闭")
            self.logfile = None
        
        self.btn_start.config(state='normal')
        self.btn_stop.config(state='disabled')
        self.btn_pause.config(state='disabled')

    def read_serial(self):
        while self.running:
            try:
                raw = self.serial_port.read(1024)
                if not raw:
                    continue
                text = raw.decode(errors='ignore')
                fixed_line = line.replace('\n', '\r\n')
                # 加时间戳
                timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
                if self.timeStamp:
                    line = f"[{timestamp}] {fixed_line}"
                else:
                    line = fixed_line
                self.stream.feed(line)

                # 写入日志
                #with self.thread_lock:
                if self.logfile:
                    self.logfile.write(f"{timestamp} {line}\n")
                    self.logfile.flush()
                
                lines = self.screen.display
                self.history = ["".join(row) for row in lines]
                if len(self.history) > self.max_history:
                    self.history = self.history[-self.max_history:]
            except Exception as e:
                print("串口读异常:", e)
                time.sleep(1)

    def refresh_screen(self):
        if self.paused:
            self.after(200, self.refresh_screen)
            return

        total_lines = len(self.history)
        y0 = int(self.canvas.canvasy(0) // self.char_height)

        for i in range(len(self.text_items)):
            y = y0 + i
            if 0 <= y < total_lines:
                line = self.history[y]
                self.canvas.itemconfig(self.text_items[i], text=line)
            else:
                self.canvas.itemconfig(self.text_items[i], text="")
            self.canvas.coords(self.text_items[i], 2, i * self.char_height)

        self.canvas.config(scrollregion=(0, 0, self.char_width * self.cols, self.char_height * total_lines))

        if self.auto_scroll:
            self.canvas.yview_moveto(1.0)

        self.after(100, self.refresh_screen)

    def on_scroll(self, *args):
        self.auto_scroll = False
        self.canvas.yview(*args)

    def toggle_pause(self):
        self.paused = not self.paused
        self.btn_pause.config(text="恢复显示" if self.paused else "暂停显示")
    
    def toggle_timestamp(self):
        """切换时间戳（主线程执行）"""
        self.timeStamp = not self.timeStamp
        btn_text = "关闭时间戳" if self.timeStamp else "显示时间戳"
        self.btn_sysTime.config(text=btn_text)
        log(f"时间戳已{('开启' if self.timeStamp else '关闭')}")
    
    def on_close(self):
        self.stop()
        self.destroy()

if __name__ == "__main__":
    app = PyteSerialMonitor()
    app.protocol("WM_DELETE_WINDOW", app.on_close)
    app.mainloop()

