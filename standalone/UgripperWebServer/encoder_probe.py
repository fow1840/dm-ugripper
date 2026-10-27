"""Bounded encoder position reads, using the recorder's Modbus read request only."""
import math
import os
from pathlib import Path
import select
import termios
import time

from common import operation_lock

REQUEST = bytes.fromhex('01 03 00 41 00 01 d4 1e')


def crc16(data):
    value = 0xffff
    for byte in data:
        value ^= byte
        for _ in range(8):
            value = (value >> 1) ^ (0xa001 if value & 1 else 0)
    return value


def read_position(fd, timeout=0.15):
    termios.tcflush(fd, termios.TCIFLUSH)
    if os.write(fd, REQUEST) != len(REQUEST):
        raise OSError('short encoder request')
    deadline = time.monotonic() + timeout
    buffer = bytearray()
    while time.monotonic() < deadline:
        readable, _, _ = select.select([fd], [], [], max(0, deadline - time.monotonic()))
        if not readable:
            break
        chunk = os.read(fd, 256)
        if not chunk:
            raise OSError('encoder disconnected')
        buffer.extend(chunk)
        while len(buffer) >= 7:
            frame = buffer[:7]
            if frame[:3] == b'\x01\x03\x02' and crc16(frame[:5]) == int.from_bytes(frame[5:], 'little'):
                return int.from_bytes(frame[3:5], 'big')
            del buffer[0]
    return None


def position_sample(position):
    """Keep the raw register count, but match the recorder's signed angle."""
    signed_position = position - 65536 if position >= 32768 else position
    return dict(status='ok', position=position,
                angle_deg=signed_position * 360 / 65536,
                angle_rad=signed_position * math.tau / 65536,
                read_at=time.time())


def read_encoder(path):
    fd = None
    original = None
    try:
        fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK | os.O_CLOEXEC)
        original = termios.tcgetattr(fd)
        for speed, baud in ((termios.B1000000, 1000000), (termios.B115200, 115200)):
            attrs = termios.tcgetattr(fd)
            attrs[:6] = [0, 0, termios.CLOCAL | termios.CREAD | termios.CS8, 0, speed, speed]
            attrs[6][termios.VMIN] = 0
            attrs[6][termios.VTIME] = 0
            termios.tcsetattr(fd, termios.TCSANOW, attrs)
            for _ in range(2):
                position = read_position(fd)
                if position is not None:
                    return dict(position_sample(position), baudrate=baud)
        return dict(status='unavailable', message='未收到有效读数')
    except (OSError, termios.error):
        return dict(status='unavailable', message='编码器未连接或串口不可用')
    finally:
        if fd is not None:
            try:
                if original is not None:
                    termios.tcsetattr(fd, termios.TCSANOW, original)
            except (OSError, termios.error):
                pass
            finally:
                os.close(fd)


def read_encoders():
    # Keep the recording/maintenance lock until BOTH serial descriptors close.
    with operation_lock():
        return {side: read_encoder(Path('/dev') / (side + '_encoder')) for side in ('left', 'right')}


def stream_encoders(duration=60, sides=('left','right'), acquire_lock=True):
    """100 Hz target, persistent descriptors, bounded read and connection lifetime."""
    import contextlib
    with (operation_lock() if acquire_lock else contextlib.nullcontext()), contextlib.ExitStack() as stack:
        ports = {}
        for side in sides:
            fd = os.open('/dev/' + side + '_encoder', os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK | os.O_CLOEXEC)
            stack.callback(os.close, fd)
            original = termios.tcgetattr(fd)
            def restore(fd=fd, original=original):
                try: termios.tcsetattr(fd, termios.TCSANOW, original)
                except (OSError, termios.error): pass
            stack.callback(restore)
            for speed in (termios.B1000000, termios.B115200):
                attrs = termios.tcgetattr(fd)
                attrs[:6] = [0, 0, termios.CLOCAL | termios.CREAD | termios.CS8, 0, speed, speed]
                attrs[6][termios.VMIN] = attrs[6][termios.VTIME] = 0
                termios.tcsetattr(fd, termios.TCSANOW, attrs)
                if read_position(fd) is not None: break
            else: raise ValueError(side + ': encoder unavailable')
            ports[side] = fd
        start = tick = time.monotonic()
        count = 0
        valid = dict.fromkeys(sides, 0)
        missed = dict.fromkeys(sides, 0)
        dropped = dict.fromkeys(sides, 0)
        while time.monotonic() - start < duration:
            values = {}
            for side, fd in ports.items():
                position = read_position(fd, timeout=0.004)
                if position is None:
                    missed[side] += 1; dropped[side] += 1
                    if missed[side] >= 20: raise ValueError(side + ': encoder read timeout (20 consecutive samples)')
                    values[side] = dict(status='unavailable',message='未收到有效读数')
                    continue
                missed[side] = 0; valid[side] += 1
                values[side] = position_sample(position)
            count += 1
            elapsed = time.monotonic()-start
            yield dict(values, rate_hz=round(min(valid.values())/max(elapsed,0.01),1), dropped=dict(dropped), remaining_s=max(0,round(duration-elapsed,1)))
            tick += 0.01
            time.sleep(max(0,tick-time.monotonic()))
            if tick < time.monotonic()-0.01: tick=time.monotonic()
