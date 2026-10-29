"""Read-only status: never opens serial/video nodes owned by recorders."""
import os
import re
from pathlib import Path
import shutil
import time
from common import DATA, STATE, command, read_json, operation_lock, Busy, ALLOW

from environment_config import read_settings
from ego_status import ego_status
from tactile_panel import panel_status, latest_alarm

CPU_PREV = None

def disk(path, required=False):
    mounted = os.path.ismount(path) if required else True
    if not mounted:
        return {'mounted': False, 'total': 0, 'used': 0, 'free': 0}
    d = shutil.disk_usage(path)
    return dict(mounted=True, total=d.total, used=d.used, free=d.free)

FIRMWARE_STATUS = Path('/dev/shm/ugripper/firmware_status.json')
BMS_SOURCE = Path('/opt/bluetooth_server/bms_latest.txt')


def battery_status():
    """Read the Bluetooth-owned snapshot independently of recorder lifecycle."""
    unavailable = {'connected': False, 'status': 'service_unavailable'}
    keys = ('timestamp', 'soc_percent', 'totalCapacity_mAh', 'remainCapacity_mAh',
            'totalVoltage_mV', 'current_mA', 'isCharging', 'isDischarging')
    try:
        with BMS_SOURCE.open('rb') as stream:
            raw = stream.read(4097)
        if len(raw) > 4096:
            return unavailable
        fields = {}
        for line in raw.decode('utf-8').splitlines():
            key, separator, value = line.partition(':')
            if not separator or key not in keys:
                continue
            value = value.strip()
            if key in fields or not re.fullmatch(r'[+-]?[0-9]+', value):
                return unavailable
            fields[key] = int(value)
        if any(key not in fields for key in keys):
            return unavailable
        now = int(time.time() * 1000)
        age = now - fields['timestamp'] * 1000
        if fields['timestamp'] <= 0 or not 0 <= age <= 20000:
            return unavailable
        if any(not 0 <= fields[key] <= 2147483647 for key in
               ('soc_percent', 'totalCapacity_mAh', 'remainCapacity_mAh', 'totalVoltage_mV')):
            return unavailable
        if (fields['soc_percent'] > 100 or not -2147483648 <= fields['current_mA'] <= 2147483647
                or fields['isCharging'] not in (0, 1) or fields['isDischarging'] not in (0, 1)):
            return unavailable
        return dict(connected=True, status='ready', source=str(BMS_SOURCE), updated_at_ms=now,
                    sample_age_ms=age, percent=fields['soc_percent'],
                    charging=bool(fields['isCharging']), discharging=bool(fields['isDischarging']),
                    voltage_mv=fields['totalVoltage_mV'], current_ma=fields['current_mA'],
                    capacity_mah=fields['totalCapacity_mAh'], remaining_mah=fields['remainCapacity_mAh'])
    except (OSError, UnicodeError, ValueError):
        return unavailable


def firmware_status():
    cached = read_json(FIRMWARE_STATUS)
    if not isinstance(cached, dict):
        cached = {}
    stamp = cached.get('updated_at_ms')
    fresh = (isinstance(stamp, (int, float)) and not isinstance(stamp, bool)
             and 0 <= time.time() * 1000 - stamp <= 15000)
    result = {'main_camera': {}, 'gripper': {}}
    for side in ('left', 'right'):
        for group in result:
            items = cached.get(group, {}) if fresh else {}
            item = items.get(side, {}) if isinstance(items, dict) else {}
            result[group][side] = item if isinstance(item, dict) else {}
    return result


def status():
    global CPU_PREV
    cpu = [int(v) for v in Path('/proc/stat').read_text().splitlines()[0].split()[1:9]]
    now = (sum(cpu), cpu[3] + cpu[4])
    usage = None
    if CPU_PREV and now[0] > CPU_PREV[0]:
        usage = round(100 * (1 - (now[1] - CPU_PREV[1]) / (now[0] - CPU_PREV[0])), 1)
    CPU_PREV = now
    mem = {l.split(':')[0]: int(l.split()[1])*1024 for l in Path('/proc/meminfo').read_text().splitlines()}
    devices = []
    for side, label in [('left', '左'), ('right', '右')]:
        for key, title in [(f'{side}_gripper','夹爪'),(f'{side}_encoder','编码器'),
                           (f'cam_{side}','主摄'),(f'tcam_{side}_l','触觉 L'),(f'tcam_{side}_r','触觉 R'),
                           (f'{side}_fays_stereo','双目'),(f'{side}_fays_imu','双目 IMU')]:
            present = Path('/dev', key).exists()
            item = dict(id=key, side=side, label=label+title, present=present, stream=None)
            if 'fays' in key:
                from fays_status import stream_status
                item.update(stream_status(side, key.endswith('imu')))
            devices.append(item)
    if Path('/dev/cam_chest').exists():
        devices.append(dict(id='cam_chest', side='extra', label='胸部主摄', present=True, stream=None))
    env = {}
    for line in Path('/etc/environment').read_text().splitlines():
        if '=' in line:
            k,v = line.split('=',1); env[k] = v.strip('"\' ')
    try:
        with operation_lock(): busy = False
    except Busy:
        busy = True
    from production_test import snapshot
    from time_sync_test import snapshot as sync_snapshot
    from inspection import snapshot as inspection_snapshot
    from device_api import versions as api_versions, devices as api_devices, recording as api_recording
    return dict(inspection=inspection_snapshot(),time_sync=sync_snapshot(),production=snapshot(),device_sn=env.get('DEVICE_SN',''), versions=api_versions()["versions"], devices=api_devices()["devices"], device_cards=devices,
                cpu_percent=usage, memory=dict(total=mem['MemTotal'], used=mem['MemTotal']-mem['MemAvailable']),
                disks=dict(data=disk(DATA, True), system=disk('/')), busy=busy,
                recording=api_recording(),
                firmware=firmware_status(), device_versions=device_versions(), battery=battery_status(), configuration=read_settings(), ego=ego_status(), tactile=panel_status(), tactile_alarm=latest_alarm(),
                task=read_json(STATE/'task.json'),
                service_active=command(['systemctl','is-active','ugripper.service']).returncode == 0)


def device_versions():
    """Read versions from the existing runtime firmware cache."""
    cached = read_json(FIRMWARE_STATUS)
    if not isinstance(cached, dict):
        return {}
    stamp = cached.get('updated_at_ms')
    if (not isinstance(stamp, (int, float)) or isinstance(stamp, bool)
            or not 0 <= time.time() * 1000 - stamp <= 15000):
        return {}
    versions = cached.get('device_versions', {})
    return {key: value for key, value in versions.items() if isinstance(value, str)} if isinstance(versions, dict) else {}
