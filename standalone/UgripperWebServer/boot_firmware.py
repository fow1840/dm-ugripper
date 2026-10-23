"""RK3588 release packages: verify, run bundled update.sh, verify across reboot."""
import contextlib
import hashlib
import os
from pathlib import Path
import stat
import re
import shutil
import tarfile
import tempfile
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
VERSION_INFO = Path('/home/user/version.info')
INSTALLED_FILES = {
    'payload/8852be.ko': Path('/home/user/lib/8852be.ko'),
    'payload/user_start.sh': Path('/home/user/user_start.sh'),
    'payload/50-default.conf': Path('/etc/rsyslog.d/50-default.conf'),
    'payload/rsyslog': Path('/etc/logrotate.d/rsyslog'),
}


def read_version(path=None):
    try:
        with (VERSION_INFO if path is None else path).open() as stream:
            value = stream.read(4097)
        if len(value) > 4096:
            return ''
        value = value.strip()
        if re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_.+-]{0,255}', value):
            return value
    except (OSError, UnicodeError):
        pass
    return ''


def file_hash(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024*1024), b''):
            digest.update(block)
    return digest.hexdigest()


@contextlib.contextmanager
def unpack_package(path):
    # Never extract links, special nodes, absolute paths, '..', or duplicate names.
    with tempfile.TemporaryDirectory(prefix='firmware-', dir=path.parent) as temporary:
        destination = Path(temporary)
        try:
            with tarfile.open(path, 'r:*') as archive:
                names, files, total = set(), [], 0
                for index, member in enumerate(archive):
                    name = member.name.rstrip('/')
                    if (index >= 256 or not name or len(name) > 200 or
                            any(not re.fullmatch(r'[A-Za-z0-9_.+-]+', part) or part in ('.', '..')
                                for part in name.split('/')) or name in names or
                            not (member.isdir() or member.isfile()) or member.sparse):
                        raise ValueError('升级包包含非法路径、链接或重复文件')
                    names.add(name)
                    if member.isdir():
                        continue
                    total += member.size
                    if member.size < 0 or total >= MAX_UPLOAD:
                        raise ValueError('升级包解压内容必须小于 200 MiB')
                    target = destination/name
                    target.parent.mkdir(parents=True, exist_ok=True)
                    with archive.extractfile(member) as source, target.open('xb') as output:
                        shutil.copyfileobj(source, output)
                    files.append(target)
        except (tarfile.TarError, EOFError, OSError) as exc:
            raise ValueError('请上传包含 update.sh、SHA256SUMS 和 payload 的完整升级包') from exc
        scripts = [f for f in files if f.name == 'update.sh']
        if len(scripts) != 1:
            raise ValueError('升级包必须且只能包含一个 update.sh')
        root = scripts[0].parent
        if any(not f.is_relative_to(root) for f in files):
            raise ValueError('升级包文件必须位于同一个发布目录')
        yield root


def inspect_package_root(root):
    checksums = {}
    try:
        manifest = (root/'SHA256SUMS').read_text()
        if len(manifest) > 65536:
            raise ValueError('SHA256SUMS 过大')
        for line in manifest.splitlines():
            match = re.fullmatch(r'([0-9a-f]{64}) [ *]([A-Za-z0-9_.+/-]+)', line)
            if not match:
                raise ValueError('SHA256SUMS 格式非法')
            digest, name = match.groups()
            if (name in checksums or any(part in ('', '.', '..') for part in name.split('/'))
                    or name.startswith('/')):
                raise ValueError('SHA256SUMS 路径非法或重复')
            if file_hash(root/name) != digest:
                raise ValueError('升级包 SHA-256 校验失败: '+name)
            checksums[name] = digest
    except (OSError, UnicodeError) as exc:
        raise ValueError('升级包文件或 SHA256SUMS 缺失') from exc
    required = {'update.sh', 'payload/version.info', 'payload/fstab', *INSTALLED_FILES,
                'payload/boot-v2.5.img', 'payload/boot-v3.img'}
    files = {str(f.relative_to(root)) for f in root.rglob('*') if f.is_file()}
    if not required <= checksums.keys() or files - {'SHA256SUMS', 'readme.txt'} != checksums.keys():
        raise ValueError('升级包文件清单不完整或含未校验文件')
    version = read_version(root/'payload/version.info')
    if not version:
        raise ValueError('升级包 version.info 无有效固件版本')
    compatible = COMPATIBLE.read_bytes()
    match = re.search(rb'V[0-9]+', compatible)
    model = match.group().decode() if match else ''
    image = {'V100': 'boot-v2.5.img', 'V130': 'boot-v3.img'}.get(model)
    if not image:
        raise ValueError('升级包不支持当前硬件型号: '+model)
    info = inspect_image(root/'payload'/image)
    return dict(**info, firmware_version=version, hardware_model=model,
                files={name: checksums[name] for name in INSTALLED_FILES})


def inspect_package(path):
    with unpack_package(path) as root:
        return inspect_package_root(root)


def verify_installed(info):
    if read_version() != info['firmware_version']:
        raise RuntimeError('升级后 version.info 与目标固件版本不一致')
    for name, digest in info['files'].items():
        if name not in INSTALLED_FILES or file_hash(INSTALLED_FILES[name]) != digest:
            raise RuntimeError('升级后文件 SHA-256 不匹配: '+name)
    if partition_hash(info['size']) != info['sha256']:
        raise RuntimeError('启动分区 SHA-256 校验失败')



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
    report('校验 3588 固件升级包')
    with unpack_package(path) as root:
        info = inspect_package_root(root)
        check_target(info['size'], run)
        require_battery()
        pending = dict(id=job['id'], kind='rk3588_boot', status='running', stage='执行 update.sh',
                       boot_id=BOOT_ID.read_text().strip(), **info)
        persist(pending)
        try:
            report('执行 3588 固件 update.sh，请勿断电', target_version=info['firmware_version'])
            # Fail on command errors even if the vendor script omits `set -e`.
            run(['/bin/sh', '-e', root/'update.sh'], 900)
            report('校验 3588 固件版本、文件和启动分区')
            verify_installed(info)
            run(['sync'])
            pending['stage'] = 'awaiting_reboot'; persist(pending)
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
        verify_installed(pending)
        old.update(status='success', success=True, boot_verified=True,
                   stage='3588 启动固件重启后校验通过', message='',
                   result={'sha256': pending['sha256'], 'firmware_version': pending['firmware_version'],
                           'status': '重启后校验通过',
                           'installed': True, 'boot_verified': True})
    except Exception as exc:
        old.update(status='failed', success=False, boot_verified=False,
                   error_code='boot_verification_failed', stage='3588 启动固件校验失败', message=str(exc))
    old.update(finished=time.time(), reboot_pending=False)
    save_task(old, STATE)
    PENDING.unlink(missing_ok=True)
