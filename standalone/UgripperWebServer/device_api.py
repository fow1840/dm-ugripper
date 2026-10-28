"""Read-only public inventory. Reuse producer caches; never open device ports."""
import os
from pathlib import Path
import re
import threading
import time

from common import STATE, ALLOW, read_json, operation_lock, Busy
from telemetry import FIRMWARE_STATUS, battery_status
from ego_status import ego_status

DPKG_STATUS = Path('/var/lib/dpkg/status')
RECORDING_LOCK = Path('/tmp/umi_recording.lock')
_PACKAGE_LOCK = threading.Lock()
_PACKAGE_CACHE = None
ALIASES = {
    'software_version': 'ugripper', 'das_usb_updater_version': 'das-usb-updater',
    'bluetooth_gatt_server_version': 'bluetooth-gatt-server',
    'databot_device_joint_version': 'databot-device-joint',
    'device_ota_mender_version': 'device-ota-mender',
}
FIRMWARE_FIELDS = [f'{kind}_{side}_firmware_version' for kind, sides in
    [('gripper', ('left', 'right')), ('cam', ('left', 'right', 'chest')),
     ('stereo', ('left', 'right'))] for side in sides] + ['ego_software_version', 'ego_firmware_version', 'rk3588_firmware_version']
DEVICE_IDS = [name for side in ('left', 'right') for name in
    (f'{side}_gripper', f'{side}_encoder', f'cam_{side}', f'tcam_{side}_l',
     f'tcam_{side}_r', f'{side}_fays_stereo', f'{side}_fays_imu')] + ['cam_chest', 'ego']


def mapping(value):
    return value if isinstance(value, dict) else {}


def fresh(stamp, age=15000):
    return type(stamp) in (int, float) and 0 <= time.time()*1000 - stamp <= age


def select(query, key):
    values = query.get(key, [])
    if not values:
        return None
    names = list(dict.fromkeys(name.strip() for value in values for name in value.split(',')))
    if not names or len(names) > 128 or any(not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_.+:-]{0,127}', n) for n in names):
        raise ValueError('invalid_selector')
    return names


def packages():
    """dpkg's existing database is the cache, invalidated by inode/size/mtime."""
    global _PACKAGE_CACHE
    with _PACKAGE_LOCK:
        try:
            with DPKG_STATUS.open() as stream:
                info = os.fstat(stream.fileno())
                identity = (info.st_ino, info.st_size, info.st_mtime_ns, info.st_ctime_ns)
                if _PACKAGE_CACHE and _PACKAGE_CACHE[0] == identity and time.monotonic()-_PACKAGE_CACHE[3] < 1:
                    return _PACKAGE_CACHE[1], _PACKAGE_CACHE[2]
                result = {}
                fields = {}
                for line in stream:
                    if line.strip():
                        if not line[0].isspace() and ': ' in line:
                            key, value = line.rstrip('\n').split(': ', 1)
                            fields[key] = value
                    else:
                        _package_entry(fields, result)
                        fields = {}
                _package_entry(fields, result)
            stamp = info.st_mtime_ns // 1000000
            _PACKAGE_CACHE = (identity, result, stamp, time.monotonic())
            return result, stamp
        except (OSError, UnicodeError):
            return None, None


def _package_entry(fields, result):
    if fields.get('Status') == 'install ok installed' and fields.get('Package') and fields.get('Version'):
        value = fields['Version']
        result[fields['Package']] = value
        if fields.get('Architecture'):
            result[fields['Package']+':'+fields['Architecture']] = value


def versions(names=None, *, details=False, scope="product"):
    if scope not in ('product', 'all'):
        raise ValueError('invalid_scope')
    cached = mapping(read_json(FIRMWARE_STATUS))
    values = mapping(cached.get('device_versions'))
    stamp = cached.get('updated_at_ms')
    current = fresh(stamp)
    installed, package_stamp = packages()
    default_packages = sorted(n for n in (installed or {}) if ':' not in n)
    keys = names if names is not None else list(dict.fromkeys(
        ['hardware_version', 'data_version', *ALLOW, *FIRMWARE_FIELDS] +
        (default_packages if scope == 'all' else [])))
    result = {}
    for name in keys:
        if name in ALIASES or name not in ('hardware_version', 'data_version', *FIRMWARE_FIELDS) and name not in values:
            package = ALIASES.get(name, name)
            value = (installed or {}).get(package, '')
            if name == 'das_usb_updater_version' and not value:
                value = (installed or {}).get('ugripper-usb-updater', '')
            if name == 'software_version' and value and value[0] not in 'vV':
                value = 'v' + value
            result[name] = dict(version=value or None, status='ready' if value else 'unavailable' if installed is None else 'not_installed',
                                source='dpkg', updated_at_ms=package_stamp)
        else:
            value = values.get(name)
            value = value if isinstance(value, str) and value else None
            state = ('stale' if stamp is not None else 'unavailable') if not current else (
                'disabled' if value == 'disabled' else 'ready' if value else 'unrecognized')
            result[name] = dict(version=value if current else None, status=state,
                                source=str(FIRMWARE_STATUS), updated_at_ms=stamp)
    if not details:
        result = {name: {key: item[key] for key in ('version', 'status')}
                  for name, item in result.items()}
    return {'versions': result}


def devices(ids=None, *, details=False):
    if ids and any(name not in DEVICE_IDS for name in ids):
        raise ValueError('unknown_device')
    cached = mapping(read_json(FIRMWARE_STATUS))
    inventory = mapping(cached.get('devices'))
    stamp = cached.get('updated_at_ms')
    result = {}
    for name in DEVICE_IDS if ids is None else ids:
        item = mapping(inventory.get(name))
        path = (None if name == 'ego' else '/dev/stereo_'+name.split('_')[0]
                if name.endswith('_fays_stereo') else '/dev/'+name)
        value = dict(id=name, path=path, serial=None, enabled=None,
                     connected=None, identified=None, stream=None, status='unavailable',
                     updated_at_ms=stamp, source=str(FIRMWARE_STATUS))
        if fresh(stamp) and item:
            value.update(item)
            enabled, connected, identified = (value.get(k) for k in ('enabled', 'connected', 'identified'))
            value['status'] = ('disabled' if enabled is False else 'disconnected' if connected is False
                               else 'unrecognized' if connected is True and identified is False
                               else 'connected' if connected is True else 'unknown')
            if value.get('error')=='gripper_version_mismatch':value['status']='gripper_version_mismatch'
            if name.endswith(('_fays_imu', '_fays_stereo')) and enabled is True:
                side = name.split('_')[0]
                from fays_status import stream_status
                stream = stream_status(side, name.endswith('imu'))
                value['stream'] = stream['stream']


        elif stamp is not None:
            value['status'] = 'stale'
        if name == 'ego':
            ego = ego_status()
            value.update(serial=ego.get('serial') or None, bound=ego.get('bound'),
                         connected=ego.get('connected'), identified=bool(ego.get('serial')),
                         status=ego.get('state', 'unknown'), source='ego_binding_status.json')
            # The binding status reader has already enforced freshness and identity.
            value['enabled'] = ego.get('bound')
            value['updated_at_ms'] = ego.get('updated_at_ms')
        value['serial'] = value.get('serial') or None
        if not details:
            value = {key: value.get(key) for key in
                     ('serial', 'enabled', 'connected', 'identified', 'stream', 'status')}
            if name == 'ego':
                value['bound'] = ego.get('bound')
        result[name] = value
    return {'devices': result}


def recording():
    """The recording lock covers startup/finalize; never infer idle from its absence alone."""
    lock = mapping(read_json(RECORDING_LOCK))
    cached = mapping(read_json(FIRMWARE_STATUS))
    phase = 'unknown'
    active = None
    if RECORDING_LOCK.exists():
        pid = lock.get('pid')
        if type(pid) is int and pid > 0 and Path('/proc', str(pid)).exists():
            phase = lock.get('state', 'active')
            if phase not in ('starting', 'recording', 'finalizing'):
                phase = 'active'
            active = True
    elif fresh(cached.get('updated_at_ms')):
        state = mapping(cached.get('recording'))
        if state.get('active') is False:
            phase, active = 'idle', False
    available = False
    try:
        with operation_lock():
            available = True
    except (Busy, OSError):
        pass
    task = mapping(read_json(STATE/'task.json'))
    pending = task.get('reboot_pending') is True
    battery = battery_status()
    battery_ready = (battery.get('connected') is True and type(battery.get('percent')) is int
                     and 30 <= battery['percent'] <= 100)
    return dict(state=phase, active=active, episode_dir=lock.get('episode_dir'),
                updated_at_ms=lock.get('updated_at_ms', cached.get('updated_at_ms')),
                maintenance_available=available and not pending,
                battery_ready=battery_ready,
                can_upgrade=active is False and available and not pending and battery_ready)


def option(query, name, default):
    values = query.get(name, [default])
    if len(values) != 1:
        raise ValueError('invalid_' + name)
    return values[0]


def detail_option(query):
    value = option(query, 'details', 'false')
    if value not in ('true', 'false'):
        raise ValueError('invalid_details')
    return value == 'true'


def task_summary(value):
    """Bounded polling contract: no logs, per-round results or image payloads."""
    keys = ('id', 'status', 'stage', 'success', 'started', 'updated', 'finished',
            'reboot_pending', 'boot_verified', 'phase', 'group', 'round',
            'remaining_s', 'phase_deadline')
    return {key: value[key] for key in keys
            if key in value and (value[key] is None or type(value[key]) in (str, int, float, bool))}


def status_summary():
    from production_test import snapshot
    from time_sync_test import snapshot as sync_snapshot
    from inspection import snapshot as inspection_snapshot
    return dict(inspection=task_summary(mapping(inspection_snapshot())),recording=recording(), battery=battery_status(),
                task=task_summary(mapping(read_json(STATE/'task.json'))),
                production=task_summary(mapping(snapshot())),
                time_sync=task_summary(mapping(sync_snapshot())))
