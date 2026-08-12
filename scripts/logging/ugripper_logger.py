#!/usr/bin/env python3
"""Small dependency-free V3 logger for Ugripper Python processes."""

from __future__ import annotations

import inspect
import re
import threading
import time


ERROR_CODE_RE = re.compile(r"^UGR-[0-9]{2}-[0-9]{2}$")
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


def _append_error_code(line: str, code: str) -> str:
    if not ERROR_CODE_RE.fullmatch(code):
        raise ValueError(f"invalid Ugripper error code: {code}")
    if "error_code=" in line:
        expected = f" error_code={code}"
        if line.count("error_code=") != 1 or not line.endswith(expected):
            raise ValueError("payload contains a conflicting Ugripper error code")
        return line
    return f"{line} error_code={code}"


def ugripper_log(
    level: str,
    message: object,
    *,
    component: str | None = None,
    error_code: str | None = None,
    stacklevel: int = 1,
) -> None:
    short_level = LEVELS.get(level.upper())
    if short_level is None:
        raise ValueError(f"unsupported log level: {level}")
    if short_level in ("E", "F") and error_code is None:
        raise ValueError("ERROR/FATAL logs require an explicit error_code")

    frame = inspect.currentframe()
    for _ in range(stacklevel):
        if frame is not None:
            frame = frame.f_back
    function = frame.f_code.co_name if frame is not None else "unknown"
    line_number = frame.f_lineno if frame is not None else 0
    timestamp = time.time_ns() // 1_000
    seconds, micros = divmod(timestamp, 1_000_000)
    prefix = (
        f"[{seconds}.{micros:06d}][tid {threading.get_native_id()}]"
        f"[{short_level}][{function} {line_number}] "
    )
    label = f"[{component}] " if component else ""
    lines = str(message).splitlines() or [""]
    for raw_line in lines:
        if not raw_line:
            continue
        payload = f"{label}{raw_line}"
        if error_code is not None:
            payload = _append_error_code(payload, error_code)
        print(prefix + payload, flush=True)
