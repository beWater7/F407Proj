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
        self.txt_output = scrolledtext.ScrolledText(self, font=("Consolas", 10), bg="#1e1e1e", fg="#d4d4d4", insertbackground="white")
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
                #line = raw.decode(errors='ignore')
                line = raw.decode(errors='ignore').replace('\r', '\r\n')
                timestamp = datetime.now().strftime("[%Y-%m-%d %H:%M:%S] ")
                
                """
                if self.timeStamp:
                    self.insert_ansi(timestamp + line + '\n')
                else:
                    self.insert_ansi(line + '\n')
                """
                """
                if self.timeStamp:
                    self.txt_output.after(0, self.insert_ansi, timestamp + line)
                else:
                    self.txt_output.after(0, self.insert_ansi, line)
                """
                if self.timeStamp:
                    display_text = timestamp + line + '\n'
                    #display_text = timestamp + line
                else:
                    display_text = line + '\n'
                    #display_text = line
                # 使用 after 确保在主线程中执行GUI更新
                self.txt_output.after(0, self.insert_ansi, display_text)
                
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
    
    """
    def read_from_port(self):
        while self.running:
            try:
                raw = self.serial_port.readline()
                if not raw:
                    continue
                # 保留所有控制字符（包括\r）
                line = raw.decode(errors='ignore')
                
                if self.timeStamp:
                    timestamp = datetime.now().strftime("[%Y-%m-%d %H:%M:%S] ")
                    self._process_serial_data(timestamp + line)
                else:
                    self._process_serial_data(line)
                
                # 日志处理（移除ANSI和\r）
                plain = ANSI_RE.sub('', line.replace('\r', ''))
                if plain.strip():  # 避免写入空行
                    self.logfile.write(datetime.now().strftime("[%Y-%m-%d %H:%M:%S] ") + plain)
                    self.logfile.flush()
                    
            except Exception as e:
                self.print_text(f"[错误] 读取异常: {e}\n", 'red')
                # ... 错误处理代码保持不变 ...
    
    def _process_serial_data(self, data):
        #处理串口数据，支持\r覆盖和ANSI颜色
        if not hasattr(self, 'serial_buffer'):
            self.serial_buffer = ""
        
        self.serial_buffer += data
        
        # 按行处理
        while '\n' in self.serial_buffer:
            line, self.serial_buffer = self.serial_buffer.split('\n', 1)
            self._handle_line_with_cr(line + '\n')
        
        # 处理剩余内容（无换行符的）
        if self.serial_buffer:
            self._handle_line_with_cr(self.serial_buffer)
            self.serial_buffer = ""

    def _handle_line_with_cr(self, line):
        #处理包含\r的行
        # 分割所有\r片段
        segments = line.split('\r')
        final_content = segments[-1]  # 最后一段是最终显示内容
        
        # 如果有\r需要先清除行
        if len(segments) > 1:
            self._clear_current_line()
        
        # 插入处理后的内容
        self._insert_with_ansi(final_content)

    def _clear_current_line(self):
        #清除当前行内容
        if hasattr(self, 'current_line_start'):
            line_end = self.txt_output.index(f"{self.current_line_start} lineend")
            self.txt_output.delete(self.current_line_start, line_end)

    def _insert_with_ansi(self, text):
        #插入带ANSI颜色的文本
        if self.paused:
            return
        
        # 记录新行起始位置
        if '\n' in text or not hasattr(self, 'current_line_start'):
            self.current_line_start = self.txt_output.index("end-1c linestart")
        
        # ANSI颜色处理
        pos = 0
        current_tag = 'reset'
        while pos < len(text):
            match = ANSI_RE.search(text, pos)
            if match:
                # 插入普通文本
                self.txt_output.insert("end", text[pos:match.start()], current_tag)
                # 更新标签
                code = match.group(1)
                current_tag = self.ansi_code_to_tag(code)
                pos = match.end()
            else:
                self.txt_output.insert("end", text[pos:], current_tag)
                break
        
        self.txt_output.see("end")
    """
    def monitor_port(self):
        #后台监测线程：定期探测串口状态
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
    # 原始版
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
    """
    def insert_ansi(self, text):
        if self.paused:
            return

        pos = 0
        tag = 'reset'

        while pos < len(text):
            # 优先找 \r 或 ANSI
            rpos = text.find('\r', pos)
            m = ANSI_RE.search(text, pos)

            # 谁先出现？
            next_pos = len(text)
            next_event = None

            if rpos != -1 and (m is None or rpos < m.start()):
                next_pos = rpos
                next_event = 'CR'
            elif m:
                next_pos = m.start()
                next_event = 'ANSI'

            # 插入到下一个事件前
            if next_pos > pos:
                chunk = text[pos:next_pos]
                self.txt_output.insert(tk.END, chunk, tag)

            # 处理事件
            if next_event == 'CR':
                # 回车，删除当前行的内容
                if not hasattr(self, 'current_line_start'):
                    self.current_line_start = self.txt_output.index('insert linestart')
                line_start = self.current_line_start
                self.txt_output.delete(line_start, 'insert lineend')
                pos = next_pos + 1

            elif next_event == 'ANSI':
                # ANSI 转换
                code = m.group(1)
                tag = self.ansi_code_to_tag(code)
                pos = m.end()

            else:
                break

        # 剩下的
        if pos < len(text):
            self.txt_output.insert(tk.END, text[pos:], tag)

        # 每次如果出现 \n，则记录新行起点
        if '\n' in text:
            self.current_line_start = self.txt_output.index(tk.END)

        self.txt_output.see(tk.END)
    """
    """
    def insert_ansi(self, text):
        if self.paused:
            return

        pos = 0
        tag = 'reset'

        while pos < len(text):
            # 优先找 \r 或 ANSI
            rpos = text.find('\r', pos)
            m = ANSI_RE.search(text, pos)
            
            # 谁先出现？
            next_pos = len(text)
            next_event = None
            
            if rpos != -1 and (m is None or rpos < m.start()):
                next_pos = rpos
                next_event = 'CR'
            elif m:
                next_pos = m.start()
                next_event = 'ANSI'
            
            # 插入到下一个事件前的文本
            if next_pos > pos:
                chunk = text[pos:next_pos]
                # 根据是否在覆盖模式决定插入位置
                if hasattr(self, 'overwrite_mode') and self.overwrite_mode:
                    self._insert_with_overwrite(chunk, tag)
                else:
                    self.txt_output.insert(tk.END, chunk, tag)
            
            # 处理事件
            if next_event == 'CR':
                # 回车，准备覆盖当前行
                self._handle_carriage_return()
                pos = next_pos + 1
            elif next_event == 'ANSI':
                # ANSI 转换
                code = m.group(1)
                tag = self.ansi_code_to_tag(code)
                pos = m.end()
            else:
                break

        # 处理剩余文本
        if pos < len(text):
            remaining = text[pos:]
            if hasattr(self, 'overwrite_mode') and self.overwrite_mode:
                self._insert_with_overwrite(remaining, tag)
            else:
                self.txt_output.insert(tk.END, remaining, tag)
            
            # 如果剩余文本包含换行，更新行起始位置
            if '\n' in remaining:
                #self._update_line_start()
                self._reset_overwrite_mode()

        self.txt_output.see(tk.END)
    """
    def insert_ansi(self, text):
        pos = 0
        while pos < len(text):
            m = ANSI_RE.search(text, pos)
            cr = text.find('\r', pos)

            # 找谁先出现
            next_pos = len(text)
            next_event = None

            if cr != -1 and (m is None or cr < m.start()):
                next_pos = cr
                next_event = "CR"
            elif m:
                next_pos = m.start()
                next_event = "ANSI"

            if next_pos > pos:
                chunk = text[pos:next_pos]
                self.text.insert("end", chunk, self.current_tag)

            if next_event == "CR":
                line_start = self.text.index("insert linestart")
                self.text.delete(line_start, "insert lineend")
                pos = next_pos + 1
            elif next_event == "ANSI":
                code = m.group(1)
                self.current_tag = self.ansi_code_to_tag(code)
                pos = m.end()
            else:
                break

        if pos < len(text):
            self.text.insert("end", text[pos:], self.current_tag)

        self.text.see("end")
    def _handle_carriage_return(self):
        """处理回车符，设置覆盖模式"""
        # 获取当前文本末尾位置
        end_pos = self.txt_output.index(tk.END + "-1c")  # END指向的是下一行开头，减1字符得到当前行末尾
        
        # 找到当前行的开始位置
        line_start = self.txt_output.index(f"{end_pos} linestart")
        
        # 记录当前行起始位置和覆盖模式
        self.current_line_start = line_start
        self.overwrite_mode = True
        self.overwrite_pos = line_start

    def _update_line_start(self):
        #更新行起始位置
        # 找到最后一个换行符后的位置
        end_pos = self.txt_output.index(tk.END + "-1c")
        last_line_start = self.txt_output.index(f"{end_pos} linestart")
        self.current_line_start = last_line_start
        self.overwrite_mode = False

    def _insert_with_overwrite(self, text, tag):
        #覆盖模式插入文本
        if not hasattr(self, 'overwrite_pos'):
            self.overwrite_pos = self.txt_output.index(tk.END)
        
        # 处理换行符
        if '\n' in text:
            parts = text.split('\n', 1)
            before_newline = parts[0]
            after_newline = parts[1] if len(parts) > 1 else ''
            
            # 处理换行前的内容
            if before_newline:
                self._overwrite_text(before_newline, tag)
            
            # 插入换行符并重置覆盖模式
            self.txt_output.insert(self.overwrite_pos, '\n', tag)
            self._reset_overwrite_mode()
            
            # 处理换行后的内容
            if after_newline:
                self.txt_output.insert(tk.END, after_newline, tag)
        else:
            # 没有换行符，直接覆盖
            self._overwrite_text(text, tag)

    def _overwrite_text(self, text, tag):
        #执行实际的文本覆盖
        if not text:
            return
        
        # 计算覆盖范围
        overwrite_start = self.overwrite_pos
        overwrite_end = self.txt_output.index(f"{overwrite_start}+{len(text)}c")
        line_end = self.txt_output.index(f"{self.current_line_start} lineend")
        
        # 确保不超过行尾
        if self.txt_output.compare(overwrite_end, '>', line_end):
            overwrite_end = line_end
        
        # 删除原文本并插入新文本
        self.txt_output.delete(overwrite_start, overwrite_end)
        self.txt_output.insert(overwrite_start, text, tag)
        
        # 更新覆盖位置
        self.overwrite_pos = self.txt_output.index(f"{overwrite_start}+{len(text)}c")

    def _reset_overwrite_mode(self):
        #重置覆盖模式
        self.overwrite_mode = False
        self.overwrite_pos = None

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
