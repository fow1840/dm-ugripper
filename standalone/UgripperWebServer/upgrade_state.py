"""Shared task persistence and temporary package storage (no extra version snapshot)."""
import re
import shutil
from pathlib import Path
from common import STATE, atomic_json, read_json

PACKAGES = STATE/'upgrade-packages'
BOOT_ID = Path('/proc/sys/kernel/random/boot_id')
REBOOT_GUARD = Path('/run/ugripper/reboot-requested')


def save_task(task, state=None):
    state = STATE if state is None else state
    history = state/'upgrade-results'
    history.mkdir(exist_ok=True)
    atomic_json(history/(task['id']+'.json'), task)
    atomic_json(state/'task.json', task)


def task_result(jobid):
    if not re.fullmatch(r'[0-9a-f]{32}', jobid):
        raise ValueError('invalid_task_id')
    value = read_json(STATE/'upgrade-results'/(jobid+'.json'))
    if not isinstance(value, dict) or value.get('id') != jobid:
        latest = read_json(STATE/'task.json')
        value = latest if isinstance(latest, dict) and latest.get('id') == jobid else {}
    return value


def clear_packages():
    # rmtree does not follow directory symlinks; the root directory stays in place.
    for path in PACKAGES.iterdir():
        if path.is_dir() and not path.is_symlink():
            shutil.rmtree(path)
        else:
            path.unlink(missing_ok=True)


def recover_package_store():
    boot = BOOT_ID.read_text().strip()
    marker = STATE/'upgrade-storage-boot.json'
    previous = read_json(marker)
    if not isinstance(previous, dict) or previous.get('boot_id') != boot:
        clear_packages()
        REBOOT_GUARD.unlink(missing_ok=True)
        atomic_json(marker, {'boot_id': boot})
