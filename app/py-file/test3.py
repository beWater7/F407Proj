import tkinter as tk
import re

ANSI_RE = re.compile(r'\x1b\[([0-9;]+)m')

class App:
    def __init__(self, root):
        self.root = root
        self.txt_output = tk.Text(root, height=20, width=80)
        self.txt_output.pack()
        self.paused = False

        # 在末尾创建一个标记
        self.txt_output.insert(tk.END, "\n")  # 确保至少有一行
        self.txt_output.mark_set("progress", "end-1c")

        # 模拟数据输入
        self.root.after(1000, self.simulate)

    def ansi_code_to_tag(self, code):
        return 'reset'

    def insert_ansi(self, text):
        if self.paused:
            return

        pos = 0
        tag = 'reset'

        while pos < len(text):
            if text[pos] == '\r':
                # 覆盖标记所在行
                line_start = self.txt_output.index("progress linestart")
                line_end = self.txt_output.index("progress lineend")
                self.txt_output.delete(line_start, line_end)
                pos += 1
                continue

            m = ANSI_RE.search(text, pos)
            if m:
                start, end = m.span()
                self.txt_output.insert("progress", text[pos:start], tag)
                code = m.group(1)
                tag = self.ansi_code_to_tag(code)
                pos = end
            else:
                self.txt_output.insert("progress", text[pos:], tag)
                break

        self.txt_output.see(tk.END)

    def simulate(self):
        # 模拟多次 \r 覆盖进度条
        import time
        for i in range(0, 101, 10):
            self.insert_ansi(f"\r[{'#' * (i // 2):50}] {i}%")
            self.root.update()
            time.sleep(0.5)

root = tk.Tk()
app = App(root)
root.mainloop()
