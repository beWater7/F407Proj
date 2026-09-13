import ctypes
import json
import tkinter as tk
from tkinter import ttk, scrolledtext, messagebox, filedialog, colorchooser
import serial
import serial.tools.list_ports
import threading
import os
import re
import time
import sys
import logging
import struct
import errno
from datetime import datetime

# POSIX 串口独占锁: 仅 Linux/macOS 可用; Windows 走 pyserial exclusive 之外无 flock
try:
    import fcntl
    import termios
    _POSIX_PORT_LOCK = True
except ImportError:          # Windows
    _POSIX_PORT_LOCK = False

# DPI Aware 提高清晰度
try:
    ctypes.windll.shcore.SetProcessDpiAwareness(1)
except:
    pass

ANSI_RE = re.compile(r'\x1b\[([0-9;]*)m')
# 包尾可能只收到 ESC / ESC[ / ESC[1;33 半截 CSI，需跨包拼接
ANSI_PARTIAL_RE = re.compile(r'\x1b(?:\[[0-9;]*)?$')
SPLIT_RE = re.compile(r'\r\n|\n|\r')
# 从 MCU help 列表解析命令名：固定 4 空格缩进的 "    cmd  Description" 行
HELP_CMD_RE = re.compile(r'^    (.+?)\s{2,}\S', re.MULTILINE)
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
    # 串口输出区背景预设。以深色护眼为主，最后一个浅色用于白天/投影
    DEFAULT_BG = "#1e1e1e"
    BG_PRESETS = {
        "石墨 #1e1e1e": "#1e1e1e",
        "纯黑 #000000": "#000000",
        "深灰 #2b2b2b": "#2b2b2b",
        "深蓝 #0d1b2a": "#0d1b2a",
        "墨绿 #0f1f14": "#0f1f14",
        "暖棕 #241c16": "#241c16",
        "浅灰 #f0f0f0": "#f0f0f0",
    }

    # ---- 配置持久化：背景/透明度/窗口尺寸/串口等下次启动自动恢复 ----
    CONFIG_DIR = os.path.join(os.path.expanduser("~"), ".config", "serialTerm")
    CONFIG_PATH = os.path.join(CONFIG_DIR, "config.json")

    def __init__(self):
        global serial_stop
        serial_stop = 'reset'
        super().__init__()
        self.title("串口监控器（带系统时间戳 + ANSI彩色 + 野火初始化）")
        self.configure(bg="#2d2d2d")
        # 窗口尺寸不在这里写死：控件建完后由 _fit_to_content() 按实际内容算。
        # (旧代码写死 1400x1200，在本机 tk scaling≈2.67 的 HiDPI 下，
        #  工具栏一行需要 ~2200px，右侧「复位」「展示时间」会被窗口裁掉)
        # 上次保存的配置(背景/透明度/尺寸/串口/波特率等)
        self.cfg = self._load_config()
        # 控件全部就绪前不要回写配置(避免把半初始化状态存盘)
        self._ui_ready = False
        self.paused = False
        self.timeStamp = bool(self.cfg.get("timestamp", False))
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
        self.reset_on_open = tk.BooleanVar(value=bool(self.cfg.get("reset_on_open", False)))

        # 111    
        #self.prompt_prefix = "[mcu@board]#"
        self.prompt_prefix = "STM32F407 >"
        self.prompt_prefix_boot = "BOOT#"
        # 仅用完整提示符做前缀匹配；不要放 "..."，会误伤倒计时行
        self.prompt_prefix_list = ["STM32F407 >", "BOOT#", "@STM32:"]

        self.create_widgets()
        # 控件建好后才知道工具栏真实宽度，这里再定窗口尺寸
        self._fit_to_content()

        self.txt_output.bind("<Key>", self.on_key)
        self.txt_output.bind("<Return>", self.on_enter)
        self.txt_output.bind("<BackSpace>", self._on_backspace)
        self.txt_output.bind("<Delete>", self._on_delete)
        # 必须单独绑定，放在 on_key 里会被 Text 默认行为抢掉
        self.txt_output.bind("<Up>", self._on_history_up)
        self.txt_output.bind("<Down>", self._on_history_down)
        self.txt_output.bind("<Tab>", self._on_tab)

        self.protocol("WM_DELETE_WINDOW", self.on_close)
        #self.txt_output.delete(line_start, line_end)

        # 控件与尺寸都就绪，之后的外观改动可以安全回写配置了
        self._ui_ready = True

    def _get_editable_start(self, line_no=None):
        """返回指定行提示符之后的可编辑起始列（默认当前光标行）"""
        if line_no is None:
            line_no = int(self.txt_output.index("insert").split('.')[0])
        line_text = self.txt_output.get(f"{line_no}.0", f"{line_no}.end")
        for prefix in self.prompt_prefix_list:
            if line_text.startswith(prefix):
                return len(prefix)
        return 0

    def _find_input_line(self):
        """找最后一行带提示符的输入行；没有则返回末行号"""
        last = int(self.txt_output.index("end-1c").split('.')[0])
        for ln in range(last, 0, -1):
            text = self.txt_output.get(f"{ln}.0", f"{ln}.end")
            for prefix in self.prompt_prefix_list:
                if text.startswith(prefix):
                    return ln
        return last

    def _focus_input_line(self):
        """光标放到输入行末尾（提示符之后已有内容的后面）"""
        line = self._find_input_line()
        start = self._get_editable_start(line)
        end_col = int(self.txt_output.index(f"{line}.end").split('.')[1])
        self.txt_output.mark_set("insert", f"{line}.{max(start, end_col)}")
        self.txt_output.see("end")

    def _maybe_focus_input_line(self):
        """仅当光标不在输入行可编辑区时才跳转，避免打字过程被拽回行首"""
        line, col = map(int, self.txt_output.index("insert").split('.'))
        input_line = self._find_input_line()
        start = self._get_editable_start(input_line)
        if line != input_line or col < start:
            self._focus_input_line()

    def _on_backspace(self, event):
        line, col = map(int, self.txt_output.index("insert").split('.'))
        input_line = self._find_input_line()
        editable_start = self._get_editable_start(input_line)
        if line != input_line or col <= editable_start:
            return "break"

    def _on_delete(self, event):
        line = int(self.txt_output.index("insert").split('.')[0])
        if line != self._find_input_line():
            return "break"

    def _on_history_up(self, event):
        self._history_up()
        return "break"

    def _on_history_down(self, event):
        self._history_down()
        return "break"

    def _on_tab(self, event):
        self._tab_complete()
        return "break"

    def on_key(self, event):
        """主机本地行编辑：按键只改窗口内容，不发给 MCU。"""
        if not self.serial_port or not self.serial_port.is_open:
            return "break"

        if event.keysym in ("Return", "Up", "Down", "Tab"):
            return "break"

        if event.keysym == "BackSpace":
            return None

        if event.keysym in ("Left", "Right", "Home", "End"):
            return None

        if event.keysym.lower() == "l" and (event.state & 0x4):  # Ctrl+L
            self.clear_screen()
            return "break"

        if event.char and event.char.isprintable():
            line, col = map(int, self.txt_output.index("insert").split('.'))
            input_line = self._find_input_line()
            editable_start = self._get_editable_start(input_line)
            if line != input_line:
                self._focus_input_line()
            elif col < editable_start:
                self.txt_output.mark_set("insert", f"{input_line}.{editable_start}")
            self.txt_output.insert("insert", event.char)
            return "break"

        return "break"

    def clear_screen(self):
        self.txt_output.delete('1.0', tk.END)

    def on_enter(self, event):
        """整行发送命令 + \\r。不在本地插换行，由 MCU 回显/提示符负责换行。"""
        if not self.serial_port or not self.serial_port.is_open:
            return "break"

        line = self._find_input_line()
        line_text = self.txt_output.get(f"{line}.0", f"{line}.end")
        start_col = self._get_editable_start(line)
        cmd = line_text[start_col:].strip()

        if cmd:
            if not self.cmd_history or self.cmd_history[-1] != cmd:
                self.cmd_history.append(cmd)
            self.history_index = len(self.cmd_history)
            self._pending_draft = ''

        try:
            self.serial_port.write((cmd + '\r').encode('utf-8', errors='ignore'))
        except Exception as e:
            self.print_text(f"[错误] 发送失败: {e}\n", 'red')

        return "break"

    def _get_edit_line(self):
        """返回 (line_no, editable_start_col, current_text)"""
        line = self._find_input_line()
        full = self.txt_output.get(f"{line}.0", f"{line}.end")
        start_col = self._get_editable_start(line)
        return line, start_col, full[start_col:]

    def _replace_edit(self, text):
        """把当前输入行（提示符之后）整体替换为 text"""
        line, start_col, _ = self._get_edit_line()
        self.txt_output.delete(f"{line}.{start_col}", f"{line}.end")
        if text:
            self.txt_output.insert(f"{line}.{start_col}", text)
        self.txt_output.mark_set("insert", f"{line}.{start_col + len(text)}")
        self.txt_output.see("end")

    def _history_up(self):
        if not self.cmd_history:
            return
        self._focus_input_line()
        _, _, cur = self._get_edit_line()
        if self.history_index >= len(self.cmd_history):
            self._pending_draft = cur
            self.history_index = len(self.cmd_history) - 1
        elif self.history_index > 0:
            self.history_index -= 1
        else:
            return
        self._replace_edit(self.cmd_history[self.history_index])

    def _history_down(self):
        if not self.cmd_history:
            return
        if self.history_index < len(self.cmd_history) - 1:
            self.history_index += 1
            self._replace_edit(self.cmd_history[self.history_index])
        else:
            self.history_index = len(self.cmd_history)
            draft = self._pending_draft
            self._pending_draft = ''
            self._replace_edit(draft)

    def _learn_commands_from_text(self, text):
        """从串口 help 列表行解析命令名，例如 '    ping <ip> ...'"""
        for cmd in HELP_CMD_RE.findall(text):
            cmd = cmd.strip()
            if cmd and not set(cmd) <= {'-'}:
                self.shell_commands.add(cmd)

    def _find_completions(self, cur):
        """根据 help 学到的命令 + 历史记录做 Tab 补全"""
        cur = cur.rstrip()
        if ' ' in cur:
            rest, prefix = cur.rsplit(' ', 1)
            rest = rest.strip()
        else:
            rest, prefix = '', cur

        pool = set(self.shell_commands)
        pool.update(self.cmd_history)

        matches = []
        for cmd in pool:
            if rest:
                if not cmd.startswith(rest + ' '):
                    continue
                tail = cmd[len(rest) + 1:]
                word = tail.split()[0] if tail else ''
                if word.startswith(prefix):
                    matches.append(f"{rest} {word}".strip())
            else:
                first = cmd.split()[0]
                if first.startswith(prefix):
                    matches.append(first)
                elif cmd.startswith(prefix):
                    matches.append(cmd)

        return sorted(set(matches), key=lambda s: (len(s), s))

    def _apply_tab(self, cur, set_text, show_candidates):
        """通用 Tab 补全：set_text(新内容) / show_candidates(候选列表)"""
        matches = self._find_completions(cur)
        if not matches:
            return
        if len(matches) == 1:
            set_text(matches[0] + ' ')
            return
        common = self._common_prefix(matches)
        if common and len(common) > len(cur.rstrip()):
            set_text(common)
        else:
            show_candidates(matches)

    def _tab_complete(self):
        self._focus_input_line()
        line, start_col, cur = self._get_edit_line()

        def set_text(text):
            self._replace_edit(text)

        def show_candidates(matches):
            self.txt_output.insert(tk.END, '\n  ' + '  '.join(matches) + '\n')
            prompt = self.txt_output.get(f"{line}.0", f"{line}.{start_col}")
            self.txt_output.insert(tk.END, prompt + cur)
            self.txt_output.mark_set("insert", tk.END)
            self.txt_output.see("end")

        self._apply_tab(cur, set_text, show_candidates)

    @staticmethod
    def _common_prefix(strs):
        if not strs:
            return ''
        s = min(strs, key=len)
        for i, ch in enumerate(s):
            if any(st[i] != ch for st in strs):
                return s[:i]
        return s

    def create_widgets(self):
        style = ttk.Style(self)
        style.theme_use('clam')
        style.configure("TLabel", background="#2d2d2d", foreground="#d4d4d4", font=("Consolas", 11))
        style.configure("TButton", font=("Consolas", 11))
        style.configure("TEntry", font=("Consolas", 11))
        style.configure("TCombobox", font=("Consolas", 11))

        # 按钮内边距收窄：HiDPI 下默认 padding 被缩放整除后会虚胖
        # (每个按钮曾达 214px，11 个排一行要 2191px，窗口被迫很宽)
        style.configure("TButton", font=("Consolas", 11), padding=(2, 2))
        # LabelFrame 深色描边：分组框要和暗色主题协调
        style.configure("TLabelframe", background="#2d2d2d",
                        bordercolor="#3c3c3c", relief="solid", borderwidth=1)
        style.configure("TLabelframe.Label", background="#2d2d2d",
                        foreground="#8ab4f8", font=("Consolas", 11))

        # 统一网格内边距：所有控件用同一组，间距才规整
        P = {'padx': 4, 'pady': 3}

        # ================= 工具栏：按功能分组 + grid 对齐 =================
        # 用 grid 而不是 pack：同一列的控件宽度自动对齐（sticky='ew'），
        # 按钮不会各自长短不一。分组用 LabelFrame，一眼看清归属。
        frm_bar = ttk.Frame(self)
        frm_bar.pack(fill='x', padx=10, pady=(8, 2))

        # ---- 分组 1：连接 ----
        lf_conn = ttk.LabelFrame(frm_bar, text=" 连接 ")
        lf_conn.grid(row=0, column=0, sticky='new', padx=(0, 6))
        lf_conn.columnconfigure(1, weight=1)

        ttk.Label(lf_conn, text="串口:").grid(row=0, column=0, sticky='w', **P)
        self.cb_ports = ttk.Combobox(lf_conn, width=12, values=self.get_ports(), state='readonly')
        self.cb_ports.grid(row=0, column=1, sticky='ew', **P)
        if self.cfg.get("port"):            # 恢复上次选的串口
            self.cb_ports.set(self.cfg["port"])
        self.btn_refresh = ttk.Button(lf_conn, text="刷新", command=self.refresh_ports)
        self.btn_refresh.grid(row=0, column=2, sticky='ew', **P)
        # 勾选后：打开串口时故意脉冲 DTR/RTS 复位，便于抓启动日志
        self.chk_reset_on_open = ttk.Checkbutton(
            lf_conn, text="打开时复位", variable=self.reset_on_open)
        self.chk_reset_on_open.grid(row=0, column=3, sticky='w', **P)

        ttk.Label(lf_conn, text="波特率:").grid(row=1, column=0, sticky='w', **P)
        self.cb_baud = ttk.Combobox(lf_conn, width=10, values=["9600", "19200", "38400", "57600", "115200", "230400", "460800", "921600"], state='readonly')
        self.cb_baud.grid(row=1, column=1, sticky='ew', **P)
        self.cb_baud.set(str(self.cfg.get("baud") or "115200"))   # 恢复上次波特率
        self.btn_start = ttk.Button(lf_conn, text="打开串口", command=self.start)
        self.btn_start.grid(row=1, column=2, sticky='ew', **P)
        self.btn_stop = ttk.Button(lf_conn, text="关闭串口", command=self.stop, state='disabled')
        self.btn_stop.grid(row=1, column=3, sticky='ew', **P)

        # ---- 分组 2：操作 ----
        lf_oper = ttk.LabelFrame(frm_bar, text=" 操作 ")
        lf_oper.grid(row=0, column=1, sticky='new')

        #新增暂停模式
        self.btn_pause = ttk.Button(lf_oper, text="暂停显示", command=self.toggle_pause, state='disabled')
        self.btn_pause.grid(row=0, column=0, sticky='ew', **P)
        #新增复位模式（不关串口，脉冲复位以便观察启动日志）
        self.btn_reset = ttk.Button(lf_oper, text="复位", command=self.reset, state='disabled')
        self.btn_reset.grid(row=0, column=1, sticky='ew', **P)
        #系统时间戳
        self.btn_sysTime = ttk.Button(lf_oper, text="展示时间", command=self.systemTime, state='disabled')
        self.btn_sysTime.grid(row=0, column=2, sticky='ew', **P)

        # ---- 分组 3：外观（背景色 / 透明度）----
        lf_look = ttk.LabelFrame(frm_bar, text=" 外观 ")
        lf_look.grid(row=1, column=0, columnspan=2, sticky='ew', pady=(6, 0))
        # 只在尾部留一个"吸收列"：多余宽度都被它吃掉，
        # 控件保持自然尺寸，不会被 columnconfigure(1, weight=1) 拉成超宽下拉框
        lf_look.columnconfigure(4, weight=1)

        self._bg_color = self.DEFAULT_BG

        ttk.Label(lf_look, text="背景:").grid(row=0, column=0, sticky='w', **P)
        self.bg_choice = tk.StringVar(value="石墨 #1e1e1e")
        self.cb_bg = ttk.Combobox(lf_look, width=14, state='readonly',
                                  textvariable=self.bg_choice,
                                  values=list(self.BG_PRESETS.keys()))
        self.cb_bg.grid(row=0, column=1, sticky='w', **P)
        self.cb_bg.bind('<<ComboboxSelected>>', self._on_bg_preset)

        self.btn_pick = ttk.Button(lf_look, text="取色…", command=self._pick_bg_color)
        self.btn_pick.grid(row=0, column=2, sticky='w', **P)
        self.btn_look_reset = ttk.Button(lf_look, text="重置外观", command=self._reset_look)
        self.btn_look_reset.grid(row=0, column=3, sticky='w', **P)

        ttk.Label(lf_look, text="透明度:").grid(row=1, column=0, sticky='w', **P)
        self.scale_alpha = ttk.Scale(lf_look, from_=30, to=100, orient='horizontal',
                                     length=200, command=self._on_alpha_change)
        self.scale_alpha.grid(row=1, column=1, sticky='w', **P)
        # 数值标签：等宽 + 右对齐，滑动时数字不跳位
        self.lbl_alpha_val = ttk.Label(lf_look, text="100%", width=5, anchor='w')
        self.lbl_alpha_val.grid(row=1, column=2, sticky='w', **P)
        self.scale_alpha.set(100)   # 必须在 lbl_alpha_val 之后

        # ================= 日志路径：独占一行，保证路径完整可见 =================
        # 之前和 3 个按钮挤在同一行，输入框只剩 ~300px，路径被截断
        frm_log = ttk.Frame(self)
        frm_log.pack(fill='x', padx=10, pady=(6, 2))
        frm_log.columnconfigure(1, weight=1)   # 输入框吃掉剩余全部宽度

        # 标签用简写"日志:"，把宽度让给输入框 —— 路径才显示得全
        ttk.Label(frm_log, text="日志:").grid(row=0, column=0, sticky='w', padx=(0, 4))
        # 默认保存路径优先级：上次配置 > 环境变量 SERIAL_LOG_DIR > ~/gitProj/log
        _default_log = (self.cfg.get("log_path")
                        or os.environ.get("SERIAL_LOG_DIR")
                        or os.path.join(os.path.expanduser("~"), "gitProj", "log"))
        try:
            os.makedirs(_default_log, exist_ok=True)
        except OSError:
            pass
        self.log_path_var = tk.StringVar(value=_default_log)
        self.ent_log_path = ttk.Entry(frm_log, textvariable=self.log_path_var)
        self.ent_log_path.grid(row=0, column=1, sticky='ew', padx=4)
        self.btn_browse = ttk.Button(frm_log, text="浏览", command=self.browse_folder)
        self.btn_browse.grid(row=0, column=2, padx=4)

        # 新增：YMODEM上传按钮
        self.btn_ymodem = ttk.Button(frm_log, text="YMODEM上传", command=self.send_file_via_ymodem)
        self.btn_ymodem.grid(row=0, column=3, padx=(4, 0))

        #设置串口输出数据格式，字体:Lucida Console \ Consolas \ Courier New，大小
        self.txt_output = scrolledtext.ScrolledText(self, font=("Consolas", 10), bg=self.DEFAULT_BG, fg="#f0f0f0", insertbackground="white")
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

        # 恢复上次保存的外观（背景色 + 透明度）；无配置则用默认
        self._restore_look()

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
        self.ent_input.bind("<Up>", self._ent_history_up)
        self.ent_input.bind("<Down>", self._ent_history_down)
        self.ent_input.bind("<Tab>", self._ent_tab)
        self.cmd_history = []
        self.history_index = -1
        self._pending_draft = ''
        self._ent_draft = ''
        self.shell_commands = set()  # 从 help 输出 + 历史动态积累

        self.btn_send = ttk.Button(frm_input, text="发送", command=self.send_command)
        self.btn_send.pack(side='left', padx=6)

    # 窗口目标宽高比(宽:高)。终端类窗口横向略宽更协调；
    # 高度还会被"内容最小高度"和屏幕可用高度夹住。
    TARGET_ASPECT = 1.25

    def _fit_to_content(self, min_w=900, min_h=800):
        """按控件实际需要的最小尺寸设置窗口，保证工具栏按钮全部可见。

        宽度：必须由工具栏内容决定，写死会把右侧按钮裁掉。
              本机 tk scaling≈2.67(HiDPI) 下工具栏一行实测需要 ~2200px，
              旧代码写死 1400 导致「复位」「展示时间」点不到。

        高度：由宽度推出目标宽高比(TARGET_ASPECT)，这样窗口比例始终协调。
              若改回"按屏幕高度百分比"，宽度收窄后会变成竖长条
              (实测 1494x1632 = 0.92:1，反而不协调)。
        """
        self.update_idletasks()
        need_w = self.winfo_reqwidth()
        need_h = self.winfo_reqheight()   # 内容最小高度(含 Text 默认 24 行)
        scr_w = self.winfo_screenwidth()
        scr_h = self.winfo_screenheight()

        # 屏幕可用范围：留出窗口边框与任务栏的余地
        avail_w = max(min_w, scr_w - 60)
        avail_h = max(min_h, scr_h - 120)

        # 宽度：内容需要多少给多少，夹到屏幕内
        w = max(min_w, min(need_w, avail_w))

        # 高度：按目标宽高比由宽度推出，但不小于内容最小高度、不超过屏幕
        target_h = int(w / self.TARGET_ASPECT)
        h = max(min_h, min(max(need_h, target_h), avail_h))

        # 上次保存的窗口尺寸优先，但仍夹在合法范围内：
        # 宽度不得小于内容最小宽度(否则按钮被裁)，高度不得小于 min_h
        saved = self.cfg.get("geometry")
        if isinstance(saved, str):
            m = re.match(r'^(\d+)x(\d+)', saved)
            if m:
                w = max(min_w, min(max(w, int(m.group(1))), avail_w))
                h = max(min_h, min(max(h, int(m.group(2))), avail_h))

        self.geometry(f"{w}x{h}")
        # 最小宽度同 w：往回拖也不能窄到把按钮裁掉；高度允许自由缩小
        self.minsize(w, min_h)

    # ==================== 配置持久化 ====================
    def _load_config(self):
        """读取上次保存的配置；文件缺失/损坏都退回空配置，不影响启动。"""
        try:
            with open(self.CONFIG_PATH, "r", encoding="utf-8") as f:
                data = json.load(f)
            return data if isinstance(data, dict) else {}
        except (OSError, ValueError):
            return {}

    def _save_config(self):
        """把当前外观/串口/尺寸写入配置文件。

        写入走"临时文件 + os.replace"：即使写到一半断电/被杀，
        也不会留下半截 JSON 把下次启动弄坏。
        """
        if not getattr(self, '_ui_ready', False):
            return
        cfg = dict(getattr(self, 'cfg', {}))   # 保留未知键，向前兼容
        cfg.update({
            "geometry": self.geometry(),
            "bg": getattr(self, '_bg_color', self.DEFAULT_BG),
            "alpha": int(getattr(self, '_alpha', 100)),
            "log_path": self.log_path_var.get(),
            "baud": self.cb_baud.get(),
            "port": self.cb_ports.get(),
            "reset_on_open": bool(self.reset_on_open.get()),
            "timestamp": bool(self.timeStamp),
        })
        try:
            os.makedirs(self.CONFIG_DIR, exist_ok=True)
            tmp = self.CONFIG_PATH + ".tmp"
            with open(tmp, "w", encoding="utf-8") as f:
                json.dump(cfg, f, ensure_ascii=False, indent=2)
            os.replace(tmp, self.CONFIG_PATH)
        except OSError:
            pass   # 配置存不上不该影响使用

    # ==================== 外观：背景色 / 透明度 ====================
    @staticmethod
    def _text_fg_for_bg(bg):
        """按背景亮度选前景色：浅底用深字，深底用浅字。
        用 ITU-R BT.601 感知亮度，比简单平均更符合人眼。"""
        try:
            r = int(bg[1:3], 16)
            g = int(bg[3:5], 16)
            b = int(bg[5:7], 16)
        except (ValueError, IndexError):
            return "#f0f0f0"
        lum = (0.299 * r + 0.587 * g + 0.114 * b) / 255.0
        return "#101010" if lum > 0.5 else "#f0f0f0"

    def _apply_bg_color(self, bg):
        """设置串口输出区背景色，并联动文字色，保证可读。"""
        fg = self._text_fg_for_bg(bg)
        self.txt_output.configure(bg=bg, fg=fg, insertbackground=fg)
        # 'reset' 是默认文字 tag，必须跟着改，否则日志会沿用旧前景色看不清
        self.txt_output.tag_config('reset', foreground=fg)
        self._bg_color = bg
        self._save_config()   # 立即落盘，下次启动自动恢复

    def _on_bg_preset(self, _evt=None):
        hexc = self.BG_PRESETS.get(self.bg_choice.get())
        if hexc:
            self._apply_bg_color(hexc)

    def _pick_bg_color(self):
        """系统取色器：任意背景色"""
        _rgb, hexc = colorchooser.askcolor(
            color=getattr(self, '_bg_color', self.DEFAULT_BG),
            title="选择串口输出区背景色")
        if hexc:
            self.bg_choice.set("自定义 %s" % hexc)
            self._apply_bg_color(hexc)

    def _on_alpha_change(self, val):
        """整窗透明度。

        说明：Tk 只有“整窗 alpha”，没有逐控件透明度 —— 所以调这个会连
        按钮、边框一起变透明，文字对比度也会下降。看日志建议 >=85%。
        """
        a = float(val) / 100.0
        # 记录"实际应用的"透明度：保存配置时以它为准。
        # (不能读 scale_alpha.get() —— 手动调用回调时滑块值可能尚未更新，
        #  会导致存盘数字和界面显示不一致)
        self._alpha = int(round(float(val)))
        try:
            self.attributes('-alpha', a)
        except tk.TclError:
            # 无合成器的窗口管理器不支持，只提示一次
            if getattr(self, '_alpha_ok', None) is not False:
                self._alpha_ok = False
                self.print_text("[系统] 当前窗口管理器不支持透明度，已忽略\n", 'yellow')
            return
        if hasattr(self, 'lbl_alpha_val'):
            # Tk Scale 回调传的是字符串(如 "100.0")，不能直接 int()
            self.lbl_alpha_val.config(text="%d%%" % int(float(val)))
        self._save_config()

    def _reset_look(self):
        """恢复默认外观"""
        self.bg_choice.set("石墨 #1e1e1e")
        self._apply_bg_color(self.DEFAULT_BG)
        self.scale_alpha.set(100)
        self._on_alpha_change(100)
        self._save_config()

    def _restore_look(self):
        """按配置恢复背景色与透明度（控件的初值设置）。

        放在所有控件建好之后调用：_apply_bg_color 需要 txt_output 已存在，
        scale_alpha.set() 会触发 _on_alpha_change 回调(需 lbl_alpha_val)。
        """
        bg = self.cfg.get("bg")
        if not isinstance(bg, str) or not re.fullmatch(r'#[0-9a-fA-F]{6}', bg):
            bg = self.DEFAULT_BG
        # 回填下拉框显示：预设名或"自定义 #xxxxxx"
        preset = next((k for k, v in self.BG_PRESETS.items()
                       if v.lower() == bg.lower()), None)
        self.bg_choice.set(preset or ("自定义 %s" % bg))
        self._apply_bg_color(bg)

        try:
            alpha = int(self.cfg.get("alpha", 100))
        except (TypeError, ValueError):
            alpha = 100
        alpha = max(30, min(100, alpha))
        self.scale_alpha.set(alpha)
        self._on_alpha_change(alpha)   # set() 不一定触发回调，这里显式应用一次

        # X11 下对"尚未映射"的窗口设置 -alpha 会被静默丢弃(不报错、读回仍是 1.0)，
        # 而这里是 __init__ 阶段窗口还没显示，所以恢复的透明度会丢失。
        # 记下来，等窗口 Map 之后再真正应用一次。
        self._pending_alpha = alpha
        self.bind('<Map>', self._apply_pending_alpha, add='+')

    def _apply_pending_alpha(self, _evt=None):
        """窗口映射后真正应用透明度（见 _restore_look 的说明）。"""
        a = getattr(self, '_pending_alpha', None)
        if a is None:
            return
        self._pending_alpha = None
        self._on_alpha_change(a)
        # 有些窗口管理器没有合成器：设置不报错、也不生效。
        # 实测一次，避免用户以为"配了没存住"。
        if a < 100:
            try:
                if abs(self.attributes('-alpha') - a / 100.0) > 0.02:
                    self.print_text(
                        "[系统] 当前窗口管理器不支持透明效果，已忽略\n", 'yellow')
            except tk.TclError:
                pass

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
            self._save_config()
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

    @staticmethod
    def _describe_open_error(e, port):
        """把串口打开失败的底层异常翻译成可操作的中文提示(占用/权限/不存在)。"""
        err = getattr(e, "errno", None)
        s = str(e)
        low = s.lower()
        if err == errno.EBUSY or "busy" in low or "占用" in s:
            return ("串口被占用",
                    f"串口 {port} 正被其它程序使用, 无法独占打开。\n"
                    f"请先关闭占用方(另一个 serialTerm 实例 / tools/xfer/xfer 正在烧录 / minicom 等),\n"
                    f"再重新打开。\n\n底层错误: {e}")
        if err == errno.EACCES or "permission denied" in low:
            return ("无权限",
                    f"无权限打开串口 {port}。\n"
                    f"请检查 udev 规则, 或把当前用户加入 dialout 组后重新登录。\n\n底层错误: {e}")
        if err == errno.ENOENT or "no such file" in low:
            return ("串口不存在",
                    f"{port} 不存在或已被拔出。\n"
                    f"请重新插拔 USB 转串口, 在下拉列表里重新选择端口。\n\n底层错误: {e}")
        return ("打开串口失败", f"打开串口 {port} 失败: {e}")

    def _lock_serial_port(self, ser, port):
        """open 成功后抢占独占锁: flock(非阻塞) + TIOCEXCL。

        与 tools/xfer/xfer 同一套锁协议 —— 任何一方先打开, 另一方 flock 立即失败,
        从而给出明确的"占用"提示, 而不是两边无锁并发读写导致烧录乱码。
        """
        try:
            fcntl.flock(ser.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError as e:
            try:
                ser.close()
            except Exception:
                pass
            raise serial.SerialException(
                f"串口 {port} 正被其它程序占用 (flock 冲突: {e})\n"
                f"请先关闭占用方(例如 tools/xfer/xfer 正在烧录 / 另一个串口终端), 再点「打开」。"
            ) from e
        # 内核级单进程独占, 双保险(非必需, 失败不致命)
        try:
            fcntl.ioctl(ser.fileno(), termios.TIOCEXCL)
        except OSError:
            pass

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
        # 抢占串口独占锁: 占用时抛异常, 由调用方(弹窗/日志)提示
        if _POSIX_PORT_LOCK:
            self._lock_serial_port(ser, port)
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
            title, msg = self._describe_open_error(e, port)
            messagebox.showerror(title, msg)
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
        # 记录本次成功使用的串口/波特率，下次启动自动预选
        self._save_config()
        tip = "（打开时复位：开）" if self.reset_on_open.get() else "（打开时复位：关，不关串口也能用「复位」抓启动日志）"
        self.print_text(f"[系统] 串口 {port} 已打开，波特率 {baud} {tip}\n", 'green')
        self.txt_output.focus_set()
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
                        self._reconn_err = None
                        self.print_text(f"[系统] 自动重连 {port} 成功\n", 'green')
                        self.reader_thread_running = True
                        self.thread = threading.Thread(target=self.read_from_port, daemon=True)
                        self.thread.start()
                    except Exception as e:
                        # 占用/权限类错误持续存在时只提示一次, 避免每秒刷屏
                        _, msg = self._describe_open_error(e, port)
                        if getattr(self, "_reconn_err", None) != msg:
                            self._reconn_err = msg
                            self.print_text(f"[错误] 自动重连失败: {msg}\n", 'red')
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
        self._save_config()
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
        self._learn_commands_from_text(text)
        self.insert_ansi(text)

    def insert_ansi(self, text):
        if self.paused:
            return

        # 统一同包内的 CRLF（必须在按 \\r 清行之前做，否则 \\r\\n 会被拆成清行+换行）
        text = text.replace('\r\r\n', '\n').replace('\r\n', '\n')
        text = re.sub(r'\r+', '\r', text)

        # 提示符前多余空行折叠（命令输出末尾 \\n + 提示符前缀 \\r\\n）
        for prefix in self.prompt_prefix_list:
            text = re.sub(r'\n+(?=' + re.escape(prefix) + r')', '\n', text)
        # 连续重复提示符（旧固件双提示符）折叠为一行
        for prefix in self.prompt_prefix_list:
            text = re.sub(
                re.escape(prefix) + r'(?:\n' + re.escape(prefix) + r')+',
                prefix, text)

        # 上一包末尾悬空的 \\r
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
        # MCU 打出新提示符后，若光标不在输入区，才跳到输入行末尾
        if any(p in text for p in self.prompt_prefix_list):
            self.after_idle(self._maybe_focus_input_line)

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
            return "break"
        cmd = self.ent_input.get().strip()
        try:
            self.serial_port.write((cmd + '\r').encode())
            if cmd:
                if not self.cmd_history or self.cmd_history[-1] != cmd:
                    self.cmd_history.append(cmd)
                self.history_index = len(self.cmd_history)
                self._ent_draft = ''
            self.ent_input.delete(0, tk.END)
        except Exception as e:
            self.print_text(f"[错误] 发送失败: {e}\n", 'red')
        return "break"

    def _ent_history_up(self, event=None):
        if not self.cmd_history:
            return "break"
        if self.history_index >= len(self.cmd_history):
            self._ent_draft = self.ent_input.get()
            self.history_index = len(self.cmd_history) - 1
        elif self.history_index > 0:
            self.history_index -= 1
        else:
            return "break"
        self.ent_input.delete(0, tk.END)
        self.ent_input.insert(0, self.cmd_history[self.history_index])
        return "break"

    def _ent_history_down(self, event=None):
        if not self.cmd_history:
            return "break"
        if self.history_index < len(self.cmd_history) - 1:
            self.history_index += 1
            self.ent_input.delete(0, tk.END)
            self.ent_input.insert(0, self.cmd_history[self.history_index])
        else:
            self.history_index = len(self.cmd_history)
            self.ent_input.delete(0, tk.END)
            self.ent_input.insert(0, self._ent_draft)
        return "break"

    def _ent_tab(self, event=None):
        cur = self.ent_input.get()

        def set_text(text):
            self.ent_input.delete(0, tk.END)
            self.ent_input.insert(0, text)

        def show_candidates(matches):
            self.print_text('  ' + '  '.join(matches) + '\n', 'cyan')

        self._apply_tab(cur, set_text, show_candidates)
        return "break"

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
        self._save_config()
        self.stop()
        self.destroy()

if __name__ == "__main__":
    app = SerialMonitor()
    app.mainloop()
