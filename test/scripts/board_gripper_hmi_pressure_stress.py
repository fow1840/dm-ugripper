#!/usr/bin/env python3
import argparse
import collections
import os
import select
import statistics
import termios
import time


PRESSURE_QUERY = bytes.fromhex("5a 5a 30 02 00 00 32")


def open_port(path: str, baudrate: int) -> int:
    fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    attributes = termios.tcgetattr(fd)
    attributes[0] = 0
    attributes[1] = 0
    attributes[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
    attributes[3] = 0
    speed = getattr(termios, f"B{baudrate}")
    attributes[4] = speed
    attributes[5] = speed
    attributes[6][termios.VMIN] = 0
    attributes[6][termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW, attributes)
    termios.tcflush(fd, termios.TCIOFLUSH)
    return fd


def consume_frames(state: dict, now: float) -> None:
    buffer = state["buffer"]
    while True:
        header = buffer.find(b"\xA5\xA5")
        if header < 0:
            if len(buffer) > 1:
                del buffer[:-1]
            return
        if header:
            del buffer[:header]
        if len(buffer) < 8:
            return
        frame = bytes(buffer[:8])
        checksum = 0
        for value in frame[:7]:
            checksum ^= value
        if checksum != frame[7]:
            state["bad_xor"] += 1
            del buffer[0]
            continue
        del buffer[:8]
        if frame[2] != 0x30:
            continue
        state["samples"].append((now, int.from_bytes(frame[3:7], "big", signed=True)))


def percentile(values: list[float], ratio: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    return ordered[int(ratio * (len(ordered) - 1))]


def main() -> int:
    parser = argparse.ArgumentParser(description="Stress gripper pressure GET without lux/RGB traffic")
    parser.add_argument("--port", action="append", dest="ports")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--baseline-sec", type=float, default=5.0)
    parser.add_argument("--duration-sec", type=float, default=10.0)
    parser.add_argument("--query-hz", type=float, default=100.0)
    args = parser.parse_args()
    ports = args.ports or ["/dev/right_gripper", "/dev/left_gripper"]
    if args.baseline_sec < 0 or args.duration_sec <= 0 or args.query_hz <= 0:
        parser.error("durations and query rate must be positive")

    descriptors = {open_port(port, args.baud): port for port in ports}
    state = {
        port: {"buffer": bytearray(), "samples": [], "bad_xor": 0, "sent": 0}
        for port in ports
    }
    baseline_start = time.monotonic()
    active_start = baseline_start + args.baseline_sec
    active_end = active_start + args.duration_sec
    next_send = {port: active_start for port in ports}
    baseline_counts = collections.Counter()
    try:
        while time.monotonic() < active_end:
            now = time.monotonic()
            if now >= active_start:
                for fd, port in descriptors.items():
                    while now >= next_send[port] and next_send[port] < active_end:
                        os.write(fd, PRESSURE_QUERY)
                        state[port]["sent"] += 1
                        next_send[port] += 1.0 / args.query_hz
            ready, _, _ = select.select(list(descriptors), [], [], 0.001)
            for fd in ready:
                port = descriptors[fd]
                try:
                    state[port]["buffer"].extend(os.read(fd, 8192))
                except BlockingIOError:
                    continue
                before = len(state[port]["samples"])
                consume_frames(state[port], time.monotonic())
                if now < active_start:
                    baseline_counts[port] += len(state[port]["samples"]) - before
    finally:
        for fd in descriptors:
            os.close(fd)

    for port in ports:
        current = state[port]
        active = [(stamp, value) for stamp, value in current["samples"] if stamp >= active_start]
        gaps = [(right[0] - left[0]) * 1000.0 for left, right in zip(active, active[1:])]
        baseline_hz = baseline_counts[port] / args.baseline_sec if args.baseline_sec else 0.0
        received = len(active)
        sent = current["sent"]
        direct_loss = max(0.0, 1.0 - received / sent) * 100.0
        estimated_replies = max(0.0, received - baseline_hz * args.duration_sec)
        corrected_loss = max(0.0, 1.0 - estimated_replies / sent) * 100.0
        values = [value for _, value in active]
        print(
            f"[{port}] baseline_hz={baseline_hz:.3f} sent={sent} recv={received} "
            f"rx_hz={received / args.duration_sec:.3f} direct_loss_pct={direct_loss:.2f} "
            f"corrected_loss_pct={corrected_loss:.2f} bad_xor={current['bad_xor']}"
        )
        print(
            f"[{port}] gap_ms_p50={(statistics.median(gaps) if gaps else 0):.3f} "
            f"gap_ms_p95={percentile(gaps, 0.95):.3f} "
            f"gap_ms_max={(max(gaps) if gaps else 0):.3f} "
            f"value_min={(min(values) if values else None)} "
            f"value_max={(max(values) if values else None)}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
