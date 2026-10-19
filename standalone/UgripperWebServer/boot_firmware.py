"""RK3588 FIT boot image update, fixed partition, verified across reboot.

Based on production_tools/.production_internal/deploy.py boot update workflow.
Never accepts a partition or shell command from the HTTP client.
"""
import hashlib
import os
from pathlib import Path
import stat
import struct
import time

from common import MAX_UPLOAD, STATE, atomic_json, read_json
from upgrade_state import save_task

MIN_UPGRADE_PERCENT = 30
BMS_SOURCE = Path('/opt/bluetooth_server/bms_latest.txt')
DEVICE = Path('/dev/mmcblk0p3')
UEVENT = Path('/sys/class/block/mmcblk0p3/uevent')
COMPATIBLE = Path('/proc/device-tree/compatible')
BOOT_ID = Path('/proc/sys/kernel/random/boot_id')
PENDING = Path('/var/lib/ugripper-maintenance/boot-pending.json')


def require_battery():
    """Read the live BMS producer, which continues after recording is stopped."""
    unavailable = '电量数据不可用或已过期，禁止升级'
    try:
        with BMS_SOURCE.open() as stream:
            text = stream.read(4097)
        if len(text) > 4096:
            raise ValueError(unavailable)
        fields = {}
        for line in text.splitlines():
            key, separator, value = line.partition(':')
            if separator and key in ('timestamp', 'soc_percent'):
                if key in fields:
                    raise ValueError(unavailable)
                fields[key] = int(value.strip())
        stamp, percent = fields['timestamp'], fields['soc_percent']
        if stamp <= 0 or not 0 <= time.time() - stamp <= 20 or not 0 <= percent <= 100:
            raise ValueError(unavailable)
    except (OSError, UnicodeError, ValueError, KeyError) as exc:
        raise ValueError(unavailable) from exc
    if percent < MIN_UPGRADE_PERCENT:
        raise ValueError(f'背包电量低于 {MIN_UPGRADE_PERCENT}%，禁止升级')
    return percent


def tree(data):
    """Read bounded FDT properties; external payloads remain outside the tree."""
    if len(data) < 40:
        raise ValueError('启动镜像头不完整')
    magic, total, offset, strings, reserve, version, last, cpu, string_size, structure_size = struct.unpack_from('>10I', data)
    if (magic != 0xd00dfeed or version != 17 or last > 17 or not 40 <= total <= len(data)
            or offset < 40 or strings < 40 or offset + structure_size > total
            or strings + string_size > total):
        raise ValueError('仅支持有效的 FIT boot.img，不支持整盘镜像或主摄固件')
    names = data[strings:strings + string_size]
    end = offset + structure_size
    stack, nodes = [], {}
    while offset + 4 <= end:
        token, = struct.unpack_from('>I', data, offset); offset += 4
        if token == 1:
            stop = data.find(b'\0', offset, end)
            if stop < 0: break
            name = data[offset:stop].decode('ascii')
            if '/' in name or (not stack and name): break
            stack.append(name)
            key = '/'.join(stack)
            if key in nodes: break
            nodes[key] = {}
            offset = (stop + 4) & ~3
        elif token == 2:
            if not stack: break
            stack.pop()
        elif token == 3:
            if not stack or offset + 8 > end: break
            length, key = struct.unpack_from('>II', data, offset); offset += 8
            stop = names.find(b'\0', key)
            if stop < 0 or offset + length > end: break
            name = names[key:stop].decode('ascii')
            props = nodes['/'.join(stack)]
            if name in props: break
            props[name] = data[offset:offset+length]
            offset = (offset + length + 3) & ~3
        elif token == 9:
            if stack or '' not in nodes: break
            return nodes, total
        elif token != 4:
            break
    raise ValueError('启动镜像 FIT 结构损坏')


def inspect_image(path):
    size = path.stat().st_size
    if not 0 < size < MAX_UPLOAD:
        raise ValueError('启动镜像必须非空且小于 200 MiB')
    data = path.read_bytes()
    nodes, total = tree(data)
    config_name = nodes.get('/configurations', {}).get('default', b'').rstrip(b'\0').decode('ascii')
    config = nodes.get('/configurations/' + config_name, {})
    payloads = {}
    for key, props in nodes.items():
        if not key.startswith('/images/') or key.count('/') != 2:
            continue
        if props.get('arch') != b'arm64\0':
            raise ValueError('启动镜像必须为 arm64')
        payload = props.get('data')
        if payload is None:
            length = props.get('data-size', b'')
            position = props.get('data-position', b'')
            relative = props.get('data-offset', b'')
            if len(length) != 4 or (len(position) != 4 and len(relative) != 4):
                raise ValueError('FIT 数据位置缺失')
            length = int.from_bytes(length, 'big')
            start = int.from_bytes(position, 'big') if position else ((total + 3) & ~3) + int.from_bytes(relative, 'big')
            if start < total or length <= 0 or start + length > size:
                raise ValueError('FIT 数据范围超出文件')
            payload = data[start:start+length]
        hashes = [v for k, v in nodes.items() if k.startswith(key+'/') and k.count('/') == 3 and v.get('algo') == b'sha256\0']
        if not payload or not hashes or any(h.get('value') != hashlib.sha256(payload).digest() for h in hashes):
            raise ValueError('FIT 内容 SHA-256 校验失败')
        payloads[key.rsplit('/', 1)[1]] = (props, payload)
    kernel = config.get('kernel', b'').rstrip(b'\0').decode('ascii')
    fdt = config.get('fdt', b'').rstrip(b'\0').decode('ascii')
    if kernel not in payloads or fdt not in payloads or payloads[kernel][0].get('type') != b'kernel\0':
        raise ValueError('FIT 默认配置缺少 kernel 或设备树')
    dt, _ = tree(payloads[fdt][1])
    if b'rockchip,rk3588' not in dt[''].get('compatible', b'').split(b'\0'):
        raise ValueError('镜像设备树不是 RK3588')
    return {'size': size, 'sha256': hashlib.sha256(data).hexdigest()}


def check_target(size, run):
    if b'rockchip,rk3588' not in COMPATIBLE.read_bytes().split(b'\0'):
        raise ValueError('当前设备不是 RK3588')
    if not stat.S_ISBLK(DEVICE.stat().st_mode):
        raise ValueError('启动分区不是块设备')
    fields = dict(line.split('=', 1) for line in UEVENT.read_text().splitlines() if '=' in line)
    if fields.get('PARTNAME') != 'boot' or fields.get('PARTN') != '3':
        raise ValueError('mmcblk0p3 不是预期的 boot 分区，拒绝写入')
    capacity = int(run(['blockdev', '--getsize64', DEVICE]).strip())
    if not 0 < size <= capacity:
        raise ValueError('启动镜像超过 boot 分区容量')


def partition_hash(size):
    digest = hashlib.sha256()
    with DEVICE.open('rb', buffering=0) as stream:
        while size:
            block = stream.read(min(size, 4*1024*1024))
            if not block: raise RuntimeError('启动分区读取不完整')
            digest.update(block); size -= len(block)
    return digest.hexdigest()


def persist(value):
    atomic_json(PENDING, value)
    with PENDING.open('rb') as f: os.fsync(f.fileno())
    fd = os.open(PENDING.parent, os.O_RDONLY | os.O_DIRECTORY)
    try: os.fsync(fd)
    finally: os.close(fd)


def install(path, job, report, run, force=False):
    require_battery()
    report('检查 3588 启动镜像')
    info = inspect_image(path)
    check_target(info['size'], run)
    if partition_hash(info['size']) == info['sha256'] and not force:
        return dict(status='启动分区已匹配，跳过写入与重启', **info)
    require_battery()
    pending = dict(id=job['id'], kind='rk3588_boot', status='running', stage='写入启动分区',
                   boot_id=BOOT_ID.read_text().strip(), **info)
    persist(pending)
    try:
        report('写入 3588 启动分区，请勿断电')
        run(['dd', 'if='+str(path), 'of='+str(DEVICE), 'bs=4M', 'conv=fsync', 'status=progress'], 600)
        report('回读校验 3588 启动分区')
        if partition_hash(info['size']) != info['sha256']:
            raise RuntimeError('启动分区写入后 SHA-256 不匹配，未重启')
        pending['stage'] = 'awaiting_reboot'; persist(pending)
        run(['sync'])
        return dict(status='awaiting_reboot', installed=True, boot_verified=False,
                    reboot_pending=True, **info)
    except Exception as exc:
        pending.update(status='failed', message=str(exc)); persist(pending)
        raise


def recover(run):
    """Never reflash on startup. Preserve awaiting-reboot across service restarts."""
    pending = read_json(PENDING)
    if not isinstance(pending, dict) or not pending:
        return
    old = read_json(STATE/'task.json')
    if not isinstance(old, dict) or old.get('id') != pending.get('id'):
        return  # Keep unresolved recovery evidence; submit will refuse another upgrade.
    try:
        if pending.get('status') != 'running' or pending.get('stage') != 'awaiting_reboot':
            raise RuntimeError('启动固件任务中断，未自动重新刷写；请检查设备')
        if BOOT_ID.read_text().strip() == pending.get('boot_id'):
            old.update(status='awaiting_reboot', stage='固件写入校验成功，等待重启指令',
                       success=True, reboot_pending=True, boot_verified=False)
            save_task(old, STATE)
            return
        check_target(pending['size'], run)
        if partition_hash(pending['size']) != pending['sha256']:
            raise RuntimeError('重启后启动分区 SHA-256 校验失败')
        old.update(status='success', success=True, boot_verified=True,
                   stage='3588 启动固件重启后校验通过', message='',
                   result={'sha256': pending['sha256'], 'status': '重启后校验通过',
                           'installed': True, 'boot_verified': True})
    except Exception as exc:
        old.update(status='failed', success=False, boot_verified=False,
                   error_code='boot_verification_failed', stage='3588 启动固件校验失败', message=str(exc))
    old.update(finished=time.time(), reboot_pending=False)
    save_task(old, STATE)
    PENDING.unlink(missing_ok=True)
