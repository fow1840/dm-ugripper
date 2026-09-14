#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
DA-Gripper 完整 OTA 更新脚本
============================
功能：
  1. 自动探测 USB 串口（支持 CH340 / CH9344 / WCH 系列）
  2. 查询当前固件版本号
  3. 发送串口触发命令，App 重启进入 Bootloader
  4. 标准 Ymodem 协议发送固件（支持 .bin / .hex 自动解析）
  5. 等待 Bootloader 完成：备份区接收 → CRC 校验 → 拷贝到运行区 → 再次校验 → 重启
  6. 查询升级后固件版本号，验证是否成功

用法：
  python ota_update.py                              # 自动选串口+默认固件路径
  python ota_update.py --port COM116                # 指定串口
  python ota_update.py --port COM116 --file app.hex # 指定串口和固件
  python ota_update.py --check-only                 # 只查询版本，不升级
  python ota_update.py --baud 230400                # 指定波特率

协议：
  OTA 触发   : 5A 5A 42 01 00 00 <XOR>
  版本查询   : 5A 5A 52 56 00 00 <XOR>  ->  返回 16 字节 ASCII 版本号
  固件传输   : 标准 Ymodem（1024 字节数据帧 + CRC16-XMODEM 校验）
"""

import argparse
import os
import sys
import time
import re
import serial
import serial.tools.list_ports


# ===================== 默认配置 =====================
DEFAULT_BAUDRATE = 115200
DEFAULT_FIRMWARE_HEX = os.path.join(
    os.path.dirname(os.path.abspath(__file__)),
    "DA-Gripper", "Output", "DA-Gripper.hex"
)

# Ymodem 控制字符
SOH = 0x01   # 128 字节数据帧头
STX = 0x02   # 1024 字节数据帧头
EOT = 0x04   # 传输结束
ACK = 0x06   # 应答
NAK = 0x15   # 非应答 / 请求 CRC 模式重传
CAN = 0x18   # 取消传输
C   = 0x43   # 请求以 CRC16 模式开始

PACKET_128  = 128
PACKET_1024 = 1024


# ===================== 工具函数 =====================

def auto_detect_port():
    """自动探测串口，优先选择 USB-URAT 适配器（跨平台）。"""
    ports = list(serial.tools.list_ports.comports())
    if not ports:
        return None

    # 过滤 Linux 上的非物理串口（/dev/ttyS* 通常是主板内置串口）
    if sys.platform.startswith("linux"):
        ports = [p for p in ports if not p.device.startswith("/dev/ttyS")
                 or "USB" in (p.description or "")]

    # 优先匹配 CH340 / CH9344 / WCH 等常见 USB 串口
    prefer_keywords = ("USB-SERIAL", "CH340", "CH910", "CH9344", "WCH", "USB Serial",
                       "USB-SERIAL CH", "USB2.0-Serial", "TTL")
    candidates = []
    for p in ports:
        desc = (p.description or "") + " " + (p.hwid or "")
        for kw in prefer_keywords:
            if kw in desc:
                candidates.append((1, p.device))
                break
        else:
            candidates.append((0, p.device))

    candidates.sort(key=lambda x: x[0], reverse=True)
    return candidates[0][1] if candidates else None


def crc16_xmodem(data: bytes) -> int:
    crc = 0
    for byte in data:
        crc ^= (byte << 8)
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def parse_hex(filename: str):
    """解析 Intel HEX 文件，返回 (raw_bytes, start_address)。"""
    data = {}
    base_addr = 0
    with open(filename, 'r', encoding='ascii') as f:
        for line in f:
            line = line.strip()
            if not line or not line.startswith(':'):
                continue
            byte_count = int(line[1:3], 16)
            address = int(line[3:7], 16)
            record_type = int(line[7:9], 16)
            data_str = line[9:9 + byte_count * 2]

            if record_type == 0:
                full_addr = base_addr + address
                for i in range(byte_count):
                    data[full_addr + i] = int(data_str[i*2:i*2+2], 16)
            elif record_type == 1:
                break
            elif record_type == 2:
                base_addr = int(data_str, 16) << 4
            elif record_type == 4:
                base_addr = int(data_str, 16) << 16

    if not data:
        return None, 0
    start = min(data.keys())
    end = max(data.keys())
    size = end - start + 1
    result = bytearray(size)
    for a, v in data.items():
        result[a - start] = v
    return bytes(result), start


def load_firmware(path: str):
    """根据扩展名加载固件，返回 (bytes, start_addr)。"""
    ext = os.path.splitext(path)[1].lower()
    if ext == ".hex":
        fw, start = parse_hex(path)
        if fw is None:
            raise RuntimeError(f"HEX 解析失败：{path}")
        # 去除尾部 0xFF 填充（节省传输时间）
        valid_len = len(fw)
        while valid_len > 0 and fw[valid_len - 1] == 0xFF:
            valid_len -= 1
        fw = fw[:valid_len]
        return fw, start
    elif ext == ".bin":
        with open(path, "rb") as f:
            fw = f.read()
        valid_len = len(fw)
        while valid_len > 0 and fw[valid_len - 1] == 0xFF:
            valid_len -= 1
        return fw[:valid_len], 0
    else:
        raise RuntimeError(f"不支持的固件格式：{ext}（仅支持 .hex / .bin）")


# ===================== Ymodem 协议 =====================

def make_packet(seq: int, data: bytes, use_stx: bool = True):
    """构造 Ymodem 数据包（带 CRC16 校验）。"""
    header = STX if use_stx else SOH
    pkt_len = PACKET_1024 if use_stx else PACKET_128
    padded = data + bytes(pkt_len - len(data))
    seq_byte = seq & 0xFF
    seq_comp = (~seq) & 0xFF
    crc = crc16_xmodem(padded)
    return (bytes([header, seq_byte, seq_comp])
            + padded
            + bytes([(crc >> 8) & 0xFF, crc & 0xFF]))


def make_file_info_packet(filename: str, filesize: int):
    """构造 Ymodem 文件信息包（第 0 号包，文件名 + 大小）。"""
    payload = (filename.encode('ascii')
               + b'\x00'
               + str(filesize).encode('ascii')
               + b'\x00')
    payload = payload + bytes(PACKET_128 - len(payload))
    return make_packet(0, payload, use_stx=False)


# ===================== 应用层协议 =====================

def xor_check_4byte(data_buf: bytes) -> int:
    """App 协议校验：DataBuf[0..3] 异或。"""
    return data_buf[0] ^ data_buf[1] ^ data_buf[2] ^ data_buf[3]


def send_ota_trigger(ser: serial.Serial) -> bool:
    """发送 OTA 触发命令：5A 5A 42 01 00 00 XOR。
    XOR = 0x42 ^ 0x01 ^ 0x00 ^ 0x00 = 0x43"""
    data_buf = bytes([0x42, 0x01, 0x00, 0x00])
    xor = xor_check_4byte(data_buf)
    cmd = bytes([0x5A, 0x5A]) + data_buf + bytes([xor])
    print(f"[1/5] 发送 OTA 触发命令：{cmd.hex(' ')}")
    ser.reset_input_buffer()
    ser.write(cmd)
    ser.flush()

    deadline = time.time() + 2.0
    resp = bytearray()
    while time.time() < deadline:
        try:
            b = ser.read(64)
        except serial.SerialException:
            # Linux 下 MCU 复位导致 USB-串口瞬间断开，触发命令已发出
            print("      ✓ (设备复位中，串口短暂断开属正常)")
            return True
        if b:
            resp.extend(b)
            if len(resp) >= 8 and resp[0] == 0xA5 and resp[1] == 0xA5:
                print(f"      App 应答帧：{bytes(resp[:8]).hex(' ')}")
                if resp[3] == 0x54:
                    print("      ✓ App 确认 OTA，即将重启进入 Bootloader")
                    return True
                else:
                    print(f"      ✗ 应答错误码：0x{resp[3]:02X}")
                    return False
    if resp:
        print(f"      原始响应：{bytes(resp[:32]).hex(' ')}")
    return True  # 某些版本可能不发响应，依然继续尝试


def query_version(ser: serial.Serial, label: str = ""):
    """查询版本号：5A 5A 52 56 00 00 XOR -> 返回 16 字节版本号或 None。
    XOR = 0x52 ^ 0x56 ^ 0x00 ^ 0x00 = 0x04"""
    data_buf = bytes([0x52, 0x56, 0x00, 0x00])
    xor = xor_check_4byte(data_buf)
    cmd = bytes([0x5A, 0x5A]) + data_buf + bytes([xor])
    if label:
        print(f"\n{label}")
    print(f"      查询命令：{cmd.hex(' ')}")
    ser.reset_input_buffer()
    ser.write(cmd)
    ser.flush()

    resp = bytearray()
    deadline = time.time() + 3.0
    while time.time() < deadline:
        b = ser.read(256)
        if b:
            resp.extend(b)
        # 已有 16 字节以上，尝试解析
        if len(resp) >= 16:
            break

    if not resp:
        print("      ✗ 无响应")
        return None

    # 找版本号字符串：以 'V' (0x56) 开头的 ASCII 串
    raw = bytes(resp)
    ver_bytes = None
    for i in range(len(raw) - 15):
        candidate = raw[i:i+16]
        # V1.x.x 格式：0x56(V) + 数字 + 0x2E(.) + 数字 + 0x2E(.) + 数字
        if (candidate[0] == 0x56
                and 0x30 <= candidate[1] <= 0x39
                and candidate[2] == 0x2E
                and 0x30 <= candidate[3] <= 0x39
                and candidate[4] == 0x2E
                and 0x30 <= candidate[5] <= 0x39):
            ver_bytes = candidate
            break

    if ver_bytes is None and len(raw) >= 16:
        # 没找到典型格式，取最后 16 字节
        ver_bytes = raw[-16:]

    if ver_bytes is None:
        print(f"      响应不足 16 字节 ({len(raw)} 字节)：{raw[:48].hex(' ')}")
        return None

    try:
        ver_str = ver_bytes.rstrip(b'\x00').rstrip(b'\xff').decode('ascii')
    except UnicodeDecodeError:
        ver_str = ""

    if re.fullmatch(r"V[0-9]+\.[0-9]+\.[0-9]+(?:_[0-9]+)?", ver_str):
        print(f"      ✓ 版本号：{ver_str}")
        print(f"        16 字节原始：{ver_bytes.hex(' ')}")
        return ver_str
    else:
        print(f"      响应 ({len(raw)} 字节)：{raw[:48].hex(' ')}")
        return None


# ===================== Ymodem 发送主流程 =====================

def do_ymodem_tx(ser: serial.Serial, firmware: bytes) -> bool:
    """标准 Ymodem：等待 'C' → 文件信息包 → 数据包 → 双 EOT → 空结束包。"""
    num_packets = (len(firmware) + PACKET_1024 - 1) // PACKET_1024
    print(f"\n[2/5] Ymodem 准备：{len(firmware)} 字节 → {num_packets} 个数据帧")

    # 步骤 A：等待 Bootloader 发来的 'C'（要求 CRC16 模式）
    print("      等待 Bootloader 发送 'C' ...", end="", flush=True)
    got_c = False
    deadline = time.time() + 15.0
    while time.time() < deadline:
        b = ser.read(1)
        if b:
            if b[0] == C:
                got_c = True
                print(" ✓")
                break
            elif b[0] == NAK:
                print(" NAK（期望 ACK/CRC 模式）继续等待...")
            else:
                print(f" 收到 0x{b[0]:02X}, 继续等待...")
    if not got_c:
        print(" ✗ 超时未收到 'C'，请检查 Bootloader 是否启动")
        return False

    # 步骤 B：发送文件信息包 (seq=0, 文件名+大小)
    p0 = make_file_info_packet("app.bin", len(firmware))
    print(f"[3/5] 发送文件信息包 ...", end="", flush=True)
    deadline = time.time() + 10.0
    ack_ok = False
    while time.time() < deadline:
        ser.write(p0)
        ser.flush()
        start_wait = time.time()
        while time.time() - start_wait < 2.0:
            b = ser.read(1)
            if not b:
                continue
            if b[0] == ACK:
                ack_ok = True
                print(" ✓ ACK")
                # 有些 Bootloader 在 ACK 后还会发一个 'C'，短暂消费它
                time.sleep(0.2)
                while ser.in_waiting:
                    b2 = ser.read(ser.in_waiting)
                break
            elif b[0] == NAK:
                print(" NAK, 重传", end="", flush=True)
                break
            elif b[0] == C:
                # 'C' 表示准备好接收数据（兼容变体）
                ack_ok = True
                print(" ✓ (收到 C 表示就绪)")
                break
            elif b[0] == CAN:
                print(" ✗ 远端取消")
                return False
        if ack_ok:
            break
    if not ack_ok:
        print(" ✗ 文件信息包未确认")
        return False

    # 步骤 C：发送数据包
    print(f"[4/5] 发送 {num_packets} 个数据帧：")
    for i in range(num_packets):
        offset = i * PACKET_1024
        chunk = firmware[offset:offset + PACKET_1024]
        pkt = make_packet(i + 1, chunk, use_stx=True)
        seq = i + 1

        for attempt in range(5):  # 最多重试 5 次
            ser.write(pkt)
            ser.flush()
            start_wait = time.time()
            ok = False
            while time.time() - start_wait < 5.0:
                b = ser.read(1)
                if not b:
                    continue
                if b[0] == ACK:
                    ok = True
                    break
                elif b[0] == NAK:
                    print(f"        帧 {seq}: NAK, 重试 {attempt+1}/5")
                    break
                elif b[0] == CAN:
                    print(f"        帧 {seq}: ✗ 远端取消")
                    return False
            if ok:
                break
        else:
            print(f"      ✗ 帧 {seq} 重试耗尽，放弃")
            return False

        # 进度输出（每 5 帧或最后一帧打印）
        if seq % 5 == 0 or seq == num_packets:
            percent = seq * 100 // num_packets
            print(f"        帧 {seq:>3}/{num_packets}  ACK  [{percent:>3}%]")

    # 步骤 D：双 EOT 结束（标准 Ymodem：EOT→NAK→EOT→ACK）
    print(f"[5/5] 发送结束帧 ...", end="", flush=True)
    ser.write(bytes([EOT]))
    ser.flush()
    # 等待 NAK 或 ACK
    deadline = time.time() + 5.0
    got_first_resp = False
    finished = False
    while time.time() < deadline:
        b = ser.read(1)
        if not b:
            continue
        if b[0] == NAK and not got_first_resp:
            print(" NAK → 重发 EOT", end="", flush=True)
            ser.write(bytes([EOT]))
            ser.flush()
            got_first_resp = True
            continue
        if b[0] == ACK:
            finished = True
            print(" ✓ ACK (EOT 确认)")
            break
    if not finished:
        print(" ✗ EOT 未确认")
        return False

    # 步骤 E：空文件信息包表示结束传输（标准 Ymodem 可选步骤，Bootloader 会忽略）
    end_pkt = make_file_info_packet("", 0)
    ser.write(end_pkt)
    ser.flush()
    time.sleep(0.5)
    # 丢弃所有入站数据
    while ser.in_waiting:
        ser.read(ser.in_waiting)

    return True


# ===================== 主流程 =====================

def wait_app_start(ser: serial.Serial, timeout: float = 15.0) -> bool:
    """等待 Bootloader 完成 Flash 操作并自动重启进入 App。
    通过检测 a5 a5 开头的上报帧判断 App 已运行。"""
    print(f"\n等待 Bootloader → App 重启（最多 {timeout:.0f}s）...",
          end="", flush=True)
    buf = bytearray()
    deadline = time.time() + timeout
    while time.time() < deadline:
        b = ser.read(256)
        if b:
            buf.extend(b)
            # 检查是否出现 a5 a5 帧头（说明 App 已启动上报数据）
            for i in range(len(buf) - 1):
                if buf[i] == 0xA5 and buf[i+1] == 0xA5:
                    print(" ✓ (检测到 App 数据帧)")
                    return True
    # 如果没检测到帧，但串口还通，也算成功（可能 App 处于空闲未上报状态）
    print(" (超时，但继续下一步验证)")
    return True


def main() -> int:
    parser = argparse.ArgumentParser(
        description="DA-Gripper OTA 升级工具（串口触发 + Ymodem）",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__
    )
    parser.add_argument("--port", "-p", help="串口 (例: COM116, /dev/ttyUSB0)")
    parser.add_argument("--file", "-f",
                        default=DEFAULT_FIRMWARE_HEX,
                        help=f"固件路径 (.hex / .bin)，默认：{DEFAULT_FIRMWARE_HEX}")
    parser.add_argument("--baud", "-b", type=int, default=DEFAULT_BAUDRATE,
                        help=f"波特率，默认 {DEFAULT_BAUDRATE}")
    parser.add_argument("--check-only", action="store_true",
                        help="只查询版本号，不进行 OTA")
    parser.add_argument("--timeout-reset", type=float, default=2.0,
                        help="OTA 触发后重启等待时间（秒）")
    args = parser.parse_args()

    print("=" * 62)
    print("  DA-Gripper OTA 升级工具")
    print("=" * 62)

    # --- 串口选择 ---
    port = args.port or auto_detect_port()
    if not port:
        print("✗ 未找到任何串口，请手动通过 --port 指定")
        return 1
    print(f"  串口       : {port}")
    print(f"  波特率     : {args.baud}")

    # --- 固件加载 ---
    if not args.check_only:
        if not os.path.isfile(args.file):
            print(f"✗ 固件文件不存在：{args.file}")
            return 1
        try:
            firmware, start_addr = load_firmware(args.file)
        except Exception as e:
            print(f"✗ 固件加载失败：{e}")
            return 1
        print(f"  固件       : {args.file}")
        print(f"  有效大小   : {len(firmware)} 字节"
              f"  (起始 0x{start_addr:08X})")
    print()

    # --- 打开串口 ---
    try:
        ser = serial.Serial(port, args.baud, timeout=0.1)
    except serial.SerialException as e:
        print(f"✗ 打开串口失败：{e}")
        if sys.platform.startswith("linux") and "Permission" in str(e):
            print(f"  Linux 提示：将用户加入 dialout 组后重新登录：")
            print(f"    sudo usermod -aG dialout $USER")
            print(f"  或临时使用：sudo python {sys.argv[0]} --port {port}")
        return 1

    try:
        # ===== 第一步：查询升级前版本号 =====
        ver_before = query_version(ser, label="─ 查询升级前版本号 ─")
        time.sleep(0.3)

        if args.check_only:
            print("\n（check-only 模式，不进行升级）")
            return 0 if ver_before else 2

        # ===== 第二步：串口触发 OTA =====
        if not send_ota_trigger(ser):
            print("✗ OTA 触发失败，程序退出")
            return 3

        # 等待 App 复位 / Bootloader 启动 (关闭旧串口再重新打开)
        ser.close()
        time.sleep(args.timeout_reset)
        # 重新打开串口（设备复位后 COM 号可能变化，再次探测）
        port2 = args.port or auto_detect_port()
        if not port2:
            print("✗ 复位后未找到串口")
            return 1
        if port2 != port:
            print(f"      注意：复位后串口变为 {port2}")
        ser = serial.Serial(port2, args.baud, timeout=0.1)

        # ===== 第三步：Ymodem 传输 =====
        if not do_ymodem_tx(ser, firmware):
            print("\n✗ Ymodem 传输失败")
            return 4

        # ===== 第四步：等待 Bootloader 完成 + App 重启 =====
        wait_app_start(ser, timeout=15.0)
        ser.reset_input_buffer()
        time.sleep(1.0)

        # ===== 第五步：查询升级后版本号 =====
        ver_after = query_version(ser, label="─ 查询升级后版本号 ─")
        # 再查一次确认稳定
        time.sleep(0.3)
        ver_after2 = query_version(ser, label="─ 二次确认版本号 ─")

        # 结果汇总
        print()
        print("─" * 62)
        if ver_before:
            print(f"  升级前版本 : {ver_before}")
        print(f"  升级后版本 : {ver_after or '（未知）'}")
        if ver_after and ver_after2 and ver_after == ver_after2:
            print("  两次查询一致：✓")
        if ver_after and ver_before and ver_after != ver_before:
            print("  版本发生变化：✓")
        if ver_after and ver_after == ver_after2:
            print("\n🎉 OTA 升级成功！")
            return 0
        else:
            print("\n⚠ OTA 传输完成，但未能读取到版本号（请手动确认）")
            return 5
    finally:
        try:
            ser.close()
        except Exception:
            pass


if __name__ == "__main__":
    sys.exit(main())
