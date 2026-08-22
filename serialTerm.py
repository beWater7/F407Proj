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

ANSI_RE = re.compile(r'\x1b\[([0-9;]*)m')
# 包尾可能只收到 ESC / ESC[ / ESC[1;33 半截 CSI，需跨包拼接
ANSI_PARTIAL_RE = re.compile(r'\x1b(?:\[[0-9;]*)?$')
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

    def _log(self, msg, color='reset', newline=True):
        suffix = '\n' if newline else ''
        if self.gui:
            self.gui.print_text(f"[YMODEM] {msg}{suffix}", color)
        else:
            end = '\n' if newline else ''
            print(f"[YMODEM] {msg}", end=end, flush=True)

    def _progress(self, sent, total):
        """同一行刷新进度条（Tk / 终端）"""
        if total <= 0:
            total = 1
        done = min(sent, total)
        pct = min(100, done * 100 // total)
        bar_len = 40
        filled = bar_len * pct // 100
        bar = '#' * filled + '-' * (bar_len - filled)
        line = f"[{bar}] {pct:3d}%  {done}/{total}"
        if self.gui:
            self.gui.after(0, lambda l=line: self.gui.update_ymodem_progress(l))
        else:
            print(f"\r[YMODEM] {line}", end='', flush=True)

    def send_file(self, filepath, already_got_c=False):
        filename = os.path.basename(filepath)
        filesize = os.path.getsize(filepath)
        self._log(f"发送文件: {filename} ({filesize} bytes)")

        # 等待目标发出 'C'（可跳过普通文本）
        if not already_got_c:
            if not self._wait_for_char(CRC16, timeout=15):
                self._log("等待接收端 'C' 超时", 'red')
                return False

        # 发送文件头包
        self._send_header(filename, filesize)

        # 等待 ACK + 'C'（头包后 boot 会擦 APP 扇区，可能要数秒）
        if not self._wait_for_ack_sequence([ACK, CRC16], timeout=30):
            self._log("文件头应答错误", 'red')
            return False

        # 发送文件数据
        sent = 0
        if self.gui:
            self.gui.after(0, self.gui.begin_ymodem_progress)
        with open(filepath, 'rb') as f:
            pkt_no = 1
            while True:
                chunk = f.read(PACKET_1K_SIZE)
                if not chunk:
                    break
                self._send_data_packet(pkt_no, chunk)
                if not self._wait_for_ack(timeout=10):
                    self._log("数据包ACK超时", 'red')
                    return False
                sent += len(chunk)
                pkt_no += 1
                self._progress(sent, filesize)

        if self.gui:
            self.gui.after(0, lambda: self.gui.end_ymodem_progress(True))
        elif filesize:
            print()  # 进度条后换行

        # EOT：若先收到 NAK，再发第二次 EOT（兼容常见实现）
        self._log("发送EOT")
        self.ser.write(bytes([EOT]))
        self.ser.flush()
        resp = self._wait_for_ack_or_nak(timeout=5)
        if resp == NAK:
            self.ser.write(bytes([EOT]))
            self.ser.flush()
            if not self._wait_for_ack(timeout=5):
                self._log("第二次EOT未收到ACK", 'red')
                return False
        elif resp != ACK:
            self._log("EOT未收到ACK", 'red')
            return False

        # 空包结束会话（失败时重试，兼容 USB 串口拆包）
        if not self._wait_for_char(CRC16, timeout=10):
            self._log("等待结束会话 'C' 超时", 'red')
            return False
        for attempt in range(1, 4):
            self._send_empty_packet()
            try:
                self.ser.flush()
            except Exception:
                pass
            if self._wait_for_ack(timeout=5):
                break
            if attempt < 3:
                self._log(f"空包ACK超时，重试 ({attempt}/3)...", 'yellow')
                # 板端失败时会回 'C'，清掉后重发空包
                self._drain_for_char(CRC16, timeout=1.0)
            else:
                self._log("空包ACK超时", 'red')
                return False
        self._log("文件发送完成", 'green')
        return True

    def _drain_for_char(self, ch, timeout=1.0):
        """短时等待某个控制字节（如 'C'），丢弃其它噪声。"""
        start = time.time()
        while time.time() - start < timeout:
            c = self.ser.read(1)
            if not c:
                continue
            if c[0] == ch:
                return True
            if c[0] == CAN:
                return False
        return False

    def _send_header(self, filename, filesize):
        packet = bytearray([SOH])
        packet += bytes([0x00, 0xFF])
        body = f"{filename}\0{filesize}".encode()
        if len(body) > PACKET_SIZE:
            body = body[:PACKET_SIZE]
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
        data = bytearray(data)
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
        """等待目标控制字节，跳过普通可打印文本。"""
        start = time.time()
        while time.time() - start < timeout:
            c = self.ser.read(1)
            if not c:
                continue
            b = c[0]
            if b == ch:
                return True
            # 发送端取消
            if b == CAN:
                return False
        return False

    def _wait_for_ack(self, timeout=5):
        return self._wait_for_char(ACK, timeout)

    def _wait_for_ack_or_nak(self, timeout=5):
        start = time.time()
        while time.time() - start < timeout:
            c = self.ser.read(1)
            if not c:
                continue
            b = c[0]
            if b in (ACK, NAK):
                return b
            if b == CAN:
                return None
        return None

    def _wait_for_ack_sequence(self, seq, timeout=10):
        buf = []
        start = time.time()
        while time.time() - start < timeout:
            c = self.ser.read(1)
            if not c:
                continue
            buf.append(c[0])
            if len(buf) > 64:
                buf = buf[-64:]
            if buf[-len(seq):] == seq:
                return True
            if c[0] == CAN:
                return False
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
        self._pending_cr = False  # 跨包的 \r，避免把 \r\n 拆开后误清行
        self._ansi_pending = ''   # 跨包未完成的 ANSI CSI（避免露出 [1;33m）
        self._current_ansi_tag = 'reset'  # 跨包保持当前颜色
        self.reader_paused = False  # YMODEM 期间暂停读线程，避免抢 ACK/C
        self._ymodem_busy = False
        # 打开串口时是否用 DTR/RTS 脉冲复位板子（默认关：关开串口不再误复位）
        self.reset_on_open = tk.BooleanVar(value=False)

        # 111    
        #self.prompt_prefix = "[mcu@board]#"
        self.prompt_prefix = "STM32F407 >"
        self.prompt_prefix_boot = "BOOT#"
        # 仅用完整提示符做前缀匹配；不要放 "..."，会误伤倒计时行
        self.prompt_prefix_list = ["STM32F407 >", "BOOT#", "@STM32:"]

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
        """主机本地行编辑：按键只改窗口内容，不发给 MCU。
        回车时由 on_enter 整行发送，避免 MCU 逐字回显环回造成乱码。"""
        if not self.serial_port or not self.serial_port.is_open:
            return "break"

        if event.keysym == "Return":
            return "break"

        if event.keysym == "BackSpace":
            # 交给 _on_backspace 做本地删除，不发串口
            return None

        if event.keysym in ("Up", "Down", "Left", "Right", "Home", "End", "Tab"):
            # 本地光标移动；不发给 MCU
            return None

        if event.keysym.lower() == "l" and (event.state & 0x4):  # Ctrl+L
            self.clear_screen()
            return "break"

        if event.char and event.char.isprintable():
            # 只允许在最后一行提示符之后插入
            line, col = map(int, self.txt_output.index("insert").split('.'))
            last_line = int(self.txt_output.index("end-1c").split('.')[0])
            if line != last_line:
                self.txt_output.mark_set("insert", "end-1c")
            editable_start = self._get_editable_start()
            cur_col = int(self.txt_output.index("insert").split('.')[1])
            if cur_col < editable_start:
                self.txt_output.mark_set("insert", f"{last_line}.{editable_start}")
            self.txt_output.insert("insert", event.char)
            return "break"

        return "break"

    def clear_screen(self):
        self.txt_output.delete('1.0', tk.END)

    def on_enter(self, event):
        """整行发送命令 + \\r。MCU 不再依赖逐字输入。"""
        if not self.serial_port or not self.serial_port.is_open:
            return "break"

        line_start = self.txt_output.index("insert linestart")
        line_end = self.txt_output.index("insert lineend")
        line_text = self.txt_output.get(line_start, line_end)

        for prefix in self.prompt_prefix_list:
            if line_text.startswith(prefix):
                line_text = line_text[len(prefix):]
                break
            elif prefix in line_text:
                pos = line_text.rfind(prefix)
                line_text = line_text[pos + len(prefix):]
                break

        line_text = line_text.strip()
        self.txt_output.insert("end", "\n")
        self.txt_output.see("end")

        try:
            self.serial_port.write((line_text + '\r').encode('utf-8', errors='ignore'))
        except Exception as e:
            self.print_text(f"[错误] 发送失败: {e}\n", 'red')

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

        # 勾选后：打开串口时故意脉冲 DTR/RTS 复位，便于抓启动日志
        self.chk_reset_on_open = ttk.Checkbutton(
            frm_top, text="打开时复位", variable=self.reset_on_open)
        self.chk_reset_on_open.pack(side='left', padx=(8, 0))
        
        #新增暂停模式
        self.btn_pause = ttk.Button(frm_top, text="暂停显示", command=self.toggle_pause, state='disabled')
        self.btn_pause.pack(side='left', padx=(8, 0))
        
        #新增复位模式（不关串口，脉冲复位以便观察启动日志）
        self.btn_reset = ttk.Button(frm_top, width=12, text="复位", command=self.reset, state='disabled')
        self.btn_reset.pack(side='left', padx=(6, 30))

        #系统时间戳
        self.btn_sysTime = ttk.Button(frm_top, text="展示时间", command=self.systemTime, state='disabled')
        self.btn_sysTime.pack(side='left')
        
        frm_log = ttk.Frame(self)
        frm_log.pack(fill='x', padx=10, pady=5)
        ttk.Label(frm_log, text="日志保存路径:").pack(side='left')
        # 默认保存路径：环境变量 SERIAL_LOG_DIR，否则 ~/gitProj/log
        _default_log = os.environ.get(
            "SERIAL_LOG_DIR",
            os.path.join(os.path.expanduser("~"), "gitProj", "log"),
        )
        try:
            os.makedirs(_default_log, exist_ok=True)
        except OSError:
            pass
        self.log_path_var = tk.StringVar(value=_default_log)
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
        if self._ymodem_busy:
            messagebox.showwarning("警告", "YMODEM 正在传输中")
            return

        filepath = filedialog.askopenfilename(
            title="选择要上传的 APP 固件",
            filetypes=[("Binary", "*.bin"), ("All files", "*.*")],
        )
        if not filepath:
            return

        self._ymodem_busy = True
        self.btn_ymodem.config(state='disabled')
        threading.Thread(
            target=self._ymodem_worker,
            args=(filepath,),
            daemon=True,
        ).start()

    def _ymodem_worker(self, filepath):
        success = False
        try:
            # 暂停读线程，避免抢走 ACK / 'C'
            self.reader_paused = True
            time.sleep(0.05)

            sender = YmodemSender(self.serial_port, gui=self)

            # 若已在 Waiting/发 C（例如手动敲过 upgrade），切勿再发 upgrade：
            # 字面量里的 'a' 会被接收端当成 ABORT2 用户中止。
            already_waiting = sender._wait_for_char(CRC16, timeout=0.8)
            if already_waiting:
                self.print_text(
                    "[YMODEM] 检测到接收端已在等待(C)，直接发送文件\n", 'cyan')
            else:
                try:
                    self.serial_port.reset_input_buffer()
                    self.serial_port.reset_output_buffer()
                except Exception:
                    pass
                self.print_text("[YMODEM] 发送 upgrade 进入接收模式...\n", 'cyan')
                self.serial_port.write(b"upgrade\r")
                self.serial_port.flush()
                if not sender._wait_for_char(CRC16, timeout=15):
                    self.print_text(
                        "[YMODEM] 未等到接收端 'C'，请确认已在 boot CLI\n", 'red')
                    success = False
                else:
                    already_waiting = True

            if already_waiting:
                # 头包后要擦扇区，ACK 可能较慢
                success = sender.send_file(filepath, already_got_c=True)
        except Exception as e:
            self.print_text(f"[YMODEM] 异常: {e}\n", 'red')
            success = False
        finally:
            self.reader_paused = False
            self._ymodem_busy = False
            self.after(0, lambda: self.btn_ymodem.config(state='normal'))
            if success:
                self.after(0, lambda: messagebox.showinfo(
                    "YMODEM", "文件上传成功！可用 goto 0 / reset 跳转 APP"))
            else:
                self.after(0, lambda: messagebox.showerror(
                    "YMODEM", "文件上传失败，请查看日志。"))

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
    def _pulse_mcu_reset(self, ser=None):
        """通过 DTR/RTS 脉冲让板子复位（CH340/野火常见接法）。串口保持打开，可抓启动日志。"""
        ser = ser or self.serial_port
        if not ser or not ser.is_open:
            return False
        try:
            # 先拉到空闲，再脉冲一下
            ser.setDTR(False)
            ser.setRTS(False)
            time.sleep(0.02)
            ser.setDTR(True)
            ser.setRTS(True)
            time.sleep(0.1)
            ser.setDTR(False)
            ser.setRTS(False)
            return True
        except Exception as e:
            self.print_text(f"[错误] 复位脉冲失败: {e}\n", 'red')
            return False

    def _open_serial(self, port, baud):
        """打开串口；默认 open 前拉低 DTR/RTS，避免关开串口就误复位。"""
        ser = serial.Serial()
        ser.port = port
        ser.baudrate = int(baud)
        ser.timeout = 0.5
        # 必须在 open 前设置，否则驱动一 open 就可能已经脉冲复位了
        ser.dtr = False
        ser.rts = False
        ser.open()
        try:
            ser.setDTR(False)
            ser.setRTS(False)
        except Exception:
            pass
        if self.reset_on_open.get():
            self._pulse_mcu_reset(ser)
            self.print_text("[系统] 已按「打开时复位」脉冲复位 MCU\n", 'yellow')
        return ser

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
            self.serial_port = self._open_serial(port, baud)
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
        tip = "（打开时复位：开）" if self.reset_on_open.get() else "（打开时复位：关，不关串口也能用「复位」抓启动日志）"
        self.print_text(f"[系统] 串口 {port} 已打开，波特率 {baud} {tip}\n", 'green')
        #串口数据读取线程
        self.thread = threading.Thread(target=self.read_from_port, daemon=True)
        self.thread.start()
        #串口自动重连线程
        self.monitor_thread = threading.Thread(target=self.monitor_port, daemon=True)        
        self.monitor_thread.start()
 
    def read_from_port(self):
        """用 read() 而不是 readline()：CLI 提示符 @STM32: 没有 \\n，readline 会卡住。"""
        while self.running:
            try:
                if self.reader_paused:
                    time.sleep(0.05)
                    continue
                if not self.serial_port or not self.serial_port.is_open:
                    break
                n = self.serial_port.in_waiting
                raw = self.serial_port.read(n if n > 0 else 1)
                if not raw:
                    continue
                # latin-1 保留 0x1B 等控制字节
                line = raw.decode('latin-1', errors='ignore')
                ts = datetime.now().strftime("[%Y-%m-%d %H:%M:%S] ") if self.timeStamp else None
                self.txt_output.after(0, self.feed_ansi, line, ts)

                plain = ANSI_RE.sub('', line)
                if self.logfile:
                    self.logfile.write((ts or datetime.now().strftime("[%Y-%m-%d %H:%M:%S] ")) + plain)
                    self.logfile.flush()
            except Exception as e:
                self.print_text(f"[错误] 读取串口异常: {e}\n", 'red')
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
                self.reader_thread_running = False
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
                        self.serial_port = self._open_serial(port, baud)
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
                # 关闭前先拉低，减轻 close 时的线电平跳变复位
                try:
                    self.serial_port.setDTR(False)
                    self.serial_port.setRTS(False)
                except Exception:
                    pass
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
        """不关串口：脉冲复位 MCU，启动日志可直接看到。"""
        if not self.serial_port or not self.serial_port.is_open:
            messagebox.showwarning("提示", "请先打开串口")
            return
        if self._ymodem_busy:
            messagebox.showwarning("提示", "YMODEM 传输中，请勿复位")
            return
        if self._pulse_mcu_reset():
            self.print_text("[系统] 已脉冲复位 MCU（串口保持打开）\n", 'yellow')
    def systemTime(self):
        self.timeStamp = not self.timeStamp
        if self.timeStamp:
            self.btn_sysTime.config(text="不展示时间")
        else:
            self.btn_sysTime.config(text="展示时间")
    def feed_ansi(self, text, timestamp=None):
        """跨包拼接半截 ESC[...m，避免露出 [1;33m / [1;31m。
        timestamp 在拼接 CSI 之后再加，避免把时间戳插进 ESC 与 [ 中间。"""
        if self.paused:
            return
        if self._ansi_pending:
            text = self._ansi_pending + text
            self._ansi_pending = ''

        m = ANSI_PARTIAL_RE.search(text)
        if m:
            self._ansi_pending = text[m.start():]
            text = text[:m.start()]

        if not text:
            return
        if timestamp:
            text = timestamp + text
        self.insert_ansi(text)

    def insert_ansi(self, text):
        if self.paused:
            return

        # 统一同包内的 CRLF（必须在按 \\r 清行之前做，否则 \\r\\n 会被拆成清行+换行）
        text = text.replace('\r\r\n', '\n').replace('\r\n', '\n')
        # 折叠多余 \\r（固件若把 \\n 再转成 \\r\\n，跨包时可能只剩连续 \\r）
        text = re.sub(r'\r+', '\r', text)

        # 上一包末尾悬空的 \\r：若本包以 \\n 开头则只是换行；
        # 若仍是 \\r 则继续挂起（切勿清行，否则会把已输出的提示符拆成 STM3/STM32F 多行）；
        # 否则清行后覆盖（boot 倒计时）。
        if self._pending_cr:
            self._pending_cr = False
            if text.startswith('\n'):
                text = text[1:]
                self.txt_output.insert(tk.END, '\n', self._current_ansi_tag)
            elif text.startswith('\r'):
                text = text[1:]
                if not text:
                    self._pending_cr = True
                    return
                line_start = self.txt_output.index('end - 1c linestart')
                line_end = self.txt_output.index('end - 1c lineend')
                self.txt_output.delete(line_start, line_end)
            else:
                line_start = self.txt_output.index('end - 1c linestart')
                line_end = self.txt_output.index('end - 1c lineend')
                self.txt_output.delete(line_start, line_end)

        # 支持 CSI 清行：ESC[2K / ESC[K（shell 历史上常用）
        text = re.sub(r'\x1b\[[0-9]*K', '', text)

        pos = 0
        tag = self._current_ansi_tag

        for m in re.finditer(r'\n|\r', text):
            start, end = m.span()
            chunk = text[pos:start]
            sep = m.group()

            if chunk:
                tag = self._insert_chunk(chunk, tag)

            if sep == '\n':
                self.txt_output.insert(tk.END, '\n', tag)
            else:
                # 包末尾单独的 \\r：先挂起，等下一包再决定
                if end == len(text):
                    self._pending_cr = True
                else:
                    # 行内 \\r：回到行首覆盖（boot 倒计时）
                    line_start = self.txt_output.index('end - 1c linestart')
                    line_end = self.txt_output.index('end - 1c lineend')
                    self.txt_output.delete(line_start, line_end)

            pos = end

        if pos < len(text):
            tag = self._insert_chunk(text[pos:], tag)

        self._current_ansi_tag = tag
        self.txt_output.see(tk.END)

    def _insert_chunk(self, chunk, tag):
        pos = 0
        while pos < len(chunk):
            m = ANSI_RE.search(chunk, pos)
            if m:
                start, end = m.span()
                if start > pos:
                    self.txt_output.insert(tk.END, chunk[pos:start], tag)
                tag = self.ansi_code_to_tag(m.group(1))
                pos = end
            else:
                self.txt_output.insert(tk.END, chunk[pos:], tag)
                break
        return tag

    def ansi_code_to_tag(self, code):
        """解析 SGR：支持 31 / 1;33 / 1;31;44 等组合。"""
        tag = 'reset'
        if not code:
            return tag
        for p in code.split(';'):
            if p in ('', '0', '39'):
                tag = 'reset'
            elif p == '31':
                tag = 'red'
            elif p == '32':
                tag = 'green'
            elif p == '33':
                tag = 'yellow'
            elif p == '34':
                tag = 'blue'
            elif p == '35':
                tag = 'magenta'
            elif p == '36':
                tag = 'cyan'
            elif p == '37':
                tag = 'reset'
            # 1=粗体, 40-47=背景色：忽略，只取前景
        return tag

    def send_command(self, event=None):
        if not self.serial_port or not self.serial_port.is_open:
            messagebox.showwarning("提示", "串口未打开")
            return
        cmd = self.ent_input.get().strip()
        if not cmd:
            return
        try:
            # 只发 \r；\r\n 会让 MCU CLI 多处理一次空行
            self.serial_port.write((cmd + '\r').encode())
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

    def begin_ymodem_progress(self):
        """创建一行可覆盖的进度行"""
        self._ymodem_progress_mark = 'ymodem_progress'
        try:
            self.txt_output.mark_unset(self._ymodem_progress_mark)
        except tk.TclError:
            pass
        self.txt_output.insert(tk.END, "[YMODEM] \n", 'cyan')
        # mark 在刚插入行的行首
        line = int(self.txt_output.index('end-2c').split('.')[0])
        self.txt_output.mark_set(self._ymodem_progress_mark, f'{line}.0')
        self.txt_output.mark_gravity(self._ymodem_progress_mark, tk.LEFT)

    def update_ymodem_progress(self, line_text):
        """同一行刷新：[############----]  42%  n/total"""
        mark = getattr(self, '_ymodem_progress_mark', None)
        text = f"[YMODEM] {line_text}"
        if not mark:
            self.begin_ymodem_progress()
            mark = self._ymodem_progress_mark
        try:
            start = self.txt_output.index(mark)
            line_no = start.split('.')[0]
            end = f'{line_no}.end'
            self.txt_output.delete(start, end)
            self.txt_output.insert(start, text, 'cyan')
            self.txt_output.mark_set(mark, start)
            self.txt_output.see(tk.END)
        except tk.TclError:
            self.txt_output.insert(tk.END, text + '\n', 'cyan')
            self.txt_output.see(tk.END)

    def end_ymodem_progress(self, ok=True):
        mark = getattr(self, '_ymodem_progress_mark', None)
        if mark:
            try:
                self.txt_output.mark_unset(mark)
            except tk.TclError:
                pass
            self._ymodem_progress_mark = None
        # 进度行已在，补一个换行感：若末行不是空则无需再插
        self.txt_output.see(tk.END)

    def on_close(self):
        self.stop()
        self.destroy()

if __name__ == "__main__":
    app = SerialMonitor()
    app.mainloop()
