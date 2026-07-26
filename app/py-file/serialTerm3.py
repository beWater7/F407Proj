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

# DPI Aware 提高清晰度
try:
    ctypes.windll.shcore.SetProcessDpiAwareness(1)
except:
    pass

ANSI_RE = re.compile(r'\x1b\[([0-9;]+)m')
SPLIT_RE = re.compile(r'\r\n|\n|\r')

class SerialMonitor(tk.Tk):
    def __init__(self):
        global serial_stop
        serial_stop = 'reset'
        super().__init__()
        self.title("串口监控器（带系统时间戳 + ANSI彩色 + 野火初始化）")
        self.geometry("2000x1400")
        self.configure(bg="#2d2d2d")
        self.paused = False
        self.timeStamp = False
        """
        # 默认缩放
        self.scale = 1.5
        self.tk.call('tk', 'scaling', self.scale)
        
        self.option_add("*Font", "Consolas 12")

        ttk.Button(self, text="放大", command=self.zoom_in).pack(pady=10)
        ttk.Button(self, text="缩小", command=self.zoom_out).pack(pady=10)

        self.label = ttk.Label(self, text="Hello Tkinter!")
        self.label.pack(pady=20)
        """
        self.serial_port = None
        self.running = False
        self.logfile = None

        # 111    
        self.prompt_prefix = "[mcu@board]#"

        self.create_widgets()
        
        self.txt_output.bind("<Key>", self.on_key)
        self.txt_output.bind("<Return>", self.on_enter)
        
        self.protocol("WM_DELETE_WINDOW", self.on_close)
        #self.txt_output.delete(line_start, line_end)
        
        """
        # 必须初始化
        self.prev_line_len = 0

        # 如果用了 current_line_index，也一并初始化
        self.current_line_index = None
        """
    """
    def zoom_in(self):
        self.scale += 0.1
        self.tk.call('tk', 'scaling', self.scale)

    def zoom_out(self):
        self.scale = max(0.5, self.scale - 0.1)
        self.tk.call('tk', 'scaling', self.scale)
    """
    def on_key(self, event):
        # 不允许光标移到提示符前面
        cursor = self.txt_output.index(tk.INSERT)
        line_start = self.txt_output.index("insert linestart")

        # 当前行的内容
        line_text = self.txt_output.get(line_start, f"{line_start} lineend")
        if line_text.startswith(self.prompt_prefix):
            prompt_end_idx = f"{line_start}+{len(self.prompt_prefix)}c"  # +1 for space
            if self.txt_output.compare(cursor, "<", prompt_end_idx):
                self.txt_output.mark_set(tk.INSERT, tk.END)
        return None

    def on_enter(self, event):
        cursor = self.txt_output.index(tk.INSERT)
        line_start = self.txt_output.index("insert linestart")
        line_end = self.txt_output.index("insert lineend")

        line_text = self.txt_output.get(line_start, line_end)
        if line_text.startswith(self.prompt_prefix):
            user_input = line_text[len(self.prompt_prefix):].strip()
            #if user_input:
            print(f"[TX] 发送: {user_input}")  # TODO: 串口发送
            # TODO: self.serial_port.write((user_input + '\r\n').encode())
            self.serial_port.write((user_input + '\n').encode())
        # 插入换行符，但不再加提示符（MCU自己会打印）
        #self.txt_output.mark_set(tk.INSERT, f"{tk.END} -1c")
        #self.txt_output.insert(tk.END)
            # 输入数据有正文时打印换行符
            if user_input:
                self.print_text(f"\n", "white")
        return "break"


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
        self.cb_baud = ttk.Combobox(frm_top, width=12, values=["9600", "19200", "38400", "57600", "115200", "230400", "460800", "921600"], state='readonly')
        self.cb_baud.set("115200")
        self.cb_baud.pack(side='left', padx=6)

        self.btn_start = ttk.Button(frm_top, text="打开串口", command=self.start)
        self.btn_start.pack(side='left', padx=(30, 6))
        self.btn_stop = ttk.Button(frm_top, text="关闭串口", command=self.stop, state='disabled')
        self.btn_stop.pack(side='left', padx=6)
        
        #新增暂停模式
        self.btn_pause = ttk.Button(frm_top, text="暂停显示", command=self.toggle_pause, state='disabled')
        self.btn_pause.pack(side='left')
        
        #新增复位模式
        self.btn_reset = ttk.Button(frm_top, width=12, text="复位", command=self.reset, state='disabled')
        self.btn_reset.pack(side='left', padx=(6, 30))

        #系统时间戳
        self.btn_sysTime = ttk.Button(frm_top, text="展示时间", command=self.systemTime, state='disabled')
        self.btn_sysTime.pack(side='left')
        
        frm_log = ttk.Frame(self)
        frm_log.pack(fill='x', padx=10)
        ttk.Label(frm_log, text="日志保存路径:").pack(side='left')
        #self.log_path_var = tk.StringVar(value=os.getcwd())
        #默认保存路径 
        self.log_path_var = tk.StringVar(value=r"C:\Users\liudayi\Desktop\stm32工程\TEMPLATE_2\log")
        self.ent_log_path = ttk.Entry(frm_log, textvariable=self.log_path_var)
        self.ent_log_path.pack(side='left', fill='x', expand=True, padx=6)
        self.btn_browse = ttk.Button(frm_log, text="选择目录", command=self.browse_folder)
        self.btn_browse.pack(side='left')
        
        #设置串口输出数据格式，字体:Lucida Console \ Consolas \ Courier New，大小
        self.txt_output = scrolledtext.ScrolledText(self, font=("Consolas", 10), bg="#1e1e1e", fg="#f0f0f0", insertbackground="white")
        self.txt_output.pack(fill='both', expand=True, padx=10, pady=10)

        self.txt_output.tag_config('reset', foreground='#d4d4d4')
        self.txt_output.tag_config('red', foreground='#ff4c4c')
        #  #80ff80:亮绿    #50fa7b:浅亮绿
        self.txt_output.tag_config('green', foreground='#50fa7b')
        #  #ffff66:明亮黄  #FFD700:金黄
        self.txt_output.tag_config('yellow', foreground='#ffff66')
        self.txt_output.tag_config('blue', foreground='#569cd6')
        self.txt_output.tag_config('magenta', foreground='#c586c0')
        self.txt_output.tag_config('cyan', foreground='#4ec9b0')
        self.txt_output.tag_config('bold', font=("Consolas", 12, "bold"))

        
        frm_input = ttk.Frame(self)
        frm_input.pack(fill='x', padx=10, pady=(0,10))
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
        global serial_stop

        #获取配置
        port = self.cb_ports.get()
        baud = self.cb_baud.get()
        if not port or not baud:
            messagebox.showwarning("提示", "请选择串口和波特率")
            return

        # 彻底关掉旧串口（先 stop）
        self.stop()

        #一些复位操作
        self.btn_pause.config(text="暂停显示")
        self.btn_pause.config(state='normal')
        self.btn_reset.config(text="复位")
        self.btn_reset.config(state='normal')
        self.btn_sysTime.config(text="展示时间")
        self.btn_sysTime.config(state='normal')

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
        clean_port = port.replace(":", "").replace("/", "").replace("\\", "")
        filename = f"{clean_port}_{date_str}.log"
        path = os.path.join(self.log_path_var.get(), filename)
        try:
            self.logfile = open(path, "a", encoding="utf-8")
        except Exception as e:
            messagebox.showerror("错误", f"日志文件打开失败: {e}")
            self.serial_port.close()
            self.serial_port = None
            return

        self.running = True
        self.reader_thread_running = True  # 避免多次读线程
        self.btn_start.config(state='disabled')
        self.btn_stop.config(state='normal')
        self.print_text(f"[系统] 串口 {port} 已打开，波特率 {baud}\n", 'green')
        #串口数据读取线程
        self.thread = threading.Thread(target=self.read_from_port, daemon=True)
        self.thread.start()
        #串口自动重连线程
        self.monitor_thread = threading.Thread(target=self.monitor_port, daemon=True)        
        self.monitor_thread.start()

        """
        # 野火初始化
        try:

            #self.serial_port.send_break()
            #time.sleep(0.1)
            #self.serial_port.setDTR(False)
            #self.serial_port.setRTS(False)
            #time.sleep(0.1)
            #self.serial_port.setDTR(True)
            #self.serial_port.setRTS(True)
            # 拉低设备会重启
            if self.serial_port.dtr:  # 如果有残留状态就清掉
                self.serial_port.setDTR(False)
            if self.serial_port.rts:
                self.serial_port.setRTS(False)

            self.serial_port = serial.Serial(port, baudrate=baud, timeout=1)
        except Exception as e:
            messagebox.showwarning("警告", f"初始化信号设置异常: {e}")
        """
        """    
        if self.serial_port is None:
            try:
                self.serial_port = serial.Serial(port, baudrate=baud, timeout=1)
            except Exception as e:
                messagebox.showwarning("警告", f"串口打开失败: {e}")
                return
        else:
            if not self.serial_port.is_open:
                try:
                    self.serial_port.open()
                except Exception as e:
                    messagebox.showwarning("警告", f"串口重新打开失败: {e}")
                    return
        """
 
    def read_from_port(self):
        while self.running:
            try:
                raw = self.serial_port.readline()
                if not raw:
                    continue
                #line = raw.decode(errors='ignore').rstrip('\r\n')
                #line = raw.decode(errors='ignore').rstrip('\n')
                line = raw.decode(errors='ignore')
                timestamp = datetime.now().strftime("[%Y-%m-%d %H:%M:%S] ")
                
               
                if self.timeStamp:
                    #self.insert_ansi(timestamp + line + '\n')
                    self.insert_ansi(timestamp + line)
                else:
                    #self.insert_ansi(line + '\n')
                    self.insert_ansi(line)
                """
                if self.timeStamp:
                    self.txt_output.after(0, self.insert_ansi, timestamp + line)
                else:
                    self.txt_output.after(0, self.insert_ansi, line)
                """    
                plain = ANSI_RE.sub('', line)
                self.logfile.write(timestamp + plain + '\n')
                self.logfile.flush()
            except Exception as e:
                self.print_text(f"[错误] 读取串口异常: {e}\n", 'red')
                #self.running = False
                try:
                    if self.serial_port and self.serial_port.is_open:
                        self.serial_port.close()
                except:
                    pass
                try:
                    del self.serial_port
                except:
                    pass
                self.serial_port = None
                self.reader_thread_running = False  # 标记读线程已停
                break

    def monitor_port(self):
        """后台监测线程：定期探测串口状态"""
        while self.running:
            if self.serial_port:
                # 检查串口是否被 OS 拔掉
                available_ports = self.get_ports()
                current_port = self.cb_ports.get()
                if current_port not in available_ports:
                    self.print_text("[系统] 端口在系统里消失，准备重连\n", 'yellow')
                    try:
                        self.serial_port.close()
                    except:
                        pass
                    self.serial_port = None
                    self.reader_thread_running = False
            else:
                # 尝试自动重连
                port = self.cb_ports.get()
                baud = self.cb_baud.get()
                ports = self.get_ports()
                if port in ports:
                    try:
                        self.serial_port = serial.Serial(port, int(baud), timeout=0.5)
                        self.serial_port.setDTR(False)
                        self.serial_port.setRTS(False)
                        self.print_text(f"[系统] 自动重连 {port} 成功\n", 'green')
                        self.reader_thread_running = True
                        self.thread = threading.Thread(target=self.read_from_port, daemon=True)
                        self.thread.start()
                    except Exception as e:
                        self.print_text(f"[错误] 自动重连失败: {e}\n", 'red')
            time.sleep(1)


    def stop(self):
        self.running = False
        self.reader_thread_running = False
        if self.serial_port and self.serial_port.is_open:
            try:
                global serial_stop
                self.serial_port.close()
                serial_stop = 'manual'
            except:
                pass
        try:
            del self.serial_port
        except:
            pass
        self.serial_port = None

        if self.logfile:
            self.logfile.close()
            self.logfile = None

        self.btn_start.config(state='normal')
        #串口关闭后禁止配置的按钮
        self.btn_stop.config(state='disabled')
        self.btn_pause.config(state='disabled')
        self.btn_reset.config(state='disabled')
        self.btn_sysTime.config(state='disabled')
        
        self.print_text("[系统] 串口已关闭\n", 'yellow')
 
    def toggle_pause(self):
        self.paused = not self.paused
        if self.paused:
            self.btn_pause.config(text="恢复显示")
            #self.print_text("[系统] 已暂停终端显示，日志仍在记录\n", 'yellow')
        else:
            self.btn_pause.config(text="暂停显示")
            #self.print_text("[系统] 已恢复终端显示\n", 'green')
    
    def reset(self):
        self.serial_port.setDTR(False)
        self.serial_port.setRTS(False)
        self.print_text("[系统] reset\n", 'yellow')
    def systemTime(self):
        self.timeStamp = not self.timeStamp
        if self.timeStamp:
            self.btn_sysTime.config(text="不展示时间")
        else:
            self.btn_sysTime.config(text="展示时间")
    """
    def insert_ansi(self, text):
        if self.paused:
            return
        pos = 0
        tag = 'reset'
        for m in ANSI_RE.finditer(text):
            start, end = m.span()
            self.txt_output.insert(tk.END, text[pos:start], tag)
            code = m.group(1)
            tag = self.ansi_code_to_tag(code)
            pos = end
        self.txt_output.insert(tk.END, text[pos:], tag)
        self.txt_output.see(tk.END)
    """

    def insert_ansi(self, text):
        if self.paused:
            return

        pos = 0
        tag = 'reset'

        for m in SPLIT_RE.finditer(text):
            start, end = m.span()
            chunk = text[pos:start]
            sep = m.group()

            if chunk:
                self._insert_chunk(chunk, tag)

            if sep == '\r\n' or sep == '\n':
                self.txt_output.insert(tk.END, '\n', tag)
            elif sep == '\r':
                # 删除最后一行
                line_start = self.txt_output.index('end - 1c linestart')
                line_end = self.txt_output.index('end - 1c lineend')
                # 加上延迟，否则只能看到最终的打印
                time.sleep(0.1)
                self.txt_output.delete(line_start, line_end)
            
            pos = end

        # 处理剩余尾巴
        if pos < len(text):
            self._insert_chunk(text[pos:], tag)

        self.txt_output.see(tk.END)

    def _insert_chunk(self, chunk, tag):
        pos = 0
        while pos < len(chunk):
            m = ANSI_RE.search(chunk, pos)
            if m:
                start, end = m.span()
                self.txt_output.insert(tk.END, chunk[pos:start], tag)
                code = m.group(1)
                tag = self.ansi_code_to_tag(code)
                pos = end
            else:
                self.txt_output.insert(tk.END, chunk[pos:], tag)
                break
    """
    def insert_ansi(self, text):
        if self.paused:
            return

        # 把这串拆成多条：只保留最后一条覆盖用
        if '\r' in text:
            last_line = text.split('\r')[-1]
        else:
            last_line = text

        # 保证没有多余换行
        last_line = last_line.replace('\n', '')

        # 删除最后一行
        last_line_start = self.txt_output.index('end - 1c linestart')
        last_line_end = self.txt_output.index('end - 1c lineend')
        self.txt_output.delete(last_line_start, last_line_end)

        # 插入最后一条
        pos = 0
        tag = 'reset'
        while pos < len(last_line):
            m = ANSI_RE.search(last_line, pos)
            if m:
                start, end = m.span()
                self.txt_output.insert(tk.END, last_line[pos:start], tag)
                code = m.group(1)
                tag = self.ansi_code_to_tag(code)
                pos = end
            else:
                self.txt_output.insert(tk.END, last_line[pos:], tag)
                break

        self.txt_output.see(tk.END)
    """
    def ansi_code_to_tag(self, code):
        if code in ('0', '39'):
            return 'reset'
        elif code in ('1;31', '31'):
            return 'red'
        elif code in ('1;32', '32'):
            return 'green'
        elif code in ('1;33', '33'):
            return 'yellow'
        elif code in ('1;34', '34'):
            return 'blue'
        elif code in ('1;35', '35'):
            return 'magenta'
        elif code in ('1;36', '36'):
            return 'cyan'
        else:
            return 'reset'

    def send_command(self, event=None):
        if not self.serial_port or not self.serial_port.is_open:
            messagebox.showwarning("提示", "串口未打开")
            return
        cmd = self.ent_input.get().strip()
        if not cmd:
            return
        try:
            self.serial_port.write((cmd + '\r\n').encode())
            #self.print_text(f"[TX] {cmd}\n", 'blue')
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
