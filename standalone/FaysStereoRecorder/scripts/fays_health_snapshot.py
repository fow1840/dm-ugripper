#!/usr/bin/env python3
"""Batch health parsing without periodic interpreter startup.

Private snapshots are not episode metadata. Consumers must check snapshot age,
recorder identity and live source existence; never treat a cached bool as health.
"""
import json
import os
import sys
import time


def load(path):
    try:
        with open(path, encoding="utf-8") as stream:
            data = json.load(stream)
        return data if isinstance(data, dict) else {}
    except (OSError, ValueError):
        return {}


def integer(value):
    try:
        number = int(value or 0)
        return number if 0 < number <= 9223372036854775807 else 0
    except (ValueError, TypeError, OverflowError):
        return 0


def deadline(value, stale):
    stamp = integer(value)
    return int(stamp / 1000000 + float(stale) * 1000) if stamp else 0


def run(directory, sources, frame_stale, imu_stale):
    cached = {}
    parent = os.getppid()
    while os.getppid() == parent:
        for side, calibration_path, runtime_path in sources:
            try:
                st = os.stat(calibration_path)
                identity = (st.st_dev, st.st_ino, st.st_size, st.st_mtime_ns, st.st_ctime_ns)
            except OSError:
                identity = None
            previous, calibration = cached.get(calibration_path, (None, {}))
            if identity is None:
                calibration = {}
            elif identity != previous:
                calibration = load(calibration_path)
            cached[calibration_path] = identity, calibration
            info = calibration.get("device_info", {})
            valid = calibration.get("valid") is True and isinstance(info, dict) and bool(info.get("serial_number"))
            runtime = load(runtime_path)
            # /proc/uptime uses BOOTTIME with 10ms precision on the shell side.
            fields = (time.clock_gettime_ns(time.CLOCK_BOOTTIME) // 10000000 * 10, int(valid),
                      integer(runtime.get("pid")),
                      deadline(runtime.get("last_frame_system_ns"), frame_stale),
                      deadline(runtime.get("last_imu_system_ns"), imu_stale))
            target = os.path.join(directory, side)
            with open(target + ".tmp", "w", encoding="ascii") as stream:
                stream.write(" ".join(map(str, fields)) + "\n")
            os.replace(target + ".tmp", target)
        time.sleep(0.1)


if __name__ == "__main__":
    run(sys.argv[1], [("left", sys.argv[2], sys.argv[3]),
                      ("right", sys.argv[4], sys.argv[5])], sys.argv[6], sys.argv[7])
