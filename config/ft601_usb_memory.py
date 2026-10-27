#!/usr/bin/env python3
"""Raise usbfs quota only for FT601; preserve preexisting or externally changed values."""
import fcntl
import json
import os
from pathlib import Path
import sys


def apply(sysroot, state):
    devices = sysroot / 'bus/usb/devices'
    found = False
    for device in devices.iterdir():
        try:
            if (device / 'idVendor').read_text().strip() == '0403' and (device / 'idProduct').read_text().strip() == '601f':
                found = True
                break
        except OSError:
            continue
    if not found:
        return
    quota = sysroot / 'module/usbcore/parameters/usbfs_memory_mb'
    current = int(quota.read_text())
    if current == 0 or current >= 64:  # zero means unlimited
        return
    if state.exists():
        saved = json.loads(state.read_text())
        if current != saved['applied']:
            return  # Administrator changed the quota while our lease was active.
    if not state.exists():
        temporary = state.with_suffix('.tmp')
        temporary.write_text(json.dumps({'previous': current, 'applied': 64}))
        temporary.replace(state)
    quota.write_text('64\n')


def restore(sysroot, state):
    if not state.exists():
        return
    saved = json.loads(state.read_text())
    quota = sysroot / 'module/usbcore/parameters/usbfs_memory_mb'
    if int(quota.read_text()) == saved['applied']:
        quota.write_text(str(saved['previous']) + '\n')
    state.unlink()


def main():
    runtime = Path('/run/ugripper-ft601-memory')
    runtime.mkdir(mode=0o700, exist_ok=True)
    info = runtime.lstat()
    if runtime.is_symlink() or info.st_uid != 0 or info.st_mode & 0o077:
        raise RuntimeError('unsafe state directory')
    with (runtime / 'lock').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        {'apply': apply, 'restore': restore}[sys.argv[1]](Path('/sys'), runtime / 'state.json')


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, RuntimeError, KeyError, IndexError) as exc:
        print('FT601 USB memory: ' + str(exc), file=sys.stderr)
        sys.exit(1)
