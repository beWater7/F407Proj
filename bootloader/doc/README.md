# Bootloader 排障与工具文档

本目录记录排查与修复过程，供后续维护参考。

| 文档 | 内容 |
| --- | --- |
| [01-startup-code-issues.md](./01-startup-code-issues.md) | 启动代码/链接脚本/Makefile 里发现并修复的问题 |
| [02-pyocd-usage.md](./02-pyocd-usage.md) | pyOCD 的安装、烧录、在线调试常用方法 |
| [03-debug-process.md](./03-debug-process.md) | 本次"板子烧录后没反应"问题的排查指令、思路与结论 |
| [04-ymodem-app-upgrade.md](./04-ymodem-app-upgrade.md) | YMODEM 串口升级 APP：Boot/PC 原方案缺陷与修复 |
| [05-serialterm-dtr-reset.md](./05-serialterm-dtr-reset.md) | serialTerm3 关开串口误复位与启动日志抓取 |

## 快速结论

工程 `bootloader/` 是一个 F407 的 Makefile 工程，之前生成的 `loader.bin` 烧录后板子没有任何反应。
排查后确认是 **两个独立问题叠加**造成的：

1. **启动代码缺少 `.data`/`.bss` 初始化**（详见文档1）——带初值的全局变量在RAM里是随机值。
2. **`TIM3_IRQHandler` 被声明成了 `static`**（详见文档3）——链接器无法用它覆盖启动文件里的弱符号，
   导致开机十几毫秒后 TIM3 中断一触发就卡死在 `Default_Handler` 的死循环里，
   连串口开机日志都打不出来，表现就是"烧录后板子没反应"。

以上问题均已修复并在实际硬件上用 pyOCD + SWD 验证通过。

后续又补齐：

- **YMODEM 真正写入 APP1**，并修好 PC 端与读线程抢字节、`upgrade` 中 `'a'` 误中止等问题（文档4）。
- **串口工具默认不再因关开而误复位**，用「复位」脉冲抓启动日志（文档5）。
