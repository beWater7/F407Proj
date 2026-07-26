import ctypes
import tkinter as tk
from tkinter import ttk, scrolledtext, messagebox, filedialog
import serial
import serial.tools.list_ports
import threading
import os
import re
import time
import sys
import logging
import struct
from datetime import datetime

# DPI Aware 提高清晰度
try:
    ctypes.windll.shcore.SetProcessDpiAwareness(1)
except:
    pass

ANSI_RE = re.compile(r'\x1b\[([0-9;]+)m')
SPLIT_RE = re.compile(r'\r\n|\n|\r')
# 设置编码
sys.stdout.reconfigure(encoding='utf-8')

SOH = 0x01
STX = 0x02
EOT = 0x04
ACK = 0x06
NAK = 0x15
CAN = 0x18
CRC16 = ord('C')
PACKET_SIZE = 128
PACKET_1K_SIZE = 1024

def crc16(data: bytes):
    crc = 0
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = (crc << 1) ^ 0x1021
            else:
                crc <<= 1
            crc &= 0xFFFF
    return crc

class YmodemSender:
    def __init__(self, ser: serial.Serial, gui=None):
        self.ser = ser
        self.gui = gui  # 可选，打印GUI日志

    def _log(self, msg, color='reset'):
        if self.gui:
            self.gui.print_text(f"[YMODEM] {msg}\n", color)
        else:
            print(f"[YMODEM] {msg}")

    def send_file(self, filepath):
        filename = os.path.basename(filepath)
        filesize = os.path.getsize(filepath)
        self._log(f"发送文件: {filename} ({filesize} bytes)")

        # 等待目标发出 'C'
        self._wait_for_char(CRC16)

        # 发送文件头包
        self._send_header(filename, filesize)

        # 等待ACK + 'C'
        if not self._wait_for_ack_sequence([ACK, CRC16]):
            self._log("文件头应答错误", 'red')
            return False

        # 发送文件数据
        with open(filepath, 'rb') as f:
            pkt_no = 1
            while True:
                chunk = f.read(PACKET_1K_SIZE)
                if not chunk:
                    break
                self._send_data_packet(pkt_no, chunk)
                pkt_no += 1
                if not self._wait_for_ack():
                    self._log("数据包ACK超时", 'red')
                    return False
                self._log(f"发送进度: {pkt_no * PACKET_1K_SIZE}/{filesize}")

        # 发送EOT
        self._log("发送EOT")
        self.ser.write(bytes([EOT]))
        self._wait_for_ack()

        # STM32 还会回发 'C' 再发空包
        self._wait_for_char(CRC16)
        self._send_empty_packet()
        self._wait_for_ack()
        self._log("文件发送完成", 'green')
        return True

    def _send_header(self, filename, filesize):
        packet = bytearray([SOH])
        packet += bytes([0x00, 0xFF])
        body = f"{filename}\0{filesize}".encode()
        body += bytes(PACKET_SIZE - len(body))
        crc = crc16(body)
        packet += body
        packet += struct.pack('>H', crc)
        self.ser.write(packet)
        self._log("文件头包已发送")

    def _send_data_packet(self, pkt_no, data):
        packet = bytearray([STX])
        seq = pkt_no & 0xFF
        packet += bytes([seq, 0xFF - seq])
        if len(data) < PACKET_1K_SIZE:
            data += bytes(PACKET_1K_SIZE - len(data))
        crc = crc16(data)
        packet += data
        packet += struct.pack('>H', crc)
        self.ser.write(packet)

    def _send_empty_packet(self):
        packet = bytearray([SOH, 0x00, 0xFF])
        body = bytes(PACKET_SIZE)
        crc = crc16(body)
        packet += body + struct.pack('>H', crc)
        self.ser.write(packet)

    def _wait_for_char(self, ch, timeout=10):
        start = time.time()
        while time.time() - start < timeout:
            c = self.ser.read(1)
            if c and c[0] == ch:
                return True
        return False

    def _wait_for_ack(self, timeout=5):
        return self._wait_for_char(ACK, timeout)

    def _wait_for_ack_sequence(self, seq, timeout=10):
        buf = []
        start = time.time()
        while time.time() - start < timeout:
            c = self.ser.read(1)
            if not c:
                continue
            buf.append(c[0])
            if buf[-len(seq):] == seq:
                return True
        return False


#sys.stdout.reconfigure(encoding='gb2312')
class SerialMonitor(tk.Tk):
    def __init__(self):
        global serial_stop
        serial_stop = 'reset'
        super().__init__()
        self.title("串口监控器（带系统时间戳 + ANSI彩色 + 野火初始化）")
        self.geometry("1400x1200")
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
        #self.prompt_prefix = "[mcu@board]#"
        self.prompt_prefix = "STM32F407 >"
        self.prompt_prefix_boot = "BOOT#"
        #self.prompt_prefix_line = "\n"
        self.prompt_prefix_list = ["STM32F407 >", "BOOT#", "@STM32:", "..."]

        self.create_widgets()

        self.txt_output.bind("<Key>", self.on_key)
        self.txt_output.bind("<Return>", self.on_enter)
        self.txt_output.bind("<BackSpace>", self._on_backspace) # 退格
        self.txt_output.bind("<Delete>", self._on_delete)       # 删除键

        self.protocol("WM_DELETE_WINDOW", self.on_close)
        #self.txt_output.delete(line_start, line_end)

    def _get_editable_start(self):
        """返回最后一行可编辑区的起始列号"""
        last_line_start = self.txt_output.index("end-1c linestart")
        last_line_text = self.txt_output.get(last_line_start, "end-1c")

        # 假设 self.prompt_prefix_list 是一个列表，包含所有可能的提示符
        if hasattr(self, 'prompt_prefix_list'):
            for prefix in self.prompt_prefix_list:
                if last_line_text.startswith(prefix):
                    return len(prefix)
                elif last_line_text.endswith(prefix):
                    return len(last_line_text)
        # 没匹配上任何提示符，则默认可编辑区从开头开始
        return 0

    def _on_backspace(self, event):
        line, col = map(int, self.txt_output.index("insert").split('.'))
        last_line = int(self.txt_output.index("end-1c").split('.')[0])
        editable_start = self._get_editable_start()
        # 不在最后一行或光标在可编辑区前面，阻止删除
        if line != last_line or col <= editable_start:
            return "break"

    def _on_delete(self, event):
        # 光标不在最后一行，阻止删除
        line = int(self.txt_output.index("insert").split('.')[0])
        last_line = int(self.txt_output.index("end-1c").split('.')[0])
        if line != last_line:
            return "break"

    def on_key(self, event):
        """
        # 获取按下的键对应的字符
        if event.char:
            # 普通可见字符和空格
            #self.serial_port.write(event.char.encode(errors='ignore'))
            self.txt_output.insert("insert", event.char)
            return "break"
        else:
            if event.keysym == "Up":
                # Mobaxterm里，向上箭头对应转义序列 ESC [ A
                up_seq = '\x1b[A'
                print(f"[TX] 发送向上键序列: {repr(up_seq)}")
                self.serial_port.write(up_seq.encode())
                return "break"  # 阻止默认事件
            elif event.keysym == "Down":
                self.serial_port.write(b'\x1b[B')
                return "break"
            elif event.keysym.lower() == "l" and (event.state & 0x4):  # Ctrl+L
                self.serial_port.write(b'\x1b[2J\x1b[H')
                self.clear_screen()
                return "break" 
            elif event.keysym == "Return":
                self.serial_port.write(b'\r')  # 或 b'\r\n' 根据设备协议
                return "break"
            elif event.keysym == "Tab":
                self.serial_port.write((b'\t').encode())
                return "break"  # 阻止 Tkinter 默认焦点切换
            elif event.keysym == "BackSpace":
                print(f"[TX] 删除")
                # 删除本地光标前字符
                self.txt_output.delete("insert-1c")
                return "break"
        """
        # 先处理特殊键
        if event.keysym == "Up":
            up_seq = '\x1b[A'
            self.serial_port.write(up_seq.encode())
            return "break"

        elif event.keysym == "Down":
            self.serial_port.write(b'\x1b[B')
            return "break"

        elif event.keysym.lower() == "l" and (event.state & 0x4):  # Ctrl+L
            self.serial_port.write(b'\x1b[2J\x1b[H')
            self.clear_screen()
            return "break"

        elif event.keysym == "Return":
            #self.serial_port.write(b'\r')  # 或 b'\r\n'
            return "break"

        elif event.keysym == "Tab":
            self.serial_port.write((b'\t').encode())
            return "break"

        elif event.keysym == "BackSpace":
            if self.txt_output.index("insert") != self.txt_output.index("insert linestart"):
                self.txt_output.delete("insert-1c", "insert")
            return "break"

        # 其他情况：普通可见字符
        elif event.char:
            self.txt_output.insert("insert", event.char)
            #self.serial_port.write(event.char.encode(errors='ignore'))
            return "break"
        # 直接允许光标自由移动，不限制提示符
        return None

    def clear_screen(self):
        self.txt_output.delete('1.0', tk.END)

    def on_enter(self, event):
        line_start = self.txt_output.index("insert linestart")
        line_end = self.txt_output.index("insert lineend")
        line_text = self.txt_output.get(line_start, line_end)

        for prefix in self.prompt_prefix_list:
            if line_text.startswith(prefix):
                line_text = line_text[len(prefix):]
                break  # 找到匹配的就结束循环
            elif prefix in line_text:
                # 如果在中间或结尾找到 prefix，就取 prefix 后面的内容
                pos = line_text.rfind(prefix)
                line_text = line_text[pos + len(prefix):]
                break
        #else:
        #    self.serial_port.write(('\r\n').encode())
        #    return "break"

        # 构造一次性发送的字节串（数据 + 行结束符），**只写一次**
        #payload = (line_text + self.line_ending).encode(errors='ignore')

        # debug：打印到底发了什么字节（用 repr 看具体的 \r \n）
        #print("[TX] ->", repr(line_text))

        # 发送给 MCU
        self.serial_port.write((line_text+'\r\n').encode())

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
        frm_log.pack(fill='x', padx=10, pady=5)
        ttk.Label(frm_log, text="日志保存路径:").pack(side='left')
        #self.log_path_var = tk.StringVar(value=os.getcwd())
        #默认保存路径 
        self.log_path_var = tk.StringVar(value=r"E:\stm32-project\TEMPLATE_2\log")
        self.ent_log_path = ttk.Entry(frm_log, textvariable=self.log_path_var)
        self.ent_log_path.pack(side='left', fill='x', expand=True, padx=6)
        self.btn_browse = ttk.Button(frm_log, text="选择目录", command=self.browse_folder)
        self.btn_browse.pack(side='left', padx=(0,5))

        # 新增：YMODEM上传按钮
        self.btn_ymodem = ttk.Button(frm_log, text="YMODEM上传", command=self.send_file_via_ymodem)
        self.btn_ymodem.pack(side='left')

        #设置串口输出数据格式，字体:Lucida Console \ Consolas \ Courier New，大小
        self.txt_output = scrolledtext.ScrolledText(self, font=("Consolas", 10), bg="#1e1e1e", fg="#f0f0f0", insertbackground="white")
        self.txt_output.pack(fill='both', expand=True, padx=10, pady=10)
        
        # 默认字体--白色        
        self.txt_output.tag_config('reset', foreground='#ffffff')
        self.txt_output.tag_config('red', foreground='#ff4c4c')
        #  #80ff80:亮绿    #50fa7b:浅亮绿
        self.txt_output.tag_config('green', foreground='#50fa7b')
        #  #ffff66:明亮黄  #FFD700:金黄
        self.txt_output.tag_config('yellow', foreground='#ffff66')
        self.txt_output.tag_config('blue', foreground='#569cd6')
        self.txt_output.tag_config('magenta', foreground='#c586c0')
        self.txt_output.tag_config('cyan', foreground='#4ec9b0')
        self.txt_output.tag_config('bold', font=("Consolas", 12, "bold"))

        # 右键菜单
        self.context_menu = tk.Menu(self.txt_output, tearoff=0)
        self.context_menu.add_command(label="复制", command=lambda: self.txt_output.event_generate("<<Copy>>"))
        self.context_menu.add_command(label="粘贴", command=lambda: self.txt_output.event_generate("<<Paste>>"))
        self.context_menu.add_command(label="全选", command=lambda: self.txt_output.tag_add("sel", "1.0", "end"))

        # 绑定右键点击事件
        self.txt_output.bind("<Button-3>", self.show_context_menu)

        frm_input = ttk.Frame(self)
        frm_input.pack(fill='x', padx=10, pady=(0,10))
        self.ent_input = ttk.Entry(frm_input, font=("Consolas", 12))
        self.ent_input.pack(side='left', fill='x', expand=True)
        self.ent_input.bind('<Return>', self.send_command)
        self.ent_input.bind("<Up>", self.history_up)
        self.ent_input.bind("<Down>", self.history_down)
        # 历史命令缓存
        self.cmd_history = []
        self.history_index = -1

        self.btn_send = ttk.Button(frm_input, text="发送", command=self.send_command)
        self.btn_send.pack(side='left', padx=6)

    def send_file_via_ymodem(self):
        if not self.serial_port or not self.serial_port.is_open:
            messagebox.showwarning("警告", "请先打开串口！")
            return

        filepath = filedialog.askopenfilename(title="选择要上传的文件")
        if not filepath:
            return
        self.ser.write(b"IOT")

        sender = YmodemSender(self.serial_port, gui=self)
        success = sender.send_file(filepath)
        if success:
            messagebox.showinfo("YMODEM", "文件上传成功！")
        else:
            messagebox.showerror("YMODEM", "文件上传失败，请查看日志。")

    def show_context_menu(self, event):
        try:
            self.context_menu.tk_popup(event.x_root, event.y_root)
        finally:
            self.context_menu.grab_release()

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
                line = raw.decode('utf-8', errors='ignore')
                timestamp = datetime.now().strftime("[%Y-%m-%d %H:%M:%S] ")

                if self.timeStamp:
                    #self.insert_ansi(timestamp + line + '\n')
                    #self.insert_ansi(timestamp + line)
                    self.txt_output.after(0, self.insert_ansi, timestamp + line)
                else:
                    #self.insert_ansi(line + '\n')
                    #self.insert_ansi(line)
                    self.txt_output.after(0, self.insert_ansi, line)
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
            # 保存到历史
            self.cmd_history.append(cmd)
            self.history_index = len(self.cmd_history)  # 重置到最新位置
            #self.print_text(f"[TX] {cmd}\n", 'blue')
            self.ent_input.delete(0, tk.END)
        except Exception as e:
            self.print_text(f"[错误] 发送失败: {e}\n", 'red')

    def history_up(self, event=None):
        if self.cmd_history and self.history_index > 0:
            self.history_index -= 1
            self.ent_input.delete(0, tk.END)
            self.ent_input.insert(0, self.cmd_history[self.history_index])

    def history_down(self, event=None):
        if self.cmd_history and self.history_index < len(self.cmd_history) - 1:
            self.history_index += 1
            self.ent_input.delete(0, tk.END)
            self.ent_input.insert(0, self.cmd_history[self.history_index])
        else:
            # 已经到最新一条后，清空输入框
            self.history_index = len(self.cmd_history)
            self.ent_input.delete(0, tk.END)

    def print_text(self, text, tag='reset'):
        self.txt_output.insert(tk.END, text, tag)
        self.txt_output.see(tk.END)

    def on_close(self):
        self.stop()
        self.destroy()

if __name__ == "__main__":
    app = SerialMonitor()
    app.mainloop()
