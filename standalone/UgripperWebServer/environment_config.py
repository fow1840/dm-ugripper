"""Fixed-path, allowlisted environment helper, called only by the root worker."""
import os
from pathlib import Path
import re
import stat
import tempfile

ENVIRONMENT = Path('/etc/environment')
OPTIONS = {
    'UGRIPPER_LANG': ('zh', 'en'),
    'CAMERA_CODEC': ('h265', 'h264'),
    'ENABLE_CHEST_CAM_MAIN': ('false', 'true'),
    'UGRIPPER_POWER_LINE_FREQUENCY': ('50', '60'),
}
ASSIGNMENT = re.compile(r'^\s*(?:export\s+)?([A-Za-z_][A-Za-z_0-9]*)\s*=')


def read_settings(keys=OPTIONS):
    values = {key: None for key in keys}
    for line in ENVIRONMENT.read_text().splitlines():
        match = ASSIGNMENT.match(line)
        if match and match[1] in values:
            raw = line[match.end():].strip()
            value = re.match(r'''^(?:"([^"]*)"|'([^']*)'|([^\s#]*))\s*(?:#.*)?$''', raw)
            values[match[1]] = next((v for v in value.groups() if v is not None), '') if value else raw
    return values


def validate_settings(values):
    if not isinstance(values, dict) or set(values) != set(OPTIONS):
        raise ValueError('仅允许配置指定的四个参数')
    for key, value in values.items():
        if not isinstance(value, str) or value not in OPTIONS[key]:
            raise ValueError(f'{key} 的选项无效')
    return dict(values)


def write_settings(values):
    values = validate_settings(values)
    if os.geteuid() != 0:
        raise PermissionError('配置写入必须通过 root 维护 helper')
    fd = os.open(ENVIRONMENT, os.O_RDONLY | os.O_NOFOLLOW)
    with os.fdopen(fd) as source:
        info = os.fstat(source.fileno())
        if not stat.S_ISREG(info.st_mode):
            raise ValueError('环境配置不是普通文件')
        original = source.read()
    lines = []; seen = set()
    for line in original.splitlines(keepends=True):
        match = ASSIGNMENT.match(line)
        if match and match[1] in values:
            key = match[1]
            if key not in seen:
                lines.append(f'{key}="{values[key]}"\n'); seen.add(key)
        else:
            lines.append(line)
    content = ''.join(lines)
    if content and not content.endswith('\n'): content += '\n'
    content += ''.join(f'{key}="{value}"\n' for key, value in values.items() if key not in seen)
    fd, name = tempfile.mkstemp(prefix='.ugripper-environment-', dir=ENVIRONMENT.parent)
    try:
        with os.fdopen(fd, 'w') as target:
            os.fchown(target.fileno(), info.st_uid, info.st_gid)
            os.fchmod(target.fileno(), stat.S_IMODE(info.st_mode))
            target.write(content); target.flush(); os.fsync(target.fileno())
        os.replace(name, ENVIRONMENT)
        directory = os.open(ENVIRONMENT.parent, os.O_RDONLY | os.O_DIRECTORY)
        try: os.fsync(directory)
        finally: os.close(directory)
    finally:
        Path(name).unlink(missing_ok=True)
    return values
