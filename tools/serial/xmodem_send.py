#!/usr/bin/env python3
"""xmodem_send.py — 通过 UART XMODEM-CRC 向固化 boot 发送 loader（兜底恢复）。

用法::
    python3 tools/serial/xmodem_send.py <serial> <loader_blob> [--baud 115200]

- <loader_blob>: [loader_header_t(16B)][loader.bin]。
  `python3 tools/pack/make_loader_blob.py loader/build/loader.bin`
- boot 握手连发 'C'，本工具收到后开始发块；每块 128B，CRC16(0x1021)。
- 日常升级请用 C 工具: ./tools/xfer/xfer /dev/ttyUSB0 dist/upg.bin
"""

import argparse
import serial
import struct
import sys
import time
from pathlib import Path

SOH, EOT, ACK, NAK, CAN = 0x01, 0x04, 0x06, 0x15, 0x18
BLOCK = 128


def crc16(data: bytes) -> int:
    crc = 0
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if crc & 0x8000 else (crc << 1)
        crc &= 0xFFFF
    return crc


def recv_byte(ser, timeout: float) -> int | None:
    ser.timeout = timeout
    b = ser.read(1)
    return b[0] if b else None


def send(ser, payload: bytes) -> None:
    """XMODEM-CRC 发送。payload = [16B loader_header_t][loader.bin]。"""
    if len(payload) < 16:
        raise SystemExit("payload too small (<16B loader header)")
    magic, ver, size, crc = struct.unpack_from("<IIII", payload, 0)
    if magic != 0x52444C32:
        print(f"WARN: header magic 0x{magic:08X} != LOADER_HDR_MAGIC", file=sys.stderr)
    if size != len(payload) - 16:
        print(f"WARN: hdr.size {size} != body {len(payload)-16}", file=sys.stderr)

    # 握手：等 'C'
    ser.reset_input_buffer()
    deadline = time.time() + 10
    got_c = False
    while time.time() < deadline:
        b = recv_byte(ser, 1.0)
        if b == ord("C"):
            got_c = True
            break
    if not got_c:
        raise SystemExit("no 'C' handshake from stage0 (check wiring/baud/power)")
    print("[*] handshake OK")

    # 按 128B 分块，最后一块 0x1A 填充
    padded = payload + b"\x1a" * ((-len(payload)) % BLOCK)
    blocks = [padded[i:i + BLOCK] for i in range(0, len(padded), BLOCK)]
    for i, blk in enumerate(blocks, start=1):
        frame = bytes([SOH, i & 0xFF, (~i) & 0xFF]) + blk
        frame += struct.pack(">H", crc16(blk))
        for _ in range(10):  # 每块最多重发 10 次
            ser.write(frame)
            b = recv_byte(ser, 5.0)
            if b == ACK:
                break
            if b == CAN:
                raise SystemExit("receiver aborted (CAN)")
            print(f"[!] blk {i}: NAK/recv 0x{b:02X}, retry", file=sys.stderr)
        else:
            raise SystemExit(f"blk {i}: no ACK after retries")
        if i % 8 == 0:
            print(f"[*] sent block {i}/{len(blocks)}")

    # EOT 结束
    for _ in range(5):
        ser.write(bytes([EOT]))
        b = recv_byte(ser, 5.0)
        if b == ACK:
            print("[*] EOT ACK, transfer complete")
            return
    raise SystemExit("EOT not ACKed")


def main() -> int:
    ap = argparse.ArgumentParser(description="UART XMODEM-CRC 发送 loader 到 stage0")
    ap.add_argument("port", help="串口设备，如 /dev/ttyUSB0")
    ap.add_argument("blob", type=Path, help="loader blob（[16B头][loader.bin]）")
    ap.add_argument("--baud", type=int, default=115200)
    args = ap.parse_args()

    if not args.blob.is_file():
        raise SystemExit(f"blob not found: {args.blob}")
    try:
        ser = serial.Serial(args.port, args.baud, timeout=1.0)
    except Exception as e:
        raise SystemExit(f"open {args.port}: {e}")

    payload = args.blob.read_bytes()
    print(f"[*] {args.blob} {len(payload)}B -> {args.port} @{args.baud}")
    try:
        send(ser, payload)
    except KeyboardInterrupt:
        pass
    finally:
        ser.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
