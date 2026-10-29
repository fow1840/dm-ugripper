"""RK3588 release packages: verify, run bundled update.sh, verify across reboot."""
import contextlib
import hashlib
import os
from pathlib import Path
import re
import shutil
import tarfile
import tempfile
import time

from common import MAX_UPLOAD, STATE, atomic_json, read_json
from upgrade_state import save_task

MIN_UPGRADE_PERCENT = 30
BMS_SOURCE = Path('/opt/bluetooth_server/bms_latest.txt')
COMPATIBLE = Path('/proc/device-tree/compatible')
BOOT_ID = Path('/proc/sys/kernel/random/boot_id')
PENDING = Path('/var/lib/ugripper-maintenance/boot-pending.json')
VERSION_INFO = Path('/home/user/version.info')


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
    required = {'update.sh', 'payload/version.info'}
    files = {str(f.relative_to(root)) for f in root.rglob('*') if f.is_file()}
    if not required <= checksums.keys() or files - {'SHA256SUMS', 'readme.txt'} != checksums.keys():
        raise ValueError('升级包文件清单不完整或含未校验文件')
    version = read_version(root/'payload/version.info')
    if not version:
        raise ValueError('升级包 version.info 无有效固件版本')
    # version.info may contain only the version suffix; the release directory
    # preserves the complete board/version identifier across upload staging.
    identity = version if version.startswith('RB1021') else root.name
    release = re.fullmatch(r'(RB1021V130|RB1021)_(.+)', identity)
    if not release:
        raise ValueError('升级包缺少有效的板型版本标识')
    compatible = COMPATIBLE.read_bytes()
    match = re.search(rb'V[0-9]+', compatible)
    model = match.group().decode() if match else ''
    expected = {'V100': 'RB1021', 'V130': 'RB1021V130'}.get(model)
    if expected is None or release.group(1) != expected:
        raise ValueError('固件板型与当前硬件不匹配: '+release.group(1)+' / '+model)
    return dict(firmware_version=version, hardware_model=model, release_id=identity,
                sha256=file_hash(root/'SHA256SUMS'))


def inspect_package(path):
    with unpack_package(path) as root:
        return inspect_package_root(root)


def verify_installed(info):
    if read_version() != info['firmware_version']:
        raise RuntimeError('升级后 version.info 与目标固件版本不一致')



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
        require_battery()
        pending = dict(id=job['id'], kind='rk3588_boot', status='running', stage='执行 update.sh',
                       boot_id=BOOT_ID.read_text().strip(), **info)
        persist(pending)
        try:
            report('执行 3588 固件 update.sh，请勿断电', target_version=info['firmware_version'])
            # Fail on command errors even if the vendor script omits `set -e`.
            run(['/bin/sh', '-e', root/'update.sh'], 900)
            report('确认 3588 固件安装版本')
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
        verify_installed(pending)
        old.update(status='success', success=True, boot_verified=True,
                   stage='3588 启动固件重启后校验通过', message='',
                   result={'sha256': pending.get('sha256'), 'firmware_version': pending['firmware_version'],
                           'status': '重启后校验通过',
                           'installed': True, 'boot_verified': True})
    except Exception as exc:
        old.update(status='failed', success=False, boot_verified=False,
                   error_code='boot_verification_failed', stage='3588 启动固件校验失败', message=str(exc))
    old.update(finished=time.time(), reboot_pending=False)
    save_task(old, STATE)
    PENDING.unlink(missing_ok=True)
