from __future__ import annotations

import inspect
import re
import threading
import time


_LEVELS = {"TRACE": "T", "DEBUG": "D", "INFO": "I", "WARN": "W", "ERROR": "E", "FATAL": "F"}
_ERROR_CODE_RE = re.compile(r"^UGR-[0-9]{2}-[0-9]{2}$")


def audio_log(level: str, message: str, error_code: str | None = None) -> None:
    short_level = _LEVELS[level.upper()]
    if short_level in ("E", "F") and not _ERROR_CODE_RE.fullmatch(error_code or ""):
        raise ValueError("audio ERROR/FATAL logs require an explicit UGR error code")
    caller = inspect.currentframe().f_back
    timestamp = time.time_ns() // 1_000
    seconds, micros = divmod(timestamp, 1_000_000)
    prefix = (
        f"[{seconds}.{micros:06d}][tid {threading.get_native_id()}][{short_level}]"
        f"[{caller.f_code.co_name} {caller.f_lineno}] "
    )
    for line in str(message).splitlines():
        if not line:
            continue
        payload = f"[audio] {line}"
        if error_code and "error_code=UGR-" not in payload:
            payload += f" error_code={error_code}"
        print(prefix + payload, flush=True)


def read_env_file_value(key: str, env_file: str = "/etc/environment") -> str | None:
    try:
        with open(env_file, "r", encoding="utf-8") as file:
            for raw_line in file:
                line = raw_line.strip()
                if not line or line.startswith("#") or "=" not in line:
                    continue
                current_key, value = line.split("=", 1)
                if current_key.strip() != key:
                    continue
                return value.strip().strip('"').strip("'")
    except Exception as exc:
        audio_log("WARN", f"failed to read {env_file}: {exc}")
    return None
