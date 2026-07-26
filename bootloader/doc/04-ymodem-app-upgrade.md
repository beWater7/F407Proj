# 04 - YMODEM 串口升级 APP 修复

本文记录 Bootloader 通过 USART1 + YMODEM 接收 `app.bin` 并写入 APP1（`0x08008000`）的故障排查与修复。涉及固件侧与 PC 工具 `serialTerm3.py`。

## 目标流程

```
PC(serialTerm3) --upgrade--> Boot CLI
                           --> Ymodem_Receive 发 'C'
PC --SOH 头包 / STX 1K 数据包--> Boot：CRC 校验 → 擦扇区 → 写 Flash
PC --EOT + 空 SOH--> Boot 成功：dwCurrentAppAddr = APP1，提示 goto/reset 跳转
```

不走 SPI OTA 备份路径；`upgrade` CLI 语义就是「串口直接刷运行槽 APP1」。

## 之前方案的问题

### Bootloader 侧（致命）

相关文件：

- `User/app/ymodem/ymodem.c`
- `User/app/upgrade/download.c`
- `User/app/shell/cmd_src.c`
- `User/bsp/internalFlash/bsp_internalFlash.c`（应复用，旧代码未用）

| 问题 | 现象 / 后果 |
| --- | --- |
| `YMODEM_DATA_TO_FLASH` 未定义 | Flash 擦写整段被 `#if` 裁掉，收包成功但 APP 区域从未编程 |
| `byYmodemFile` 的 `malloc` 已注释，指针为 NULL | 每包 `memcpy` 到固定 `tab_1024`，且缓冲指针不前进，只保留最后 1KB |
| CRC 比较被注释 | 坏包也会 ACK，固件可能损坏却仍报成功 |
| 传输中 `os_debug` / `printf` 走同一 USART1 | 日志与 ACK/`C` 混在一起，PC 端协议错乱 |
| 旧擦写 API 是 F1 的 `FLASH_ErasePage` / 1KB page | 不适用于 F407 扇区模型 |
| `SerialDownload` 成功后无收尾 | 不设置 `dwCurrentAppAddr`，不提示跳转；还对空缓冲做 dump |

### serialTerm3 侧（致命 / 高危）

相关文件：仓库根目录 `serialTerm3.py`

| 问题 | 现象 / 后果 |
| --- | --- |
| GUI 读线程与 YMODEM `read(1)` 抢字节 | ACK/`C` 被偷走 → 超时、`文件头应答错误` |
| 点「YMODEM上传」总是再发一遍 `upgrade\r` | 若已手动敲过 `upgrade`，板子在发 `CCCC`；命令里的字母 **`a` = YMODEM ABORT2**，直接「Aborted by user」 |
| 在 Tk 主线程同步发送 | 界面长时间卡死（次要） |
| 头包后未留足擦扇区时间 | 大文件擦多扇区时 ACK 偏慢，易误判超时 |

典型误操作日志：

```
cmdbuf:upgrade
Waiting for the file to be sent ...
CCCCCCCC[YMODEM] 发送 upgrade 进入接收模式...
[YMODEM] 文件头包已发送
[YMODEM] 文件头应答错误
（0x18 CA）
Aborted by user.
```

## 修复方法

### 1. Boot：真正收包并写 APP1

- 恢复 **CRC16** 校验，失败回 NAK，不 ACK。
- 去掉传输过程中协议通道上的调试打印。
- 头包解析 `size` 后：校验 `0 < size <= APP_FLASH_SIZE`；用 `internal_flash_erase` + `GetNextSectorAddr` 按地址范围擦除。
- 数据包：`internal_flash_write(FlashDestination, payload, write_len)`，末包写入长度取 `min(packet_len, remain)`（去掉 0x1A 填充）。
- 不再依赖空的 `byYmodemFile`；`tab_1024` 仅作包暂存。
- EOT 后发 ACK + `'C'`，再收空文件名包结束会话；未写满不返回成功。
- `SerialDownload` 成功：打印 Size，`dwCurrentAppAddr = APP1_ADDRESS`，提示 `goto 0` / `reset`。
- `cmd_update`：结束后恢复 `MODE_CMD`。

### 2. PC：暂停读线程 + 避免二次 upgrade

- YMODEM 期间 `reader_paused`，读线程只 sleep，不 `read`。
- 发送放到后台线程，避免卡死 Tk。
- 先短时探测是否已有 `'C'`：
  - **已有** → 说明已在 Waiting，**不再发送** `upgrade\r`，直接传文件；
  - **没有** → 再发 `upgrade\r` 并等待 `'C'`。
- 头包后等待 ACK+`C` 超时加长（擦扇区可能数秒）；EOT 兼容先 NAK 再第二次 EOT；进度按实际字节计算。

## 验证步骤

1. 编译烧录 bootloader（`buildBoot.sh` / pyOCD @ `0x08000000`）。
2. 打开 `serialTerm3.py`，倒计时按 `U` 进 CLI。
3. **只点「YMODEM上传」选 `app/build/app.bin`**（或先手动 `upgrade` 再点上传——此时不应再发 upgrade）。
4. 看到 `Programming Completed Successfully!` 与 Size。
5. `reset` / `goto 0`，确认 APP 串口有 `[APP] start` 等输出。

## 使用注意

- 不要在板子已 `CCCC` 时再手动/自动重复发带字母 `a` 的命令（`upgrade`）。
- 中止接收请按协议约定的 `a`/`A`，不要用 CLI 命令字当协议输入。
- APP 链接地址必须是 `0x08008000`（与 `app/link.ld`、`APP1_ADDRESS` 一致）。

## 关键文件一览

| 文件 | 改动要点 |
| --- | --- |
| `bootloader/User/app/ymodem/ymodem.c` | CRC、擦写 Flash、静默传输、末包长度 |
| `bootloader/User/app/upgrade/download.c` | 成功收尾、设置 APP 地址 |
| `bootloader/User/app/shell/cmd_src.c` | `upgrade` 结束后回 MODE_CMD |
| `serialTerm3.py` | 暂停读线程、探测 C、后台发送 |
