#!/usr/bin/env python3
"""Apply auxiliary CPU placement before starting the service's process tree."""
import os
from pathlib import Path
import sys
import time


def configure(profile, sys_root=Path('/sys')):
    if not profile or profile == 'off':
        return
    if profile != 'fays_split':
        raise ValueError('unknown UGRIPPER_CPU_PROFILE=' + profile)
    root = sys_root / 'devices/system/cpu'
    capacities = [int((root / f'cpu{i}/cpu_capacity').read_text()) for i in range(8)]
    if not (0 < capacities[0] < capacities[4] and
            capacities[:4] == [capacities[0]] * 4 and
            capacities[4:] == [capacities[4]] * 4):
        raise ValueError('fays_split requires four small and four big CPUs')
    # Capture the actual service allowance, before narrowing. Descendants can
    # widen to this original set for their role, but not an assumed CPU set.
    if not set(range(8)).issubset(os.sched_getaffinity(0)):
        raise ValueError('fays_split requires CPUs 0-7 in the original allowance')
    os.sched_setaffinity(0, set(range(4)))
    os.environ['UGRIPPER_CPU_BASE_MASK'] = 'ff'


def main():
    if len(sys.argv) < 3 or sys.argv[1] != '--':
        raise SystemExit('usage: cpu_affinity.py -- command [args ...]')
    # Ignore any caller-supplied baseline; establish it from kernel state.
    os.environ.pop('UGRIPPER_CPU_BASE_MASK', None)
    try:
        configure(os.environ.get('UGRIPPER_CPU_PROFILE', 'off'))
    except (OSError, ValueError) as exc:
        print(f'[{time.time():.6f}][tid {os.getpid()}][W][cpu_affinity 0] '
              f'CPU profile not applied: {exc}', file=sys.stderr, flush=True)
    os.execvp(sys.argv[2], sys.argv[2:])


if __name__ == '__main__':
    main()
