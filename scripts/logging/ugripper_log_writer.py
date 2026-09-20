#!/usr/bin/env python3
"""Normalize every inherited stdout/stderr line into the Ugripper V3 main log."""

from __future__ import annotations

import argparse
import os
import re
import sys
import time
from pathlib import Path


ANSI_RE = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")
V3_RE = re.compile(
    r"^\[(?P<ts>[0-9]+\.[0-9]{6})\]\[tid (?P<tid>[0-9]+)\]"
    r"\[(?P<level>[TDIWECF])\]\[(?P<source>[^\]\r\n]+)\] (?P<payload>.*)$"
)
FAYS_RE = re.compile(r"^\[FAYS_TS [^\]]+\]\s*(?P<payload>.*)$")
AUDIO_RE = re.compile(
    r"^\[(?P<component>audio)\]\[(?P<level>TRACE|DEBUG|INFO|WARN|WARNING|ERROR|FATAL|CRITICAL)\]"
    r"\[[^\]]+\]\s*(?P<payload>.*)$",
    re.IGNORECASE,
)
SHELL_RE = re.compile(
    r"^\[(?P<component>[^\]]+)\]\[(?P<level>TRACE|DEBUG|INFO|WARN|WARNING|ERROR|FATAL|CRITICAL)\]"
    r"\[[0-9]{4}-[0-9]{2}-[0-9]{2} [^\]]+\]\s*(?P<payload>.*)$",
    re.IGNORECASE,
)
LEVELS = {
    "TRACE": "T",
    "DEBUG": "D",
    "INFO": "I",
    "WARN": "W",
    "WARNING": "W",
    "ERROR": "E",
    "FATAL": "F",
    "CRITICAL": "F",
}
PYGAME_BANNER = "Hello from the pygame community. https://www.pygame.org/contribute.html"
ERROR_TAIL_RE = re.compile(r" error_code=UGR-[0-9]{2}-[0-9]{2}$")


def _new_outer(level: str, payload: str) -> str:
    timestamp = time.time_ns() // 1_000
    seconds, micros = divmod(timestamp, 1_000_000)
    return (
        f"[{seconds}.{micros:06d}][tid {threading_native_id()}][{level}]"
        f"[main_log_forwarder 0] {payload}"
    )


def threading_native_id() -> int:
    try:
        import threading

        return threading.get_native_id()
    except (AttributeError, ImportError):
        return os.getpid()


def normalize_line(raw_line: str) -> str | None:
    line = ANSI_RE.sub("", raw_line.rstrip("\r\n")).replace("\x00", "")
    if not line or line == PYGAME_BANNER:
        return None

    match = V3_RE.fullmatch(line)
    if match:
        level = match.group("level")
        if level in ("T", "D"):
            return None
        if level == "I" and "[main_50hz_duplicate]" in match.group("payload"):
            return None
        if level in ("C", "F"):
            level = "E"
        return (
            f"[{match.group('ts')}][tid {match.group('tid')}][{level}]"
            f"[{match.group('source')}] {match.group('payload')}"
        )

    match = FAYS_RE.fullmatch(line)
    if match:
        return _new_outer("I", match.group("payload"))

    match = AUDIO_RE.fullmatch(line) or SHELL_RE.fullmatch(line)
    if match:
        level = LEVELS[match.group("level").upper()]
        if level in ("T", "D"):
            return None
        if level == "F":
            level = "E"
        payload = f"[{match.group('component')}] {match.group('payload')}"
        return _new_outer(level, payload)

    if ERROR_TAIL_RE.search(line):
        return _new_outer("E", line)
    if (" WARNING:" in line or line.lower().startswith("warning:") or
            (line.startswith("[") and "rkmpp" in line and "warning" in line.lower())):
        return _new_outer("W", line)
    return _new_outer("I", line)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--no-console", action="store_true")
    return parser.parse_args()


def normalize_existing(path: Path) -> None:
    if not path.is_file() or path.stat().st_size == 0:
        return
    original = path.read_text(encoding="utf-8", errors="replace").splitlines()
    normalized = [line for raw in original if (line := normalize_line(raw)) is not None]
    if any(
        (match := V3_RE.fullmatch(line)) is not None
        and match.group("level") in ("E", "F")
        and ERROR_TAIL_RE.search(match.group("payload")) is None
        for line in normalized
    ):
        archive = path.with_suffix(path.suffix + ".pre-v3")
        index = 1
        while archive.exists():
            archive = path.with_suffix(path.suffix + f".pre-v3.{index}")
            index += 1
        path.replace(archive)
        return
    if normalized == original:
        return
    temporary = path.with_suffix(path.suffix + ".v3.tmp")
    with temporary.open("w", encoding="utf-8") as output:
        for line in normalized:
            output.write(line + "\n")
        output.flush()
        os.fsync(output.fileno())
    temporary.replace(path)


def main() -> int:
    args = parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    normalize_existing(args.output)
    with args.output.open("a", encoding="utf-8", buffering=1) as output:
        for raw_bytes in sys.stdin.buffer:
            line = raw_bytes.decode("utf-8", errors="replace")
            normalized = normalize_line(line)
            if normalized is None:
                continue
            output.write(normalized + "\n")
            if not args.no_console:
                print(normalized, flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
