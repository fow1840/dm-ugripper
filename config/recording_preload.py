#!/usr/bin/env python3
"""Keep recording tools' file pages resident for the service lifetime.

The child runtime is never launched unless every selected file is mlocked.
No periodic touching, cache files or per-recording allocation is needed.
"""
import ctypes
import mmap
import os
from pathlib import Path
import re
import resource
import shutil
import signal
import subprocess
import sys
import time

MAX_BYTES = 512 * 1024 * 1024
RESERVE_BYTES = 512 * 1024 * 1024
PREPARE_SECONDS = 90


def log(message):
    print(f'[{time.time():.6f}][tid {os.getpid()}][I][recording_preload 0] {message}', flush=True)


def discover_tools(deadline):
    """Ask the actual tool/wrapper's loader, preserving its library environment."""
    paths = set()
    for tool in ('ffmpeg', 'ffprobe'):
        executable = shutil.which(tool)
        if not executable:
            raise RuntimeError(f'{tool} not found')
        paths.add(Path(executable).resolve())
        env = dict(os.environ, LD_DEBUG='files', LC_ALL='C')
        env.pop('LD_DEBUG_OUTPUT', None)
        result = subprocess.run([executable, '-version'], env=env,
                                stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                                text=True, timeout=max(.01, deadline - time.monotonic()),
                                check=True)
        libraries = set()
        for match in re.finditer(r'(?:calling init:|initialize program:)\s*(\S+)', result.stderr):
            name = match.group(1)
            path = name if name.startswith('/') else shutil.which(name)
            if path:
                libraries.add(Path(path).resolve())
        if not libraries:
            raise RuntimeError(f'{tool}: loader did not report dependencies')
        paths.update(libraries)
    # Include acquisition executables and their linked dependencies too. Their
    # startup should not compete for cold file I/O with the video tools.
    root = Path(__file__).resolve().parent.parent
    for name in ('UgripperRuntime', 'CameraRecorder', 'SensorRecorder'):
        executable = root / 'bin' / name / name
        if not executable.is_file():
            raise RuntimeError(f'missing acquisition executable: {executable}')
        paths.add(executable.resolve())
        result = subprocess.run(['ldd', str(executable)], capture_output=True, text=True,
                                timeout=max(.01, deadline - time.monotonic()), check=True)
        if 'not found' in result.stdout:
            raise RuntimeError(f'unresolved dependencies: {executable}')
        for match in re.finditer(r'(?:=>\s+|^\s*)(/\S+)', result.stdout, re.MULTILINE):
            paths.add(Path(match.group(1)).resolve())
    return sorted(paths)


class LockedFiles:
    def __init__(self):
        self.maps = []
        self.bytes = 0
        self.libc = ctypes.CDLL(None, use_errno=True)
        self.libc.mlock.argtypes = (ctypes.c_void_p, ctypes.c_size_t)
        self.libc.mlock.restype = ctypes.c_int
        self.libc.mmap.argtypes = (ctypes.c_void_p, ctypes.c_size_t, ctypes.c_int,
                                  ctypes.c_int, ctypes.c_int, ctypes.c_long)
        self.libc.mmap.restype = ctypes.c_void_p
        self.libc.munmap.argtypes = (ctypes.c_void_p, ctypes.c_size_t)
        self.libc.munmap.restype = ctypes.c_int

    def acquire(self, paths):
        # Deduplicate hard links as well as symlinks, and account page rounding.
        files = {}
        for path in paths:
            stat = path.stat()
            if stat.st_size:
                files.setdefault((stat.st_dev, stat.st_ino), (path, stat.st_size))
        needed = sum((size + mmap.PAGESIZE - 1) // mmap.PAGESIZE * mmap.PAGESIZE
                     for _, size in files.values())
        limit = resource.getrlimit(resource.RLIMIT_MEMLOCK)[0]
        if needed > MAX_BYTES or (limit != resource.RLIM_INFINITY and needed > limit):
            raise RuntimeError(f'locked file budget exceeded: bytes={needed} limit={limit} max={MAX_BYTES}')
        available = int(re.search(r'^MemAvailable:\s+(\d+)',
                                  Path('/proc/meminfo').read_text(), re.MULTILINE)[1]) * 1024
        if available < needed + RESERVE_BYTES:
            raise RuntimeError(f'insufficient memory: available={available} lock={needed} reserve={RESERVE_BYTES}')
        try:
            for path, size in files.values():
                with path.open('rb') as source:
                    address = self.libc.mmap(None, size, mmap.PROT_READ,
                                             mmap.MAP_SHARED, source.fileno(), 0)
                if address == ctypes.c_void_p(-1).value:
                    raise OSError(ctypes.get_errno(), f'mmap failed: {path}')
                # A read-only shared mapping is essential: mlock on writable
                # MAP_PRIVATE can populate private COW pages, leaving the actual
                # executable's file cache reclaimable while wasting memory.
                if self.libc.mlock(address, size) != 0:
                    error = ctypes.get_errno()
                    self.libc.munmap(address, size)
                    raise OSError(error, f'mlock failed: {path}')
                self.maps.append((address, size))
            self.bytes = needed
        except BaseException:
            self.close()
            raise

    def close(self):
        for address, size in reversed(self.maps):
            self.libc.munmap(address, size)  # also releases the lock
        self.maps.clear()
        self.bytes = 0


def main():
    if len(sys.argv) < 3 or sys.argv[1] != '--':
        raise SystemExit('usage: recording_preload.py -- command [args ...]')
    locked = LockedFiles()
    child = None

    def stop(signum, _frame):
        if child is not None and child.poll() is None:
            os.killpg(child.pid, signum)
        else:
            raise SystemExit(128 + signum)

    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)
    started = time.monotonic()
    try:
        # Bound preparation including filesystem faults in mlock, not only tools.
        def expired(_signum, _frame):
            raise TimeoutError('recording preload exceeded preparation deadline')
        signal.signal(signal.SIGALRM, expired)
        signal.alarm(PREPARE_SECONDS)
        log('preparation begin')
        paths = discover_tools(started + PREPARE_SECONDS)
        locked.acquire(paths)
        signal.alarm(0)
        log(f'ready files={len(locked.maps)} locked_bytes={locked.bytes} elapsed_ms={int((time.monotonic()-started)*1000)}')
        child = subprocess.Popen(sys.argv[2:], start_new_session=True)
        return child.wait()
    except Exception as exc:
        print(f'[{time.time():.6f}][tid {os.getpid()}][E][recording_preload 0] '
              f'preparation failed: {exc} error_code=UGR-70-01', file=sys.stderr, flush=True)
        return 1
    finally:
        signal.alarm(0)
        locked.close()


if __name__ == '__main__':
    sys.exit(main())
