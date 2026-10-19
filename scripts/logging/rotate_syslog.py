#!/usr/bin/python3
"""rsyslog outchannel hook: RAM-only rename rotation, never truncate the writer.

rsyslog closes its outchannel before invoking this hook and reopens afterwards.
"""
import fcntl
import os
from pathlib import Path
import time
import uuid

ROOT = Path('/tmp/logbuf')
LIMIT = 50 * 1024 * 1024
CHUNK = 5 * 1024 * 1024


def rotate(root=ROOT, limit=LIMIT, chunk=CHUNK):
    with (root / '.rotate.lock').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        active = root / 'syslog.log'
        if active.exists():
            active.rename(root / f'syslog.{time.monotonic_ns():020d}.{uuid.uuid4().hex}.log')
        files = sorted(root.glob('syslog.*.log'), key=lambda p: p.name)
        total = sum(p.stat().st_size for p in files)
        counter = root / 'dropped_bytes'
        dropped = int(counter.read_text()) if counter.exists() else 0
        # Reserve one chunk + maximum rsyslog line for the next active file.
        while files and total > limit - chunk - 65536:
            oldest = files.pop(0)
            size = oldest.stat().st_size
            oldest.unlink()
            total -= size
            dropped += size
        temp = root / '.dropped-next'
        temp.write_text(str(dropped))
        os.replace(temp, counter)


if __name__ == '__main__':
    rotate()
