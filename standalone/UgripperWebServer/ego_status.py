"""Shared read-only Ego snapshot presentation for Web and hws; never calls ADB."""
import json
from pathlib import Path
import time

RUNTIME = Path('/dev/shm/ugripper')
MAX_AGE_MS = 15000  # Runtime probes every two seconds; allow probe timeouts.


def _read(path):
    try:
        value = json.loads(path.read_text())
        return value if isinstance(value, dict) else None
    except FileNotFoundError:
        return {}
    except (OSError, ValueError):
        return None


def ego_status(runtime=RUNTIME, now_ms=None):
    binding = _read(runtime / 'ego_binding.json')
    result = dict(bound=None, connected=None, serial='', state='unknown')
    if binding is None:
        return result
    serial = binding.get('serial', '')
    if not isinstance(serial, str):
        return result
    serial = serial.strip()
    result.update(bound=bool(serial), serial=serial)
    if not serial:
        result['state'] = 'unbound'
        return result
    result.update(software_version=(binding.get('app') or {}).get('version_name', ''),
                  firmware_version=(binding.get('firmware') or {}).get('version', ''),
                  versions_checked_at_ms=binding.get('versions_checked_at_ms'))
    snapshot = _read(runtime / 'ego_binding_status.json')
    # A deleted/replaced binding must not inherit an earlier probe's result.
    if _read(runtime / 'ego_binding.json') != binding:
        return dict(bound=None, connected=None, serial='', state='unknown')
    stamp = snapshot.get('updated_at_ms') if snapshot else None
    now = time.time() * 1000 if now_ms is None else now_ms
    if (not isinstance(stamp, (int, float)) or isinstance(stamp, bool)
            or not 0 <= now - stamp <= MAX_AGE_MS
            or snapshot.get('serial') != serial):
        return result
    state = snapshot.get('status')
    if state in ('connected', 'disconnected'):
        result.update(connected=state == 'connected', state=state)
    battery = snapshot.get('battery', {})
    if result['connected'] is True and isinstance(battery, dict):
        percent, stamp = battery.get('percent'), battery.get('updated_at_ms')
        if (type(percent) is int and 0 <= percent <= 100
                and type(stamp) in (int, float) and 0 <= now - stamp <= 20000):
            result['battery'] = dict(percent=percent, charging=battery.get('charging') is True)
    sync = _read(runtime / 'ego_time_sync.json') or {}
    stamp = sync.get('updated_at_ms')
    if (result['connected'] is True and sync.get('serial') == serial
            and type(stamp) in (int, float) and 0 <= now - stamp <= 5000):
        result['time_sync'] = {key: sync.get(key) for key in
            ('status', 'mode', 'offset_ms', 'rtt_ms', 'updated_at_ms')}
    return result


if __name__ == '__main__':
    status = ego_status()
    binding = '已绑定' if status['bound'] else '未绑定' if status['bound'] is False else '未知'
    connection = ('已连接' if status['connected'] else '未连接' if status['connected'] is False
                  else '未检测（未绑定）' if status['bound'] is False else '未知（等待状态更新）')
    print('\nEgo')
    print('  Binding:    ' + binding)
    print('  Connection: ' + connection)
    if status['serial']:
        print('  Serial:     ' + status['serial'])
