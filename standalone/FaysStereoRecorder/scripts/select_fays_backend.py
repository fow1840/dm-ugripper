#!/usr/bin/env python3
"""Resolve a configured device to its USB descriptor; never select the first camera."""
import hashlib
import os
from pathlib import Path
import re
import stat
import sys
import tempfile


def values(text):
    return {m[1]: m[2].split('#', 1)[0].strip().strip('\"\'')
            for m in re.finditer(r'^([a-z_]+):\s*(.*?)$', text, re.M)}


def usb_device(port, sysroot=Path('/sys')):
    if re.fullmatch(r'[0-9]+-[0-9]+(?:\.[0-9]+)*', port):
        node = sysroot / 'bus/usb/devices' / port
    else:
        info = Path(port).stat()
        if not stat.S_ISCHR(info.st_mode):
            raise ValueError('camera must be a USB or V4L2 character device')
        node = (sysroot / 'dev/char' / f'{os.major(info.st_rdev)}:{os.minor(info.st_rdev)}' / 'device').resolve()
        # USB usbfs char devices themselves may be the sysfs device.
        if not node.exists():
            node = (sysroot / 'dev/char' / f'{os.major(info.st_rdev)}:{os.minor(info.st_rdev)}').resolve()
    while node != node.parent:
        if (node / 'idVendor').exists() and (node / 'idProduct').exists():
            return node
        node = node.parent
    raise ValueError('no USB descriptor for configured camera: ' + port)


def select(text, sysroot=Path('/sys')):
    config = values(text)
    node = usb_device(config.get('stereo_dev_port', ''), sysroot)
    identity = ((node / 'idVendor').read_text().strip(), (node / 'idProduct').read_text().strip())
    if identity == ('0403', '602e'):
        return 'uvc', text
    if identity != ('0403', '601f'):
        raise ValueError('unsupported Fays USB descriptor: ' + repr(identity))
    replacements = {'device_type': 'ft601', 'stereo_dev_port': node.name, 'imu_dev_port': 'NULL', 'right_cam_rotate_180': '0'}
    for key, value in replacements.items():
        pattern = r'^' + key + r':.*$'
        text, count = re.subn(pattern, key + ': ' + value, text, flags=re.M)
        if not count:
            text += '\n' + key + ': ' + value + '\n'
    return 'ft601', text


def main():
    source = Path(sys.argv[1])
    backend, text = select(source.read_text())
    if backend == 'uvc':
        print('uvc'); print(source)
        return
    directory = Path(tempfile.gettempdir()) / ('ugripper-fays-config-' + str(os.getuid()))
    directory.mkdir(mode=0o700, exist_ok=True)
    info = directory.lstat()
    if directory.is_symlink() or info.st_uid != os.getuid() or info.st_mode & 0o077:
        raise ValueError('unsafe generated config directory')
    name = directory / (hashlib.sha256(str(source.resolve()).encode()).hexdigest()[:20] + '.yaml')
    fd, temporary = tempfile.mkstemp(dir=directory)
    try:
        with os.fdopen(fd, 'w') as stream:
            stream.write(text)
        os.replace(temporary, name)
    finally:
        if os.path.exists(temporary): os.unlink(temporary)
    print(backend); print(name)


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError) as exc:
        print('Fays backend selection failed: ' + str(exc), file=sys.stderr)
        sys.exit(1)
