import tkinter as tk
import pyte
import time
import threading

class PyteDemo(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("pyte demo")
        self.geometry("800x600")
        
        self.cols = 80
        self.rows = 20
        self.screen = pyte.Screen(self.cols, self.rows)
        self.screen.history = pyte.History(1000)
        self.stream = pyte.Stream(self.screen)

        self.font = ("Consolas", 12)
        self.char_width = 8
        self.char_height = 20

        self.canvas = tk.Canvas(self, bg="black")
        self.canvas.pack(fill="both", expand=True)

        self.visible_lines = 40
        self.text_items = []
        for i in range(self.visible_lines):
            item = self.canvas.create_text(
                0, 0, text="", anchor="nw", fill="white", font=self.font
            )
            self.text_items.append(item)

        # 启动模拟串口线程
        threading.Thread(target=self.mock_input, daemon=True).start()

        # 启动刷新
        self.after(100, self.refresh_screen)

    def mock_input(self):
        # 每秒写一行，带换行，保证 pyte 会滚动
        count = 0
        while True:
            line = f"Line {count}\n"
            self.stream.feed(line)
            count += 1
            time.sleep(0.2)

    def refresh_screen(self):
        lines = list(self.screen.history) + self.screen.display
        total_lines = len(lines)

        for i in range(self.visible_lines):
            y = total_lines - self.visible_lines + i
            if y < 0:
                self.canvas.itemconfig(self.text_items[i], text="")
                continue
            line = "".join(lines[y]).rstrip()
            self.canvas.itemconfig(self.text_items[i], text=line)
            self.canvas.coords(self.text_items[i], 2, i * self.char_height)

        self.canvas.config(
            scrollregion=(0, 0, self.char_width * self.cols, self.char_height * total_lines)
        )

        self.after(100, self.refresh_screen)

if __name__ == "__main__":
    app = PyteDemo()
    app.mainloop()
