# 05 - 串口工具开关口误复位与启动日志

本文记录 PC 工具 `serialTerm3.py` 在「关闭再打开串口」时板子被复位的原因，以及如何在**不关串口**的情况下稳定观察 Boot/APP 启动日志。

## 现象

1. **关闭串口再打开** → 板子重启，终端能看到 boot 倒计时 / logo。
2. **保持串口打开**、板子因其它原因复位时 → 若当时没开串口或工具把 DTR 弄乱，容易**漏掉启动日志**。
3. 工具上旧的「复位」按钮只把 DTR/RTS 设为 `False`，多数情况下**不会真正复位** MCU。

用户容易形成习惯：靠「关开串口」来抓启动日志——这其实是 USB 转串口线电平跳变带来的**误复位**，不是可靠流程。

## 根因

野火等板载 **CH340 / USB-UART** 常见接法：

- `DTR` / `RTS` 经电容或电路接到 MCU 的 **NRST**（或等效复位脚）。
- Windows 上 `serial.Serial(port, ...)` **一 open**，驱动常会先拉高再变化 DTR/RTS → 等效脉冲复位。
- **close** 时线电平也可能再跳一次。

因此：

| 操作 | 实际效果 |
| --- | --- |
| 关串口再开 | 几乎总会复位 →「碰巧」能看到启动日志 |
| 开串口后再 `setDTR(False)` | 往往**太晚**：复位脉冲已经发生 |
| 只 `setDTR(False); setRTS(False)` 当复位 | 若本来就是 False，**没有边沿**，板子不动 |

这与 YMODEM / 业务逻辑无关，是**串口控件与硬件复位回路**的问题。

## 之前方案的问题

`serialTerm3.py` 里原先大致是：

```python
self.serial_port = serial.Serial(port, baud, timeout=0.5)
self.serial_port.setDTR(False)
self.serial_port.setRTS(False)
```

以及：

```python
def reset(self):
    self.serial_port.setDTR(False)
    self.serial_port.setRTS(False)
```

问题归纳：

1. **open 之后**再拉低 DTR/RTS，拦不住 open 瞬间的复位。
2. 没有「是否允许打开时复位」的开关，关开串口行为不可控。
3. 「复位」没有做**高低脉冲**，不能在保持串口打开时主动抓 boot 日志。
4. `close` 前未主动拉低控制线，部分驱动关闭时仍可能抖动复位。

## 修复方法

### 1. 打开前先固定 DTR/RTS，默认不复位

```python
ser = serial.Serial()
ser.port = port
ser.baudrate = baud
ser.dtr = False   # 必须在 open 前
ser.rts = False
ser.open()
```

默认目标：**关开串口尽量不再误复位**。

### 2. 增加「打开时复位」勾选框

- **不勾选（默认）**：仅打开串口监听，不故意复位。
- **勾选**：打开后主动做一次 DTR/RTS 脉冲，等价于「打开并复位抓日志」。

### 3. 「复位」改为脉冲，串口保持打开

推荐抓启动日志的方式：

1. 串口一直开着；
2. 点工具上的 **「复位」**；
3. 终端直接打印 Boot →（可选）进 CLI / 跳 APP 的完整日志。

脉冲逻辑（CH340/野火常见极性，若板子无反应可再改极性）：

```text
DTR/RTS = 0 → 短暂 1 → 再回到 0
```

YMODEM 传输中禁止点复位，避免协议中断。

### 4. 关闭前先拉低再 close

减轻 close 时的线电平跳变导致的意外复位。

## 推荐使用方式

| 需求 | 做法 |
| --- | --- |
| 日常调试、看运行日志 | 打开串口，**不要**勾选「打开时复位」，不要关开串口 |
| 要看完整 boot 日志 | 串口保持打开 → 点 **「复位」** |
| 仍想「一打开就复位」 | 勾选 **「打开时复位」** 再点打开串口 |
| 用 pyOCD 复位 | `pyocd reset -t stm32f407zgtx`，串口工具保持打开同样可抓日志 |

## 若「复位」无效

不同板子 DTR/RTS 与 NRST 的极性、是否接电容不一致。可尝试：

1. 对调脉冲中「先 True 再 False」与「先 False 再 True」；
2. 只脉冲 DTR 或只脉冲 RTS；
3. 用按键/NRST 或 pyOCD 复位，同时保持串口打开验证监听是否正常。

## 影响文件

| 文件 | 改动要点 |
| --- | --- |
| `serialTerm3.py` | `_open_serial`、`_pulse_mcu_reset`、「打开时复位」勾选、复位按钮脉冲、close 前拉低 |

## 小结

- **关开串口能看到启动日志** = 误用了 USB-UART 的复位副作用。
- **正确做法** = 串口常开 + 主动「复位」脉冲（或勾选打开时复位），而不是反复 close/open。
