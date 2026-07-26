import tkinter as tk
from tkinter import ttk, filedialog, messagebox
import serial
import serial.tools.list_ports
import threading
import pyte
import time
from datetime import datetime
import ctypes
import os
from queue import Queue  # 线程间安全通信（关键）

# 日志文件（记录所有操作和错误，方便定位闪退原因）
LOG_FILE = os.path.join(os.path.expanduser("~"), "serial_monitor_log.txt")

def log(message):
    """记录日志到文件（所有错误和操作都在这里）"""
    try:
        with open(LOG_FILE, "a", encoding="utf-8") as f:
            f.write(f"[{datetime.now()}] {message}\n")
    except:
        pass  # 日志写入失败不影响主程序


# DPI适配
try:
    ctypes.windll.shcore.SetProcessDpiAwareness(1)
except Exception as e:
    log(f"DPI适配失败: {str(e)}")


class PyteSerialMonitor(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("串口终端（修复闪退版）")
        self.geometry("1200x800")  # 适当减小初始尺寸，避免高分辨率下显示异常
        self.configure(bg="#2d2d2d")
        
        # 核心状态变量（线程安全相关）
        self.running = False  # 串口运行状态（主线程控制）
        self.data_queue = Queue()  # 子线程→主线程：串口数据缓存（关键）
        self.thread_lock = threading.Lock()  # 保护共享资源（串口、日志文件）
        self.ui_lock = threading.Lock()  # 保护UI组件操作

        # pyte虚拟终端（仅主线程操作）
        self.cols = 120
        self.rows = 100
        self.screen = pyte.Screen(self.cols, self.rows)
        self.stream = pyte.Stream(self.screen)
        self.screen.set_history(1000)
        self.auto_scroll = True

        # 创建UI
        self.create_widgets()

        # 字体设置（兼容无Consolas字体的环境）
        self.font_families = ["Consolas", "Microsoft YaHei", "SimHei", "Arial"]
        self.font_size = 10
        self.font = self.get_available_font()  # 获取可用字体
        self.update_char_size()  # 计算字符尺寸

        # 初始化Canvas文本项
        self.visible_lines = 40  # 减少初始可见行数，降低刷新压力
        self.text_items = [
            self.canvas.create_text(0, 0, text="", anchor="nw", fill="white", font=self.font)
            for _ in range(self.visible_lines)
        ]

        # 主线程任务（定时执行，避免子线程操作UI）
        self.schedule_main_tasks()

        log("程序启动成功")

    def get_available_font(self):
        """获取系统可用的字体（避免因字体不存在闪退）"""
        available = self.fonts()  # 获取系统所有可用字体
        for font in self.font_families:
            if font in available:
                log(f"使用字体: {font}")
                return (font, self.font_size)
        # 兜底字体
        log(f"未找到指定字体，使用默认字体")
        return ("Arial", self.font_size)

    def update_char_size(self):
        """计算字符宽高（避免固定值导致的显示异常）"""
        try:
            # 创建临时标签测量字体
            temp = tk.Label(self, font=self.font, text="0")
            temp.update_idletasks()
            self.char_width = temp.winfo_width()  # 字符宽度
            self.char_height = temp.winfo_height()  # 字符高度
            temp.destroy()
            log(f"字符尺寸: 宽={self.char_width}, 高={self.char_height}")
        except Exception as e:
            log(f"计算字符尺寸失败: {str(e)}")
            # 失败时用默认值
            self.char_width = 10
            self.char_height = 20

    def create_widgets(self):
        """创建UI组件（所有组件操作在主线程）"""
        try:
            style = ttk.Style(self)
            style.theme_use('clam')

            # 顶部控制区
            frm_top = ttk.Frame(self)
            frm_top.pack(fill='x', padx=10, pady=5)

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
            self.log_path_var = tk.StringVar(value=os.path.join(os.path.expanduser("~"), "Desktop", "serial_logs"))
            ttk.Entry(frm_log, textvariable=self.log_path_var).pack(side='left', fill='x', expand=True, padx=5)
            ttk.Button(frm_log, text="选择", command=self.browse_folder).pack(side='left')

            # 显示区（Canvas+滚动条）
            display_frame = ttk.Frame(self)
            display_frame.pack(fill='both', expand=True, padx=10, pady=5)

            self.canvas = tk.Canvas(display_frame, bg='black')
            self.vbar = tk.Scrollbar(display_frame, orient="vertical", command=self.on_scroll)
            self.canvas.config(yscrollcommand=self.vbar.set)

            self.canvas.pack(side="left", fill="both", expand=True)
            self.vbar.pack(side="right", fill="y")

            # 输入区
            frm_input = ttk.Frame(self)
            frm_input.pack(fill='x', padx=10, pady=(0,10))
            self.ent_input = ttk.Entry(frm_input)
            self.ent_input.pack(side='left', fill='x', expand=True)
            self.ent_input.bind('<Return>', self.send_command)
            ttk.Button(frm_input, text="发送", command=self.send_command).pack(side='left', padx=5)

            # 状态变量
            self.paused = False
            self.timeStamp = False
            self.serial_port = None
            self.logfile = None

        except Exception as e:
            log(f"创建UI失败: {str(e)}")
            messagebox.showerror("错误", f"UI初始化失败: {str(e)}")
            self.destroy()

    def schedule_main_tasks(self):
        """主线程定时任务（避免子线程操作UI）"""
        # 处理缓存的串口数据（子线程→主线程）
        self.process_data_queue()
        # 刷新显示
        self.refresh_screen()
        # 继续定时执行（50ms一次，平衡流畅度和性能）
        self.after(50, self.schedule_main_tasks)

    def process_data_queue(self):
        """处理子线程发送的串口数据（主线程执行）"""
        if self.paused:
            return
        try:
            # 批量处理队列中的数据（避免频繁刷新）
            while not self.data_queue.empty():
                data = self.data_queue.get()
                self.stream.feed(data)  # pyte操作在主线程
                self.data_queue.task_done()
        except Exception as e:
            log(f"处理数据队列失败: {str(e)}")

    def get_ports(self):
        """获取可用串口（主线程执行）"""
        try:
            ports = [p.device for p in serial.tools.list_ports.comports()]
            log(f"可用串口: {ports}")
            return ports
        except Exception as e:
            log(f"获取串口列表失败: {str(e)}")
            return []

    def refresh_ports(self):
        """刷新串口列表（主线程执行）"""
        try:
            with self.ui_lock:
                self.cb_ports['values'] = self.get_ports()
        except Exception as e:
            log(f"刷新串口失败: {str(e)}")

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
        """打开串口（主线程启动子线程）"""
        try:
            port = self.cb_ports.get()
            baud = self.cb_baud.get()
            if not port or not baud:
                messagebox.showwarning("提示", "请选择串口和波特率")
                return

            # 先停止之前的连接
            self.stop()

            # 创建日志目录
            log_dir = self.log_path_var.get()
            os.makedirs(log_dir, exist_ok=True)

            # 打开串口
            with self.thread_lock:
                self.serial_port = serial.Serial(
                    port=port,
                    baudrate=int(baud),
                    timeout=0.5,
                    parity=serial.PARITY_NONE,
                    stopbits=serial.STOPBITS_ONE,
                    bytesize=serial.EIGHTBITS
                )
                log(f"串口打开成功: {port} {baud}")

            # 打开日志文件
            date_str = datetime.now().strftime("%Y%m%d_%H%M%S")
            log_filename = f"{port.replace('/', '_')}_{date_str}.log"
            log_path = os.path.join(log_dir, log_filename)
            self.logfile = open(log_path, "a", encoding="utf-8")
            log(f"日志文件打开: {log_path}")

            # 更新状态
            self.running = True
            self.btn_start.config(state='disabled')
            self.btn_stop.config(state='normal')
            self.btn_pause.config(state='normal')
            self.btn_sysTime.config(state='normal')

            # 启动串口读取线程（守护线程）
            threading.Thread(target=self.read_serial, daemon=True).start()
            log("串口读取线程启动")

        except Exception as e:
            log(f"打开串口失败: {str(e)}")
            messagebox.showerror("错误", f"打开串口失败: {str(e)}")
            self.stop()  # 确保资源释放

    def stop(self):
        """关闭串口（主线程执行，释放资源）"""
        try:
            self.running = False  # 通知子线程停止
            # 关闭串口
            with self.thread_lock:
                if self.serial_port and self.serial_port.is_open:
                    self.serial_port.close()
                    log("串口已关闭")
                self.serial_port = None
            # 关闭日志文件
            if self.logfile:
                self.logfile.close()
                log("日志文件已关闭")
                self.logfile = None
            # 更新UI状态
            with self.ui_lock:
                self.btn_start.config(state='normal')
                self.btn_stop.config(state='disabled')
                self.btn_pause.config(state='disabled')
                self.btn_sysTime.config(state='disabled')
        except Exception as e:
            log(f"关闭串口失败: {str(e)}")

    def read_serial(self):
        """读取串口数据（子线程，只负责读数据，不操作UI）"""
        log("串口读取线程开始运行")
        while self.running:
            try:
                # 检查串口是否有效
                with self.thread_lock:
                    if not (self.serial_port and self.serial_port.is_open):
                        time.sleep(0.1)
                        continue
                # 读取数据（最多1024字节）
                raw = self.serial_port.read(1024)
                if not raw:
                    time.sleep(0.01)  # 减少空循环CPU占用
                    continue
                # 解码（错误用�替代，避免崩溃）
                line = raw.decode(errors='replace')
                # 处理换行
                fixed_line = line.replace('\n', '\r\n')
                # 加时间戳
                timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
                if self.timeStamp:
                    display_line = f"[{timestamp}] {fixed_line}"
                else:
                    display_line = fixed_line
                # 写入日志
                with self.thread_lock:
                    if self.logfile:
                        self.logfile.write(f"{timestamp} {fixed_line}\n")
                        self.logfile.flush()
                # 发送到主线程（通过队列，避免子线程操作pyte）
                self.data_queue.put(display_line)
            except Exception as e:
                log(f"串口读取错误: {str(e)}")
                # 出错后尝试重连（仅在运行状态）
                if self.running:
                    log("尝试重新连接串口...")
                    with self.thread_lock:
                        self.serial_port = None
                    time.sleep(2)
                    # 主线程控制重连（子线程不直接调用start）
                    self.after(0, self.start)  # 让主线程执行重连
                time.sleep(1)
        log("串口读取线程已停止")

    def refresh_screen(self):
        """刷新Canvas显示（主线程执行）"""
        if self.paused:
            return
        try:
            # 获取显示数据（history + 当前显示）
            lines = list(self.screen.history) + self.screen.display
            total_lines = len(lines)
            if total_lines == 0:
                return  # 无数据不刷新
            # 计算可见区域（滚动位置）
            try:
                # 获取当前滚动位置（相对Canvas的顶部）
                canvasy = self.canvas.canvasy(0)
                # 计算起始行（避免负索引）
                y0 = max(0, int(canvasy // self.char_height))
            except:
                y0 = 0  # 滚动计算失败时从0开始
            # 计算结束行（可见行数）
            y1 = y0 + self.visible_lines
            end_line = min(y1, total_lines)  # 不超过总行数
            # 更新可见行文本
            for i, line_idx in enumerate(range(y0, end_line)):
                if i >= len(self.text_items):
                    break  # 避免索引超过文本项数量
                # 获取行内容（拼接字符列表）
                line = "".join(lines[line_idx]).rstrip()  # 移除右侧空格
                # 更新Canvas文本
                self.canvas.itemconfig(self.text_items[i], text=line)
                # 设置位置（x=2避免贴边，y=行索引×字符高度）
                self.canvas.coords(self.text_items[i], 2, line_idx * self.char_height)
            # 更新滚动区域（总高度=总行数×字符高度）
            self.canvas.config(
                scrollregion=(0, 0, self.char_width * self.cols, self.char_height * total_lines)
            )
            # 自动滚动到底部（如果开启）
            if self.auto_scroll:
                self.canvas.yview_moveto(1.0)
        except Exception as e:
            log(f"刷新显示失败: {str(e)}")

    def on_scroll(self, *args):
        """滚动事件（主线程执行）"""
        self.auto_scroll = False  # 手动滚动时关闭自动跟随
        self.canvas.yview(*args)

    def toggle_pause(self):
        """切换暂停状态（主线程执行）"""
        self.paused = not self.paused
        btn_text = "恢复显示" if self.paused else "暂停显示"
        self.btn_pause.config(text=btn_text)
        log(f"显示已{('暂停' if self.paused else '恢复')}")

    def toggle_timestamp(self):
        """切换时间戳（主线程执行）"""
        self.timeStamp = not self.timeStamp
        btn_text = "关闭时间戳" if self.timeStamp else "显示时间戳"
        self.btn_sysTime.config(text=btn_text)
        log(f"时间戳已{('开启' if self.timeStamp else '关闭')}")

    def send_command(self, event=None):
        """发送命令（主线程执行）"""
        try:
            with self.thread_lock:
                if not (self.serial_port and self.serial_port.is_open):
                    messagebox.showinfo("提示", "请先打开串口")
                    return
            # 获取输入内容
            cmd = self.ent_input.get().strip()
            if not cmd:
                return
            # 发送（加回车）
            with self.thread_lock:
                self.serial_port.write((cmd + '\r\n').encode())
            log(f"发送命令: {cmd}")
            # 清空输入框
            self.ent_input.delete(0, tk.END)
        except Exception as e:
            log(f"发送命令失败: {str(e)}")
            messagebox.showerror("错误", f"发送失败: {str(e)}")

    def on_close(self):
        """关闭窗口（释放所有资源）"""
        log("开始关闭程序")
        self.stop()  # 确保串口和日志关闭
        # 等待数据队列处理完成
        self.data_queue.join()
        log("程序已关闭")
        self.destroy()


if __name__ == "__main__":
    try:
        app = PyteSerialMonitor()
        app.protocol("WM_DELETE_WINDOW", app.on_close)
        app.mainloop()
    except Exception as e:
        log(f"程序崩溃: {str(e)}")
        messagebox.showerror("崩溃", f"程序异常退出: {str(e)}")