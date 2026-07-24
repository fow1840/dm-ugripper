#!/usr/bin/env python3
"""Live two-column dashboard for left/right gripper HMI data."""

import argparse
import collections
import fcntl
import json
import math
import os
import select
import shutil
import signal
import statistics
import subprocess
import sys
import termios
import time
from dataclasses import dataclass, field
from datetime import datetime, timezone
from typing import Deque, List, Optional


FRAME_HEADER = b"\xA5\xA5"
FRAME_LENGTH = 8
PRESSURE_QUERY = bytes.fromhex("5A 5A 30 02 00 00 32")
KEY_LABELS = ("UP", "DOWN", "POGO3", "POGO4", "KEY5", "KEY6")
POGO_KEY_INDEX = {"left": 3, "right": 2}
STANDARD_GRAVITY = 9.80665
DEFAULT_CALIBRATION_FILE = "/var/lib/ugripper/hmi_pressure_calibration.json"
DEFAULT_HMI_TOOL = "/opt/ugripper/bin/GripperHmiTool/GripperHmiTool"
CALIBRATION_VERSION = 1
MIN_CALIBRATION_SAMPLES = 20
MIN_CALIBRATION_DELTA_RAW = 10.0
ANSI_RESET = "\033[0m"
ANSI_TITLE = "\033[1;7m"
ANSI_HEADER = "\033[1;36m"
ANSI_DIM = "\033[2m"
ANSI_PRESSURE = "\033[1;33m"
ANSI_LUX = "\033[1;36m"
ANSI_KEYS = "\033[1;32m"
ANSI_POGO = "\033[1;35m"
ANSI_OK = "\033[1;32m"
ANSI_WARN = "\033[1;33m"
ANSI_ERROR = "\033[1;31m"


def xor_checksum(payload: bytes) -> int:
    value = 0
    for byte in payload:
        value ^= byte
    return value


def format_age(now: float, timestamp: Optional[float]) -> str:
    if timestamp is None:
        return "none"
    age_ms = max(0.0, (now - timestamp) * 1000.0)
    if age_ms < 10000.0:
        return f"{age_ms:7.1f} ms"
    return f"{age_ms / 1000.0:7.1f} s"


def event_rate(timestamps: Deque[float], now: float, window_sec: float = 2.0) -> float:
    cutoff = now - window_sec
    while timestamps and timestamps[0] < cutoff:
        timestamps.popleft()
    if len(timestamps) < 2:
        return 0.0
    span = timestamps[-1] - timestamps[0]
    return 0.0 if span <= 0.0 else (len(timestamps) - 1) / span


def configure_port(path: str, baudrate: int) -> int:
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
    termios.tcflush(fd, termios.TCIFLUSH)
    if hasattr(termios, "TIOCEXCL"):
        fcntl.ioctl(fd, termios.TIOCEXCL)
    return fd


@dataclass
class PortState:
    side: str
    path: str
    fd: int = -1
    resolved_path: str = ""
    buffer: bytearray = field(default_factory=bytearray)
    opened_at: float = field(default_factory=time.monotonic)
    last_rx_at: Optional[float] = None
    total_frames: int = 0
    bad_xor: int = 0
    pressure_sent: int = 0
    pressure_value: Optional[int] = None
    pressure_raw: Optional[int] = None
    pressure_count: int = 0
    pressure_at: Optional[float] = None
    pressure_times: Deque[float] = field(default_factory=collections.deque)
    pressure_samples: Deque[tuple[float, int]] = field(
        default_factory=lambda: collections.deque(maxlen=20000)
    )
    lux_value: Optional[int] = None
    lux_count: int = 0
    lux_at: Optional[float] = None
    lux_times: Deque[float] = field(default_factory=collections.deque)
    keys: List[Optional[bool]] = field(default_factory=lambda: [None] * 6)
    key_count: int = 0
    key_at: Optional[float] = None
    key_times: Deque[float] = field(default_factory=collections.deque)
    last_key_text: str = "none"

    def open(self, baudrate: int) -> None:
        self.resolved_path = os.path.realpath(self.path)
        self.fd = configure_port(self.path, baudrate)
        self.opened_at = time.monotonic()

    def close(self) -> None:
        if self.fd >= 0:
            os.close(self.fd)
            self.fd = -1

    def read_available(self, now: float) -> None:
        try:
            chunk = os.read(self.fd, 65536)
        except BlockingIOError:
            return
        if not chunk:
            return
        self.buffer.extend(chunk)
        self.consume_frames(now)

    def consume_frames(self, now: float) -> None:
        while True:
            header = self.buffer.find(FRAME_HEADER)
            if header < 0:
                if len(self.buffer) > 1:
                    del self.buffer[:-1]
                return
            if header:
                del self.buffer[:header]
            if len(self.buffer) < FRAME_LENGTH:
                return

            frame = bytes(self.buffer[:FRAME_LENGTH])
            if xor_checksum(frame[:7]) != frame[7]:
                self.bad_xor += 1
                del self.buffer[0]
                continue

            del self.buffer[:FRAME_LENGTH]
            self.total_frames += 1
            self.last_rx_at = now
            index = frame[2]
            if index == 0x30:
                self.pressure_raw = int.from_bytes(frame[3:7], "big", signed=False)
                self.pressure_value = int.from_bytes(frame[3:7], "big", signed=True)
                self.pressure_count += 1
                self.pressure_at = now
                self.pressure_times.append(now)
                self.pressure_samples.append((now, self.pressure_value))
            elif index == 0x31:
                self.lux_value = int.from_bytes(frame[5:7], "big", signed=False)
                self.lux_count += 1
                self.lux_at = now
                self.lux_times.append(now)
            elif index == 0x01:
                raw_code = frame[6]
                key_index: Optional[int] = None
                pressed: Optional[bool] = None
                if 0x01 <= raw_code <= 0x06:
                    key_index = raw_code - 0x01
                    pressed = True
                elif 0x0A <= raw_code <= 0x0F:
                    key_index = raw_code - 0x0A
                    pressed = False
                if key_index is not None and pressed is not None:
                    self.keys[key_index] = pressed
                    self.key_count += 1
                    self.key_at = now
                    self.key_times.append(now)
                    action = "pressed" if pressed else "released"
                    self.last_key_text = f"{KEY_LABELS[key_index]} {action} raw=0x{raw_code:02X}"

    def send_pressure_query(self) -> None:
        os.write(self.fd, PRESSURE_QUERY)
        self.pressure_sent += 1


@dataclass
class PressureCalibration:
    zero_raw: Optional[float] = None
    scale_kg_per_raw: Optional[float] = None
    reference_raw: Optional[float] = None
    reference_kg: Optional[float] = None
    updated_at: str = ""

    def is_calibrated(self) -> bool:
        return (
            self.zero_raw is not None
            and self.scale_kg_per_raw is not None
            and math.isfinite(self.zero_raw)
            and math.isfinite(self.scale_kg_per_raw)
            and self.scale_kg_per_raw != 0.0
        )

    def load_kg(self, raw_value: int) -> Optional[float]:
        if not self.is_calibrated():
            return None
        return (float(raw_value) - float(self.zero_raw)) * float(self.scale_kg_per_raw)

    def to_dict(self) -> dict:
        return {
            "zero_raw": self.zero_raw,
            "scale_kg_per_raw": self.scale_kg_per_raw,
            "reference_raw": self.reference_raw,
            "reference_kg": self.reference_kg,
            "updated_at": self.updated_at,
        }

    @classmethod
    def from_dict(cls, value: object) -> "PressureCalibration":
        if not isinstance(value, dict):
            return cls()
        calibration = cls(
            zero_raw=value.get("zero_raw"),
            scale_kg_per_raw=value.get("scale_kg_per_raw"),
            reference_raw=value.get("reference_raw"),
            reference_kg=value.get("reference_kg"),
            updated_at=str(value.get("updated_at", "")),
        )
        for field_name in ("zero_raw", "scale_kg_per_raw", "reference_raw", "reference_kg"):
            field_value = getattr(calibration, field_name)
            if field_value is not None:
                try:
                    setattr(calibration, field_name, float(field_value))
                except (TypeError, ValueError):
                    setattr(calibration, field_name, None)
        return calibration


def service_is_active() -> bool:
    result = subprocess.run(
        ["systemctl", "is-active", "--quiet", "ugripper.service"],
        check=False,
    )
    return result.returncode == 0


def set_service_state(action: str) -> None:
    subprocess.run(["systemctl", action, "ugripper.service"], check=True)


def wait_for_service(active: bool, timeout_sec: float = 10.0) -> bool:
    deadline = time.monotonic() + timeout_sec
    while time.monotonic() < deadline:
        if service_is_active() == active:
            return True
        time.sleep(0.1)
    return False


def load_calibrations(path: str) -> dict[str, PressureCalibration]:
    calibrations = {
        "left": PressureCalibration(),
        "right": PressureCalibration(),
    }
    try:
        with open(path, "r", encoding="utf-8") as handle:
            payload = json.load(handle)
    except FileNotFoundError:
        return calibrations
    except (OSError, json.JSONDecodeError) as error:
        print(f"warning: cannot read calibration file {path}: {error}", file=sys.stderr)
        return calibrations

    if not isinstance(payload, dict) or payload.get("version") != CALIBRATION_VERSION:
        print(f"warning: unsupported calibration file {path}", file=sys.stderr)
        return calibrations
    for side in calibrations:
        calibrations[side] = PressureCalibration.from_dict(payload.get(side))
    return calibrations


def save_calibrations(path: str, calibrations: dict[str, PressureCalibration]) -> None:
    parent = os.path.dirname(path) or "."
    os.makedirs(parent, mode=0o755, exist_ok=True)
    temporary_path = f"{path}.tmp.{os.getpid()}"
    payload = {
        "version": CALIBRATION_VERSION,
        "left": calibrations["left"].to_dict(),
        "right": calibrations["right"].to_dict(),
    }
    try:
        with open(temporary_path, "w", encoding="utf-8") as handle:
            json.dump(payload, handle, indent=2, sort_keys=True)
            handle.write("\n")
            handle.flush()
            os.fsync(handle.fileno())
        os.chmod(temporary_path, 0o644)
        os.replace(temporary_path, path)
    finally:
        try:
            os.unlink(temporary_path)
        except FileNotFoundError:
            pass


def resolve_hmi_tool(explicit_path: str) -> Optional[str]:
    candidates = [
        explicit_path,
        os.environ.get("GRIPPER_HMI_TOOL", ""),
        DEFAULT_HMI_TOOL,
        shutil.which("GripperHmiTool") or "",
    ]
    for candidate in candidates:
        if candidate and os.path.isfile(candidate) and os.access(candidate, os.X_OK):
            return candidate
    return None


def parse_key_value_output(output: str) -> dict[str, str]:
    values: dict[str, str] = {}
    for line in output.splitlines():
        key, separator, value = line.strip().partition("=")
        if separator and key:
            values[key] = value
    return values


def read_force_calibration_from_hmi(tool: str, port: str) -> Optional[PressureCalibration]:
    result = subprocess.run(
        [tool, "--port", port, "--read-sn", "--read-calib"],
        check=False,
        capture_output=True,
        text=True,
        timeout=20,
    )
    if result.returncode != 0:
        return None
    values = parse_key_value_output(result.stdout)
    if values.get("force.valid") != "yes" or values.get("force.sn_match") != "yes":
        return None
    try:
        zero_raw = float(values["force.zero_raw"])
        reference_raw = float(values["force.reference_raw"])
        reference_mass_mg = float(values["force.reference_mass_mg"])
        calibrated_at = int(values.get("force.calibrated_at_unix_s", "0"))
    except (KeyError, TypeError, ValueError):
        return None
    delta_raw = reference_raw - zero_raw
    if delta_raw == 0.0 or reference_mass_mg <= 0.0:
        return None
    reference_kg = reference_mass_mg / 1_000_000.0
    updated_at = (
        datetime.fromtimestamp(calibrated_at, timezone.utc).isoformat()
        if calibrated_at > 0
        else ""
    )
    return PressureCalibration(
        zero_raw=zero_raw,
        scale_kg_per_raw=reference_kg / delta_raw,
        reference_raw=reference_raw,
        reference_kg=reference_kg,
        updated_at=updated_at,
    )


def write_force_calibrations_to_hmi(
    tool: str,
    states: List[PortState],
    calibrations: dict[str, PressureCalibration],
    target_sides: List[str],
) -> None:
    state_by_side = {state.side: state for state in states}
    for side in target_sides:
        calibration = calibrations[side]
        if calibration.zero_raw is None or calibration.reference_raw is None or calibration.reference_kg is None:
            raise RuntimeError(f"{side}: incomplete force calibration")
        reference_mass_mg = int(round(calibration.reference_kg * 1_000_000.0))
        command = [
            tool,
            "--port",
            state_by_side[side].path,
            "--write-force-calib",
            "--zero-raw",
            str(int(round(calibration.zero_raw))),
            "--reference-raw",
            str(int(round(calibration.reference_raw))),
            "--reference-mass-mg",
            str(reference_mass_mg),
        ]
        result = subprocess.run(command, check=False, capture_output=True, text=True, timeout=45)
        if result.returncode != 0:
            detail = (result.stderr or result.stdout).strip().replace("\n", " ")
            raise RuntimeError(f"{side}: HMI force calibration write failed: {detail}")
        print(f"{side}: force calibration written to HMI and verified")


def pogo_value(state: PortState) -> Optional[bool]:
    return state.keys[POGO_KEY_INDEX[state.side]]


def pogo_text(state: PortState) -> tuple[str, str]:
    key_index = POGO_KEY_INDEX[state.side]
    value = state.keys[key_index]
    key_name = f"K{key_index + 1}"
    if value is True:
        return f"CLOSED  ({key_name}=1)", ANSI_OK
    if value is False:
        return f"OPEN    ({key_name}=0)", ANSI_ERROR
    return f"UNKNOWN ({key_name}=?)", ANSI_WARN


def relative_pose_status(states: List[PortState]) -> tuple[str, str]:
    state_by_side = {state.side: state for state in states}
    left = pogo_value(state_by_side["left"])
    right = pogo_value(state_by_side["right"])
    if left is True and right is True:
        return "RELATIVE POSE: FIXED", ANSI_OK
    if left is False or right is False:
        return "RELATIVE POSE: NOT FIXED", ANSI_ERROR
    return "RELATIVE POSE: UNKNOWN", ANSI_WARN


def pump_serial_io(
    states: List[PortState],
    next_query: dict[int, float],
    query_interval: float,
    timeout_sec: float = 0.002,
) -> None:
    now = time.monotonic()
    for state in states:
        if now >= next_query[state.fd]:
            try:
                state.send_pressure_query()
            except BlockingIOError:
                pass
            next_query[state.fd] += query_interval
            if now - next_query[state.fd] > query_interval * 5.0:
                next_query[state.fd] = now + query_interval

    ready, _, _ = select.select([state.fd for state in states], [], [], timeout_sec)
    read_at = time.monotonic()
    for fd in ready:
        for state in states:
            if state.fd == fd:
                state.read_available(read_at)
                break


def collect_pressure_samples(
    states: List[PortState],
    target_sides: List[str],
    next_query: dict[int, float],
    query_interval: float,
    settle_seconds: float,
    sample_seconds: float,
    stop_requested,
) -> dict[str, List[int]]:
    settle_deadline = time.monotonic() + settle_seconds
    while time.monotonic() < settle_deadline:
        if stop_requested():
            raise KeyboardInterrupt
        pump_serial_io(states, next_query, query_interval)

    sample_start = time.monotonic()
    sample_deadline = sample_start + sample_seconds
    while time.monotonic() < sample_deadline:
        if stop_requested():
            raise KeyboardInterrupt
        pump_serial_io(states, next_query, query_interval)

    samples: dict[str, List[int]] = {}
    for state in states:
        if state.side not in target_sides:
            continue
        samples[state.side] = [
            value for timestamp, value in state.pressure_samples if timestamp >= sample_start
        ]
    return samples


def selected_sides(value: str) -> List[str]:
    return ["left", "right"] if value == "both" else [value]


def summarize_samples(side: str, values: List[int]) -> tuple[float, float, int]:
    if len(values) < MIN_CALIBRATION_SAMPLES:
        raise RuntimeError(
            f"{side}: only {len(values)} pressure samples received; "
            f"need at least {MIN_CALIBRATION_SAMPLES}"
        )
    median_raw = float(statistics.median(values))
    standard_deviation = float(statistics.pstdev(values))
    span = max(values) - min(values)
    print(
        f"{side}: samples={len(values)} median_raw={median_raw:.3f} "
        f"stddev_raw={standard_deviation:.3f} span_raw={span}"
    )
    return median_raw, standard_deviation, span


def run_calibration(
    args: argparse.Namespace,
    states: List[PortState],
    calibrations: dict[str, PressureCalibration],
    next_query: dict[int, float],
    query_interval: float,
    stop_requested,
) -> None:
    operation = "zero" if args.calibrate_zero else "load"
    selection = args.calibrate_zero or args.calibrate_load
    target_sides = selected_sides(selection)
    if operation == "zero":
        instruction = f"Remove all load from {selection} gripper sensor(s)."
    else:
        instruction = (
            f"Apply {args.known_kg:g} kg standard load to {selection} gripper sensor(s)."
        )
    print(instruction)
    if sys.stdin.isatty():
        input("Press Enter when the load is stable... ")
    elif args.calibration_settle_seconds > 0.0:
        print(f"Starting after {args.calibration_settle_seconds:g}s settle delay...")

    samples = collect_pressure_samples(
        states,
        target_sides,
        next_query,
        query_interval,
        args.calibration_settle_seconds,
        args.calibration_seconds,
        stop_requested,
    )
    captured_at = datetime.now(timezone.utc).isoformat()
    sample_medians = {
        side: summarize_samples(side, samples.get(side, []))[0]
        for side in target_sides
    }

    if operation == "zero":
        for side in target_sides:
            calibration = calibrations[side]
            calibration.zero_raw = sample_medians[side]
            calibration.updated_at = captured_at
            print(
                f"{side}: zero_raw={calibration.zero_raw:.3f}; "
                f"existing scale={'kept' if calibration.scale_kg_per_raw is not None else 'not set'}"
            )
    else:
        for side in target_sides:
            calibration = calibrations[side]
            if calibration.zero_raw is None:
                raise RuntimeError(
                    f"{side}: zero point is missing; run --calibrate-zero {side} first"
                )
            reference_raw = sample_medians[side]
            delta_raw = reference_raw - calibration.zero_raw
            if abs(delta_raw) < MIN_CALIBRATION_DELTA_RAW:
                raise RuntimeError(
                    f"{side}: reference delta {delta_raw:.3f} raw is too small; "
                    "check the standard load and zero point"
                )
            calibration.reference_raw = reference_raw
            calibration.reference_kg = args.known_kg
            calibration.scale_kg_per_raw = args.known_kg / delta_raw
            calibration.updated_at = captured_at
            print(
                f"{side}: reference_raw={reference_raw:.3f} delta_raw={delta_raw:.3f} "
                f"scale_kg_per_raw={calibration.scale_kg_per_raw:.12g}"
            )

    if operation == "zero":
        save_calibrations(args.calibration_file, calibrations)
        print(f"zero point saved locally until the load point is written: {args.calibration_file}")


def run_selftest() -> int:
    calibration = PressureCalibration(zero_raw=100.0, scale_kg_per_raw=0.001)
    load_kg = calibration.load_kg(1100)
    if load_kg is None or abs(load_kg - 1.0) > 1e-9:
        raise RuntimeError("calibration selftest failed")
    if abs(load_kg * STANDARD_GRAVITY - 9.80665) > 1e-9:
        raise RuntimeError("force conversion selftest failed")

    left = PortState("left", "/dev/left_gripper")
    right = PortState("right", "/dev/right_gripper")
    left.keys[3] = True
    right.keys[2] = True
    if relative_pose_status([left, right])[0] != "RELATIVE POSE: FIXED":
        raise RuntimeError("pogo fixed-state selftest failed")
    right.keys[2] = False
    if relative_pose_status([left, right])[0] != "RELATIVE POSE: NOT FIXED":
        raise RuntimeError("pogo open-state selftest failed")
    print("selftest: OK")
    return 0


def column_lines(
    state: PortState,
    now: float,
    calibration: PressureCalibration,
) -> List[tuple[str, str]]:
    force_text = "NO DATA"
    raw_text = "RAW NO DATA"
    if state.pressure_value is not None and state.pressure_raw is not None:
        load_kg = calibration.load_kg(state.pressure_value)
        if load_kg is None:
            force_text = "UNCALIBRATED"
        else:
            force_text = f"{load_kg * STANDARD_GRAVITY:9.3f} N    LOAD {load_kg:8.4f} kgf"
        raw_text = f"RAW {state.pressure_value}  hex=0x{state.pressure_raw:08X}"
        if calibration.zero_raw is not None:
            raw_text += f"  zero={calibration.zero_raw:.1f}"
    lux_text = "NO DATA" if state.lux_value is None else f"{state.lux_value} lx"
    key_text = " ".join(
        f"K{index + 1}={'?' if value is None else int(value)}"
        for index, value in enumerate(state.keys)
    )
    current_pogo_text, current_pogo_style = pogo_text(state)
    return [
        (f"{state.side.upper()} GRIPPER", ANSI_HEADER),
        (f"{state.path} -> {state.resolved_path}", ANSI_DIM),
        (f"RX {format_age(now, state.last_rx_at)}  frames={state.total_frames}  bad_xor={state.bad_xor}", ANSI_DIM),
        ("", ""),
        (f"FORCE      {force_text}", ANSI_PRESSURE),
        (raw_text, ANSI_DIM),
        ("", ""),
        (f"LUX        {lux_text}", ANSI_LUX),
        ("", ""),
        (f"KEYS       {key_text}", ANSI_KEYS),
        (f"POGO       {current_pogo_text}", current_pogo_style),
        (f"LAST KEY   {state.last_key_text}", ANSI_KEYS),
    ]


def render_dashboard(
    states: List[PortState],
    calibrations: dict[str, PressureCalibration],
    now: float,
    pressure_hz: float,
    refresh_hz: float,
) -> None:
    terminal_width = shutil.get_terminal_size((132, 30)).columns
    gap = "  |  "
    column_width = max(30, (terminal_width - len(gap)) // 2)
    left_lines = column_lines(states[0], now, calibrations[states[0].side])
    right_lines = column_lines(states[1], now, calibrations[states[1].side])
    pose_text, pose_style = relative_pose_status(states)
    title = (
        f"UGripper HMI live dashboard  pressure={pressure_hz:g} Hz/side  "
        f"lux=passive  refresh={refresh_hz:g} Hz"
    )
    lines = [
        f"{ANSI_TITLE}{title[:terminal_width].ljust(terminal_width)}{ANSI_RESET}",
        f"{pose_style}{pose_text.center(terminal_width)}{ANSI_RESET}",
        "Ctrl+C to stop; original ugripper.service state will be restored",
        "",
    ]
    for (left, left_style), (right, right_style) in zip(left_lines, right_lines):
        left_cell = left[:column_width].ljust(column_width)
        right_cell = right[:column_width].ljust(column_width)
        lines.append(
            f"{left_style}{left_cell}{ANSI_RESET}{gap}"
            f"{right_style}{right_cell}{ANSI_RESET}"
        )
    output = "\033[H" + "\n".join(lines) + "\033[J"
    sys.stdout.write(output)
    sys.stdout.flush()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Display left/right HMI pressure, passive lux, and key states in two columns"
    )
    parser.add_argument("--left-port", default="/dev/left_gripper")
    parser.add_argument("--right-port", default="/dev/right_gripper")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--pressure-hz", type=float, default=100.0)
    parser.add_argument("--refresh-hz", type=float, default=10.0)
    parser.add_argument("--duration", type=float, default=0.0, help="0 means run until Ctrl+C")
    parser.add_argument("--selftest", action="store_true", help="run offline calibration and Pogo tests")
    calibration_group = parser.add_mutually_exclusive_group()
    calibration_group.add_argument(
        "--calibrate-zero",
        choices=("left", "right", "both"),
        help="capture unloaded zero point for one or both grippers",
    )
    calibration_group.add_argument(
        "--calibrate-load",
        choices=("left", "right", "both"),
        help="capture a known standard load after zero calibration",
    )
    parser.add_argument(
        "--known-kg",
        type=float,
        default=1.0,
        help="standard load in kg for --calibrate-load",
    )
    parser.add_argument(
        "--calibration-file",
        default=DEFAULT_CALIBRATION_FILE,
        help="local zero-point staging and legacy fallback JSON path",
    )
    parser.add_argument(
        "--hmi-tool",
        default="",
        help="GripperHmiTool path used to read/write the force calibration payload",
    )
    parser.add_argument(
        "--calibration-seconds",
        type=float,
        default=2.0,
        help="pressure sample window for a calibration point",
    )
    parser.add_argument(
        "--calibration-settle-seconds",
        type=float,
        default=0.5,
        help="settle delay after operator confirmation",
    )
    parser.add_argument(
        "--no-manage-service",
        action="store_true",
        help="do not stop or restore ugripper.service",
    )
    parser.add_argument(
        "--leave-service-stopped",
        action="store_true",
        help="do not restore ugripper.service after monitoring",
    )
    args = parser.parse_args()
    if args.pressure_hz <= 0.0 or args.pressure_hz > 200.0:
        parser.error("--pressure-hz must be in (0, 200]")
    if args.refresh_hz <= 0.0 or args.refresh_hz > 30.0:
        parser.error("--refresh-hz must be in (0, 30]")
    if args.duration < 0.0:
        parser.error("--duration must be >= 0")
    if args.known_kg <= 0.0:
        parser.error("--known-kg must be > 0")
    if args.calibration_seconds <= 0.0:
        parser.error("--calibration-seconds must be > 0")
    if args.calibration_settle_seconds < 0.0:
        parser.error("--calibration-settle-seconds must be >= 0")
    return args


def main() -> int:
    args = parse_args()
    if args.selftest:
        return run_selftest()

    service_was_active = False
    service_stopped_by_us = False
    calibrations = load_calibrations(args.calibration_file)
    states = [
        PortState("left", args.left_port),
        PortState("right", args.right_port),
    ]

    if not args.no_manage_service:
        service_was_active = service_is_active()
        if service_was_active:
            if os.geteuid() != 0:
                print("ugripper.service is active; run this monitor with sudo", file=sys.stderr)
                return 2
            set_service_state("stop")
            if not wait_for_service(False):
                print("timed out stopping ugripper.service", file=sys.stderr)
                try:
                    set_service_state("start")
                except subprocess.CalledProcessError:
                    pass
                return 2
            service_stopped_by_us = True

    stop_requested = False

    def request_stop(_signum: int, _frame: object) -> None:
        nonlocal stop_requested
        stop_requested = True

    signal.signal(signal.SIGINT, request_stop)
    signal.signal(signal.SIGTERM, request_stop)

    try:
        hmi_tool = resolve_hmi_tool(args.hmi_tool)
        if not (args.calibrate_zero or args.calibrate_load) and hmi_tool is not None:
            for state in states:
                calibration = read_force_calibration_from_hmi(hmi_tool, state.path)
                calibrations[state.side] = calibration or PressureCalibration()

        for state in states:
            state.open(args.baud)

        started_at = time.monotonic()
        next_query = {state.fd: started_at for state in states}
        query_interval = 1.0 / args.pressure_hz
        if args.calibrate_zero or args.calibrate_load:
            run_calibration(
                args,
                states,
                calibrations,
                next_query,
                query_interval,
                lambda: stop_requested,
            )
            if args.calibrate_load:
                if hmi_tool is None:
                    raise RuntimeError(
                        "GripperHmiTool not found; pass --hmi-tool to write force calibration"
                    )
                for state in states:
                    state.close()
                target_sides = selected_sides(args.calibrate_load)
                write_force_calibrations_to_hmi(
                    hmi_tool,
                    states,
                    calibrations,
                    target_sides,
                )
                save_calibrations(args.calibration_file, calibrations)
                print(f"calibration cache updated: {args.calibration_file}")
        else:
            next_render = started_at
            render_interval = 1.0 / args.refresh_hz
            sys.stdout.write("\033[2J\033[H\033[?25l")
            sys.stdout.flush()

            while not stop_requested:
                now = time.monotonic()
                if args.duration > 0.0 and now - started_at >= args.duration:
                    break

                pump_serial_io(states, next_query, query_interval)
                now = time.monotonic()
                if now >= next_render:
                    render_dashboard(
                        states,
                        calibrations,
                        now,
                        args.pressure_hz,
                        args.refresh_hz,
                    )
                    next_render = now + render_interval
    except PermissionError as error:
        print(f"permission denied: {error}; run with sudo", file=sys.stderr)
        return 2
    except RuntimeError as error:
        print(f"calibration failed: {error}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        return 130
    except OSError as error:
        print(f"serial monitor failed: {error}", file=sys.stderr)
        return 2
    finally:
        for state in states:
            state.close()
        if sys.stdout.isatty():
            sys.stdout.write("\033[?25h\n")
            sys.stdout.flush()
        if service_stopped_by_us and not args.leave_service_stopped:
            try:
                set_service_state("start")
                wait_for_service(True)
            except subprocess.CalledProcessError as error:
                print(f"warning: failed to restore ugripper.service: {error}", file=sys.stderr)

    if service_was_active and service_stopped_by_us and not args.leave_service_stopped:
        print("ugripper.service restored")
    elif service_stopped_by_us:
        print("ugripper.service left stopped")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
