"""Small, versioned OS boundary shared by HTTP and maintenance processes."""
import contextlib
import fcntl
import json
import os
from pathlib import Path
import socket
import signal
import subprocess

BASE = Path(__file__).resolve().parent
ROOT = Path('/opt/ugripper')
STATE = Path(os.environ.get('UGRIPPER_WEB_STATE', '/var/lib/ugripper-web'))
RUN = Path(os.environ.get('UGRIPPER_WEB_RUN', '/run/ugripper-web'))
DATA = Path(os.environ.get('UGRIPPER_WEB_DATA', '/mnt/data_disk'))
LOCK = Path(os.environ.get('UGRIPPER_WEB_LOCK', '/run/ugripper/maintenance.lock'))
MAX_UPLOAD = 200 * 1024 * 1024
ALLOW = ('das-usb-updater', 'ugripper', 'bluetooth-gatt-server', 'databot-device-joint', 'device-ota-mender')

class Busy(RuntimeError):
    pass

def read_json(path, default=None):
    try:
        return json.loads(Path(path).read_text())
    except (OSError, ValueError):
        return {} if default is None else default

def atomic_json(path, value):
    path = Path(path)
    tmp = path.with_suffix('.new')
    with tmp.open('w') as stream:
        json.dump(value, stream, ensure_ascii=False)
        stream.flush()
        os.fsync(stream.fileno())
    tmp.replace(path)

def command(args, timeout=15):
    argv=[str(a) for a in args]
    with subprocess.Popen(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                          start_new_session=True,
                          env={'PATH':'/usr/sbin:/usr/bin:/sbin:/bin', 'LANG':'C.UTF-8',
                               'DEBIAN_FRONTEND':'noninteractive'}) as process:
        try:
            out,err=process.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGTERM)
            try: process.communicate(timeout=3)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.communicate()
            raise
        return subprocess.CompletedProcess(argv,process.returncode,out,err)

@contextlib.contextmanager
def operation_lock():
    # Never unlink this inode: recording, download and maintenance share it.
    fd = os.open(LOCK, os.O_RDWR | os.O_CREAT | os.O_CLOEXEC | os.O_NOFOLLOW, 0o660)
    try:
        try:
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as exc:
            raise Busy('设备正在录制、下载或维护，请稍后重试') from exc
        # Compatibility guard for a runtime installed before the flock contract.
        if Path('/tmp/umi_recording.lock').exists():
            raise Busy('设备正在录制或收尾')
        yield
    finally:
        os.close(fd)

def rpc(request):
    with socket.socket(socket.AF_UNIX) as sock:
        sock.settimeout(20)
        sock.connect(str(RUN / 'maintenance.sock'))
        sock.sendall(json.dumps(request).encode() + b'\n')
        with sock.makefile('rb') as stream:
            result = json.loads(stream.readline(8 * 1024 * 1024))
    if not result.get('ok'):
        if result.get('busy'):
            raise Busy(result['error'])
        raise ValueError(result.get('error', '维护服务不可用'))
    return result
