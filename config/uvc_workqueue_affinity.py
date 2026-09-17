#!/usr/bin/env python3
"""Keep USB copy/refill workers on RK3588 big cores while recording runs."""

import fcntl
import json
import os
from pathlib import Path
import sys


def cpu_list(value):
    result = set()
    for part in value.strip().split(','):
        bounds = part.split('-')
        first, last = int(bounds[0]), int(bounds[-1])
        if len(bounds) > 2 or first < 0 or last < first:
            raise ValueError('invalid CPU list')
        result.update(range(first, last + 1))
    return result


def mask_value(value):
    return int(value.strip().replace(',', ''), 16)


def mask_text(value):
    text = format(value, 'x')
    groups = []
    while text:
        groups.insert(0, text[-8:])
        text = text[:-8]
    return ','.join(groups) + '\n'


def select_big_cores(sys_root, current):
    compatible = (sys_root / 'firmware/devicetree/base/compatible').read_bytes().split(b'\0')
    if not {b'rockchip,rk3588', b'rockchip,rk3588s'}.intersection(compatible):
        return current
    root = sys_root / 'devices/system/cpu'
    online = cpu_list((root / 'online').read_text())
    capacity = {cpu: int((root / ('cpu' + str(cpu)) / 'cpu_capacity').read_text()) for cpu in online}
    maximum = max(capacity.values())
    if min(capacity.values()) <= 0 or len(set(capacity.values())) < 2:
        return current
    big = sum(1 << cpu for cpu, value in capacity.items() if value == maximum)
    # Respect an administrator's existing CPU restriction; never widen it.
    return (current & big) or current


def restore(mask_path, state_path):
    if not state_path.exists():
        return
    state = json.loads(state_path.read_text())
    current = mask_value(mask_path.read_text())
    if current == state['applied']:
        mask_path.write_text(mask_text(state['previous']))
        print('UVC workqueue affinity: restored ' + mask_text(state['previous']).strip())
    else:
        print('UVC workqueue affinity: external mask change preserved')
    state_path.unlink()


def apply(sys_root, state_path, enabled=True):
    mask_path = sys_root / 'devices/virtual/workqueue/cpumask'
    if not enabled:
        restore(mask_path, state_path)
        return
    current = mask_value(mask_path.read_text())
    if state_path.exists():
        state = json.loads(state_path.read_text())
        if current == state['applied']:
            return
        # A previous run may have stopped between saving state and applying it.
        if current != state['previous']:
            print('UVC workqueue affinity: external mask change preserved')
            state_path.unlink()
            return
        state_path.unlink()
    selected = select_big_cores(sys_root, current)
    if selected == current:
        return
    # Save restoration state before changing the kernel, including interrupted starts.
    temporary = state_path.with_suffix('.tmp')
    temporary.write_text(json.dumps({'previous': current, 'applied': selected}))
    temporary.replace(state_path)
    mask_path.write_text(mask_text(selected))
    if mask_value(mask_path.read_text()) != selected:
        raise RuntimeError('kernel did not apply the requested workqueue mask')
    print('UVC workqueue affinity: ' + mask_text(current).strip() + ' -> ' + mask_text(selected).strip())


def main():
    if len(sys.argv) != 2 or sys.argv[1] not in ('apply', 'restore'):
        raise ValueError('expected apply or restore')
    runtime = Path('/run/ugripper-workqueue-affinity')
    runtime.mkdir(mode=0o700, exist_ok=True)
    info = runtime.lstat()
    if runtime.is_symlink() or info.st_uid != 0 or info.st_mode & 0o077:
        raise RuntimeError('unsafe affinity state directory')
    with (runtime / 'lock').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        state = runtime / 'state.json'
        if sys.argv[1] == 'restore':
            restore(Path('/sys/devices/virtual/workqueue/cpumask'), state)
        else:
            setting = os.environ.get('UGRIPPER_UVC_WORKQUEUE_BIG_CORES', 'true').strip().lower()
            if setting not in ('true', 'false'):
                raise ValueError('UGRIPPER_UVC_WORKQUEUE_BIG_CORES must be true or false')
            apply(Path('/sys'), state, setting == 'true')


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, KeyError, RuntimeError) as exc:
        print('UVC workqueue affinity: ' + str(exc), file=sys.stderr)
        sys.exit(1)
