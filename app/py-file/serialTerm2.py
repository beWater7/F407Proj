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
        self.title("pyte + Canvas 串口终端")
        self.geometry("2000x1400")
        #self.configure(bg="#1e1e1e")
        self.configure(bg="#2d2d2d")
        # 串口状态
        self.running = False
        self.paused = False
        self.timeStamp = False
        self.serial_port = None
        self.logfile = None

        # pyte 虚拟终端
        #self.screen = pyte.Screen(120, 40)
        self.cols = 120  # 终端列数
        self.rows = 40   # 终端行数
        self.screen = pyte.Screen(self.cols, self.rows)
        #self.screen.history = pyte.History(1000)
        self.stream = pyte.Stream(self.screen)
        self.history = []  # 用 Python 自己的 list 做 history
        self.max_history = 1000

        #self.screen.scroll_y = 1000
        self.auto_scroll = True  # 自动跟随
        # Tkinter GUI
        self.create_widgets()

        self.font = ("Consolas", 10)
        self.char_width = 16
        self.char_height = 32

        self.visible_lines = 200
        self.text_items = []

        for i in range(self.visible_lines):
            item = self.canvas.create_text(0, 0, text="", anchor="nw", fill="white", font=self.font)
            self.text_items.append(item)
        
        # 绑定滚动
        self.canvas.bind("<Enter>", lambda e: self.canvas.bind_all("<MouseWheel>", self.on_mousewheel))
        self.canvas.bind("<Leave>", lambda e: self.canvas.unbind_all("<MouseWheel>"))

        # 滚动条拖动时自动取消自动滚动
        self.canvas.bind('<Button-1>', self.cancel_auto_scroll)

        # 模拟打印
        #self.after(500, self.mock_print)
        # 定时刷新画面
        self.after(50, self.refresh_screen)

    def mock_print(self):
        # 每次加一行
        self.history.append(f"这是测试行 {len(self.history)}")
        if len(self.history) > self.max_history:
            self.history.pop(0)
        self.after(500, self.mock_print)


    def create_widgets(self):
        style = ttk.Style(self)
        style.theme_use('clam')

        frm_top = ttk.Frame(self)
        frm_top.pack(fill='x', padx=10, pady=5)

        ttk.Label(frm_top, text="串口:").pack(side='left')
        self.cb_ports = ttk.Combobox(frm_top, width=15, values=self.get_ports(), state='readonly')
        self.cb_ports.pack(side='left', padx=5)
        ttk.Button(frm_top, text="刷新", command=self.refresh_ports).pack(side='left')

        ttk.Label(frm_top, text="波特率:").pack(side='left', padx=(20,0))
        self.cb_baud = ttk.Combobox(frm_top, width=10, values=["9600", "115200", "230400", "460800"], state='readonly')
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
        self.btn_reset = ttk.Button(frm_top, text="复位", command=self.reset_device, state='disabled')
        self.btn_reset.pack(side='left', padx=5)

        frm_log = ttk.Frame(self)
        frm_log.pack(fill='x', padx=10, pady=5)
        ttk.Label(frm_log, text="日志保存:").pack(side='left')
        #self.log_path_var = tk.StringVar(value=".")
        #默认保存路径 
        self.log_path_var = tk.StringVar(value=r"C:\Users\liudayi\Desktop\stm32工程\TEMPLATE_2\log")
        ttk.Entry(frm_log, textvariable=self.log_path_var).pack(side='left', fill='x', expand=True, padx=5)
        ttk.Button(frm_log, text="选择", command=self.browse_folder).pack(side='left')

        self.canvas = tk.Canvas(self, bg='black')

        self.canvas.pack(side='left', fill='both', expand=True)

        self.scrollbar = ttk.Scrollbar(self, orient='vertical', command=self.on_scroll)
        self.scrollbar.pack(side='right', fill='y')
        self.canvas.configure(yscrollcommand=self.scrollbar.set)
        """
       
        self.vbar = tk.Scrollbar(self, orient="vertical", command=self.on_scroll)
        self.canvas.config(yscrollcommand=self.vbar.set)
        self.canvas.pack(side="left", fill="both", expand=True)
        self.vbar.pack(side="right", fill="y")
        """
        frm_input = ttk.Frame(self)
        frm_input.pack(fill='x', padx=10, pady=(0,10))
        self.ent_input = ttk.Entry(frm_input)
        self.ent_input.pack(side='left', fill='x', expand=True)
        self.ent_input.bind('<Return>', self.send_command)
        ttk.Button(frm_input, text="发送", command=self.send_command).pack(side='left', padx=5)

    def get_ports(self):
        return [p.device for p in serial.tools.list_ports.comports()]

    def refresh_ports(self):
        self.cb_ports['values'] = self.get_ports()

    def browse_folder(self):
        folder = filedialog.askdirectory()
        if folder:
            self.log_path_var.set(folder)
    
    def fix_cr(self, data):
            #自动把裸\n转为\r\n
            fixed = ''
            i = 0
            while i < len(data):
                if data[i] == '\n':
                    fixed += '\r'
                    if i+1 >= len(data) or data[i+1] != '\n':
                        fixed += '\n'
                else:
                    fixed += data[i]
                i += 1
            return fixed
    """
    def fix_cr(self, data):
        return data.replace('\n', '\r\n')
    """
    def start(self):
        port = self.cb_ports.get()
        baud = self.cb_baud.get()
        if not port or not baud:
            messagebox.showwarning("提示", "请选择串口和波特率")
            return

        self.stop()
        try:
            self.serial_port = serial.Serial(port, int(baud), timeout=0.5)
            self.serial_port.setDTR(False)
            self.serial_port.setRTS(False)
            #if serial_stop == 'reset':
                # 正确顺序：先开再拉线（野火常用）
                #self.serial_port.setDTR(False)
                #self.serial_port.setRTS(False)
        except Exception as e:
            messagebox.showerror("错误", f"打开串口失败: {e}")
            self.serial_port = None
            return

        date_str = datetime.now().strftime("%Y%m%d_%H%M%S")
        log_path = f"{self.log_path_var.get()}/{port.replace('/', '_')}_{date_str}.log"
        self.logfile = open(log_path, "a", encoding="utf-8")

        self.running = True
        self.btn_start.config(state='disabled')
        self.btn_stop.config(state='normal')
        self.btn_pause.config(state='normal')
        self.btn_sysTime.config(state='normal')
        self.btn_reset.config(state='normal')

        threading.Thread(target=self.read_serial, daemon=True).start()
        threading.Thread(target=self.monitor_port, daemon=True).start()

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
        self.btn_pause.config(state='disabled')
        self.btn_sysTime.config(state='disabled')
        self.btn_reset.config(state='disabled')

    def read_serial(self):
        while self.running:
            try:
                raw = self.serial_port.read(1024)
                if not raw:
                    continue
                line = raw.decode(errors='ignore')
                fixed_line = self.fix_cr(line)
                timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
                # DEBUG: 看看进来的数据
                print("RAW:", repr(line))
                if self.timeStamp:
                    line = f"[{timestamp}] {fixed_line}"
                else:
                    line = fixed_line
                self.stream.feed(line)
                self.history.append(line)
                self.logfile.write(timestamp + fixed_line + '\n')
                self.logfile.flush()
            except Exception as e:
                print("串口读异常:", e)
                time.sleep(1)

    def monitor_port(self):
        while self.running:
            ports = self.get_ports()
            if self.serial_port and self.serial_port.port not in ports:
                print("串口掉线，尝试重连")
                self.stop()
                time.sleep(2)
                self.start()
            time.sleep(1)
    """
    def refresh_screen(self):
        if self.paused:
            self.after(100, self.refresh_screen)
            return

        self.canvas.delete("all")
        for y, row in enumerate(self.screen.display):
            for x, char in enumerate(row):
                if char != ' ':
                    self.canvas.create_text(
                        x*self.char_width,
                        y*self.char_height,
                        text=char,
                        anchor='nw',
                        fill='white',
                        font=self.font
                    )
        self.canvas.config(scrollregion=self.canvas.bbox("all"))
        self.after(50, self.refresh_screen)
    """
    """
    def refresh_screen(self):
        if self.paused:
            self.after(100, self.refresh_screen)
            return
        
        self.update_idletasks()
        lines = list(self.screen.history) + self.screen.display
        total_lines = len(lines)

        y0 = int(self.canvas.canvasy(0) // self.char_height)
        y1 = y0 + self.visible_lines

        for i, y in enumerate(range(y0, min(y1, total_lines))):
            line = "".join(lines[y]).rstrip()
            self.canvas.itemconfig(self.text_items[i], text=line)
            self.canvas.coords(self.text_items[i], 2, y * self.char_height)

        self.canvas.config(scrollregion=(0, 0, self.char_width * self.cols, self.char_height * total_lines))
  
        if self.auto_scroll:
            self.canvas.yview_moveto(1.0)
        
        self.after(50, self.refresh_screen)
    """
    def refresh_screen(self):
        if self.paused:
            self.after(200, self.refresh_screen)
            return

        total_lines = len(self.history)
        y0 = int(self.canvas.canvasy(0) // self.char_height)
        y1 = y0 + self.visible_lines

        for i in range(self.visible_lines):
            y = y0 + i
            if y >= total_lines:
                self.canvas.itemconfig(self.text_items[i], text="")
            else:
                self.canvas.itemconfig(self.text_items[i], text=self.history[y])

            self.canvas.coords(self.text_items[i], 2, i * self.char_height)

        self.canvas.config(scrollregion=(0, 0, self.char_width * self.cols, self.char_height * total_lines))

        if self.auto_scroll:
            self.canvas.yview_moveto(1.0)

        self.after(100, self.refresh_screen)

        def on_scroll(self, *args):
            self.auto_scroll = False
            self.canvas.yview(*args)
    """
    def refresh_screen(self):
        lines = self.history
        total_lines = len(lines)

        for i, line in enumerate(lines[-len(self.text_items):]):
            self.canvas.itemconfig(self.text_items[i], text=line)
            self.canvas.coords(self.text_items[i], 5, i * self.char_height)

        # 如果剩余可见空间没用完，清空多余项
        for j in range(len(lines), len(self.text_items)):
            self.canvas.itemconfig(self.text_items[j], text="")

        # 更新 scrollregion
        self.canvas.config(scrollregion=(0, 0, 800, total_lines * self.char_height))

        if self.auto_scroll:
            self.canvas.yview_moveto(1.0)

        self.after(50, self.refresh_screen)

    def on_mousewheel(self, event):
        self.auto_scroll = False
        self.canvas.yview_scroll(-1 * int(event.delta / 120), "units")

    def cancel_auto_scroll(self, event):
        self.auto_scroll = False
    """

    def toggle_pause(self):
        self.paused = not self.paused
        self.btn_pause.config(text="恢复显示" if self.paused else "暂停显示")

    def toggle_timestamp(self):
        self.timeStamp = not self.timeStamp
        self.btn_sysTime.config(text="关闭时间戳" if self.timeStamp else "显示时间戳")

    def reset_device(self):
        if self.serial_port:
            self.serial_port.setDTR(False)
            self.serial_port.setRTS(False)
            print("DTR/RTS 已复位")

    def send_command(self, event=None):
        if self.serial_port and self.serial_port.is_open:
            cmd = self.ent_input.get().strip()
            if cmd:
                self.serial_port.write((cmd + '\r\n').encode())
                self.ent_input.delete(0, tk.END)

    def on_close(self):
        self.stop()
        self.destroy()

if __name__ == "__main__":
    app = PyteSerialMonitor()
    app.protocol("WM_DELETE_WINDOW", app.on_close)
    app.mainloop()

