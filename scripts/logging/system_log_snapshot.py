#!/usr/bin/python3
"""Event-only immutable syslog snapshots, published by a durable manifest.

No timers, active-file following, whole-log reads, or background disk flushes.
Manifest is the sole commit record; orphan data is removed at the next event.
"""
import fcntl
import hashlib
from contextlib import ExitStack
import json
import os
from pathlib import Path
import re
import stat
import subprocess
import sys
import time
import uuid

MIB = 1024 * 1024


def syncfs(path):
    # sync -f (syncfs), deliberately foreground and without a timeout.
    subprocess.run(['/usr/bin/sync', '-f', str(path)], check=True)


def safe_relative(name):
    return isinstance(name, str) and not name.startswith('/') and all(
        p not in ('', '.', '..') for p in name.split('/'))


def committed_files(manifest):
    names = [j['path'] for j in manifest.get('journals', [])]
    for volume in manifest.get('volumes', []):
        names.extend(volume['files'])
    if not all(safe_relative(n) for n in names):
        raise ValueError('invalid manifest path')
    return names


class Snapshot:
    def __init__(self, disk, source=Path('/var/log/ugripper-syslog'),
                 machine=None, boot=None, total_limit=500*MIB, volume_limit=100*MIB, retention=30*86400,
                 require_mount=True,
                 flush=syncfs):
        self.disk = Path(disk).resolve()
        self.source = Path(source)
        self.machine = machine or Path('/etc/machine-id').read_text().strip()
        self.boot = boot or Path('/proc/sys/kernel/random/boot_id').read_text().strip()
        if not re.fullmatch(r'[a-zA-Z0-9-]+', self.machine + self.boot):
            raise ValueError('invalid machine/boot identity')
        self.root = self.disk / 'system_journal'
        self.total_limit, self.volume_limit, self.retention = total_limit, volume_limit, retention
        self.require_mount, self.flush = require_mount, flush
        if min(total_limit, volume_limit, retention) <= 0:
            raise ValueError('invalid retention')
        self.identity = None
        self.timings = {}

    def check_disk(self):
        if self.require_mount and not os.path.ismount(self.disk):
            raise RuntimeError('data disk is not a mount point')
        st = self.disk.stat()
        # Mount ID detects same-device remounts as well as replacements.
        mount_id = ''
        for line in Path('/proc/self/mountinfo').read_text().splitlines():
            fields = line.split()
            if fields[4] == str(self.disk):
                mount_id = fields[0]
        identity = (st.st_dev, st.st_ino, mount_id)
        if self.identity is not None and self.identity != identity:
            raise RuntimeError('data disk changed during snapshot')
        self.identity = identity

    def write(self, relative, writer, durable=False):
        self.check_disk()
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        # Reject symlinked directories (including an existing system_journal).
        for parent in [path.parent, *path.parent.parents]:
            if parent == self.disk:
                break
            if parent.is_symlink():
                raise RuntimeError('symlink in log destination')
        temp = path.with_name('.snapshot-' + uuid.uuid4().hex)
        with temp.open('xb') as out:
            writer(out)
        if durable:
            self.flush(temp)
        self.check_disk()
        os.replace(temp, path)
        return path

    def sources(self, stack):
        # Pin descriptors and finite lengths under the local rotation lock. No USB
        # IO holds this lock, so the logging producer is never blocked by disk IO.
        # Before the first rotation there may not be a lock file yet; retry if
        # it appears during discovery. Missing source paths are rotation races.
        for attempt in range(4):
            attempt_stack = ExitStack()
            try:
                rotation = self.source / '.rotate.lock'
                lock = attempt_stack.enter_context(rotation.open('rb')) if rotation.exists() else None
                if lock:
                    fcntl.flock(lock, fcntl.LOCK_EX)
                paths = sorted(self.source.glob('syslog.*.log')) + [self.source/'syslog.log']
                result, seen = [], set()
                for path in paths:
                    try:
                        fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW)
                    except FileNotFoundError:
                        # rsyslog reopens the active path after its rotation hook
                        # releases the lock; all existing bytes are in archives.
                        if lock and path.name == 'syslog.log':
                            continue
                        raise
                    stream = attempt_stack.enter_context(os.fdopen(fd, 'rb'))
                    st = os.fstat(stream.fileno())
                    if not stat.S_ISREG(st.st_mode):
                        raise RuntimeError('nonregular local syslog source')
                    key = f'{self.machine}:{st.st_dev}:{st.st_ino}'
                    if key not in seen:
                        result.append((stream, st.st_size, key))
                        seen.add(key)
                if not result:
                    raise FileNotFoundError('no local syslog source')
                counter = self.source/'dropped_bytes'
                evicted = int(counter.read_text()) if counter.exists() else 0
                if not lock and rotation.exists():
                    raise FileNotFoundError('first rotation raced discovery')
                if lock:
                    fcntl.flock(lock, fcntl.LOCK_UN)
                stack.enter_context(attempt_stack.pop_all())
                return result, evicted
            except FileNotFoundError:
                if attempt == 3:
                    raise RuntimeError('local syslog unavailable or rotating continuously')
            finally:
                attempt_stack.close()

    def syslogs(self, manifest):
        volumes = manifest.setdefault('volumes', [])
        now = time.time()
        expired = [v for v in volumes if now-v['created'] > self.retention]
        manifest['retention_discarded_bytes'] = manifest.get('retention_discarded_bytes', 0) + sum(v['bytes'] for v in expired)
        volumes = [v for v in volumes if now-v['created'] <= self.retention]
        old_offsets = manifest.get('offsets', {})
        offsets = {}
        copied = 0
        with ExitStack() as stack:
            sources, evicted = self.sources(stack)
            manifest['ram_evicted_bytes'] = evicted
            for stream, size, key in sources:
                prior = old_offsets.get(key, {})
                position = prior.get('position', 0)
                anchor_len = prior.get('anchor_len', 0)
                stream.seek(0)
                anchor_matches = hashlib.sha256(stream.read(anchor_len)).hexdigest() == prior.get('anchor')
                if position > size or (position and not anchor_matches):
                    manifest['source_truncations'] = manifest.get('source_truncations', 0) + 1
                    position = 0
                stream.seek(position)
                while position < size:
                    # readline is capped by both the snapshot and a bounded line
                    # buffer. A partial trailing line is retried at the next event.
                    line = stream.readline(min(128*1024+1, size-position))
                    if not line:
                        raise RuntimeError('snapshot source shrank before boundary')
                    if len(line) > 128*1024:
                        raise RuntimeError('syslog line exceeds 128KiB bound')
                    if not line.endswith(b'\n'):
                        if stream.tell() < size:
                            raise RuntimeError('snapshot source shrank before boundary')
                        break
                    if len(line) > self.volume_limit:
                        raise RuntimeError('syslog line exceeds volume limit')
                    if (not volumes or volumes[-1]['bytes'] + len(line) > self.volume_limit
                            or len(volumes[-1]['files']) >= 128):
                        volumes.append({'id': uuid.uuid4().hex, 'bytes': 0,
                                        'files': [], 'created': time.time()})
                    volume = volumes[-1]
                    budget = self.volume_limit-volume['bytes']
                    relative = 'rsyslog/syslog.'+volume['id']+'.'+uuid.uuid4().hex+'.log'
                    written = 0
                    def writer(out):
                        nonlocal position, written, line
                        while True:
                            out.write(line)
                            written += len(line)
                            position += len(line)
                            if position == size:
                                break
                            line = stream.readline(min(128*1024+1, size-position))
                            if not line:
                                raise RuntimeError('snapshot source shrank before boundary')
                            if len(line) > 128*1024:
                                raise RuntimeError('syslog line exceeds 128KiB bound')
                            if not line.endswith(b'\n'):
                                if stream.tell() < size:
                                    raise RuntimeError('snapshot source shrank before boundary')
                                break
                            if written + len(line) > budget:
                                stream.seek(position)
                                break
                    self.write(relative, writer)
                    volume['files'].append(relative)
                    volume['bytes'] += written
                    copied += written
                    stream.seek(position)
                # A short prefix guards against inode reuse/truncation. Its
                # length stays fixed on subsequent growth of this source file.
                anchor_len = min(position, 256)
                stream.seek(0)
                offsets[key] = {'position': position, 'anchor_len': anchor_len,
                                'anchor': hashlib.sha256(stream.read(anchor_len)).hexdigest()}
        now = time.time()
        volumes = [v for v in volumes if now-v['created'] <= self.retention]
        total = sum(v['bytes'] for v in volumes)
        while volumes and (total > self.total_limit or len(volumes) > 8):
            removed = volumes.pop(0)
            total -= removed['bytes']
            manifest['retention_discarded_bytes'] = manifest.get('retention_discarded_bytes', 0) + removed['bytes']
        manifest['volumes'], manifest['offsets'] = volumes, offsets
        manifest['copied_bytes'] = copied

    def cleanup(self, keep):
        # Only this implementation's known files; never recurse into symlinks.
        machine_dirs = [p for p in self.root.iterdir() if re.fullmatch(r'[a-f0-9]{32}', p.name)]
        for directory in [self.root, self.root / self.machine, self.root / 'rsyslog', *machine_dirs]:
            if not directory.is_dir() or directory.is_symlink():
                continue
            for path in directory.iterdir():
                if path.is_symlink() or not path.is_file():
                    continue
                rel = str(path.relative_to(self.root))
                if (path.name.startswith('.snapshot-') or path.suffix in ('.journal', '.log', '.gz')) and rel not in keep:
                    path.unlink()

    def run(self):
        started = time.monotonic()
        self.check_disk()
        if self.root.is_symlink():
            raise RuntimeError('symlink destination rejected')
        self.root.mkdir(exist_ok=True)
        manifest_path = self.root / 'manifest.json'
        if manifest_path.is_symlink():
            raise RuntimeError('symlink manifest rejected')
        manifest = json.loads(manifest_path.read_text()) if manifest_path.exists() else {'schema': 1}
        if manifest.get('schema') != 1:
            raise ValueError('unsupported snapshot schema')
        keep = set(committed_files(manifest))
        for name in keep:
            if not (self.root / name).is_file():
                raise RuntimeError('committed log fragment missing: ' + name)
        self.cleanup(keep)
        stage_start = time.monotonic()
        self.syslogs(manifest)
        self.timings['syslog_copy_ms'] = (time.monotonic()-stage_start)*1000
        # Retire the previous experimental format only after a durable commit.
        manifest.pop('journals', None)
        manifest.pop('journal_seen', None)
        manifest['format'] = 'syslog-only'
        manifest.update(machine=self.machine, boot=self.boot, committed_at=time.time(),
                        disk_identity=list(self.identity))
        stage_start = time.monotonic()
        # syncfs flushes all new data before publishing any offset/retention state.
        self.flush(self.root)
        self.write('manifest.json', lambda out: out.write((json.dumps(manifest, sort_keys=True) + '\n').encode()), durable=True)
        self.flush(self.root)
        self.cleanup(set(committed_files(manifest)))
        self.check_disk()
        self.flush(self.root)  # retention/deletes also precede Ready
        self.timings['commit_sync_prune_ms'] = (time.monotonic()-stage_start)*1000
        print('system-log: phases_ms=' + json.dumps(self.timings, sort_keys=True), flush=True)
        print(f'system-log: snapshot committed elapsed_ms={(time.monotonic()-started)*1000:.1f} '
              f'copied_bytes={manifest["copied_bytes"]} format=syslog-only', flush=True)
        return manifest


def run_locked(disk, lock_path='/tmp/ugripper-system-log.lock', **kwargs):
    # One global RAM lock, independent of serial and output disk selection.
    fd = os.open(lock_path, os.O_CREAT | os.O_RDWR | os.O_NOFOLLOW, 0o600)
    with os.fdopen(fd, 'a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        return Snapshot(disk, **kwargs).run()


def serve(control_fd):
    # Same inherited SOCK_SEQPACKET transport as the Ego warm worker. Idle
    # requests touch RAM only; there is no periodic disk access or timer.
    import socket
    with socket.socket(fileno=control_fd) as control:
        control.send(json.dumps({'ready': True}).encode())
        while True:
            packet = control.recv(65536)
            if not packet:
                return
            try:
                request = json.loads(packet)
                result = run_locked(request['disk'])
                reply = {'ok': True, 'copied_bytes': result['copied_bytes']}
            except Exception as exc:
                print(f'system-log: snapshot failed: {exc}', file=sys.stderr, flush=True)
                reply = {'ok': False, 'error': str(exc)}
            control.send(json.dumps(reply).encode())


def main():
    if len(sys.argv) == 3 and sys.argv[1] == '--serve-fd':
        serve(int(sys.argv[2]))
    elif len(sys.argv) == 2:
        run_locked(sys.argv[1])
    else:
        raise ValueError('expected mounted data disk root or inherited service fd')


if __name__ == '__main__':
    try:
        main()
    except Exception as exc:
        print(f'system-log: snapshot failed: {exc}', file=sys.stderr, flush=True)
        sys.exit(1)
