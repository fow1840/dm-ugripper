"""Read-only shared UVC/FT601 status for Web and HWS."""
import json
import os
from pathlib import Path
import sys
import time


def stream_status(side, imu=False, devroot=Path('/dev'), shm=Path('/dev/shm'), now_ns=None):
    path = devroot / (f'{side}_fays_imu' if imu else f'{side}_fays_stereo')
    present = path.exists()
    result = dict(present=present, stream=False)
    if not present:
        return result
    now = time.time_ns() if now_ns is None else now_ns
    for base in (shm, shm / 'ugripper'):
        try:
            data = json.loads((base / f'umi_{side}_fays_runtime_status.json').read_text())
            pid = int(data['pid'])
            if pid <= 0:
                continue
            os.kill(pid, 0)
            sample = int(data['last_imu_system_ns' if imu else 'last_frame_system_ns'])
            written = int(data['status_write_system_ns'])
            result['stream'] = (data.get('running') is True and
                                0 <= now - written < 3_000_000_000 and
                                0 <= now - sample < 3_000_000_000)
            return result
        except (OSError, ValueError, KeyError, TypeError):
            continue
    return result


if __name__ == '__main__':
    result = stream_status(sys.argv[1], sys.argv[2] == 'imu')
    print('OFF' if not result['present'] else 'OK' if result['stream'] else 'WARN')
