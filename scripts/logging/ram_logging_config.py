#!/usr/bin/python3
"""Root-only logging configuration transactions; no test switches in the CLI.

Every file publication is fsync + same-directory replace + directory fsync.
A durable undo record covers files, group grants and previously active services.
Multi-file changes are recoverable transactions, not one filesystem operation.
"""
import base64
import configparser
import fcntl
import json
import os
from pathlib import Path
import signal
import stat
import subprocess
import sys
import tempfile

BASE = '/usr/local/lib/ugripper/logging'
STATE = '/var/lib/ugripper/logging-config'
JOURNAL = '/etc/systemd/journald.conf.d/90-ugripper-volatile.conf'
RSYSLOG = '/etc/ugripper/rsyslog-ram.conf'
SUDOERS = '/etc/sudoers.d/ugripper-journal-snapshot'
RECOVERY = '/etc/systemd/system/ugripper-log-config-recovery.service'
RECOVERY_LINK = '/etc/systemd/system/multi-user.target.wants/ugripper-log-config-recovery.service'
LEGACY_UNITS = ('log-persist.timer', 'log-migrate.timer', 'log-persist.service', 'log-migrate.service')
LOG_UNITS = ('systemd-journald.service', 'rsyslog.service')
JOURNAL_VALUES = {'Storage': 'volatile', 'RuntimeMaxUse': '64M',
                  'RuntimeMaxFileSize': '8M', 'RuntimeKeepFree': '64M',
                  'MaxRetentionSec': '30day', 'ForwardToSyslog': 'yes'}


def file_data(value, mode=0o644):
    return {'kind': 'file', 'data': base64.b64encode(value.encode()).decode(),
            'mode': mode, 'uid': os.geteuid(), 'gid': os.getegid()}


def desired(user):
    result = {
        JOURNAL: file_data('[Journal]\n' + ''.join(f'{k}={v}\n' for k, v in JOURNAL_VALUES.items())),
        RSYSLOG: file_data('''# Managed bounded RAM output; excludes system rsyslog.d rules.
module(load="imuxsock")
module(load="imklog")
global(maxMessageSize="64k" workDirectory="/tmp/logbuf")
$PrivDropToUser syslog
$PrivDropToGroup adm
$outchannel ugripper_ram,/tmp/logbuf/syslog.log,5242880,/usr/local/lib/ugripper/logging/rotate_syslog.py
*.* :omfile:$ugripper_ram
'''),
        '/etc/systemd/system/rsyslog.service.d/90-ugripper-ram.conf': file_data('''[Service]
ExecStart=
ExecStart=/usr/sbin/rsyslogd -n -iNONE -f /etc/ugripper/rsyslog-ram.conf
'''),
        '/etc/tmpfiles.d/ugripper-ram-logs.conf': file_data('d /tmp/logbuf 0750 syslog adm -\n'),
        SUDOERS: file_data(f'{user} ALL=(root) NOPASSWD: {BASE}/rotate_journal.sh ""\n', 0o440),
        RECOVERY: file_data(f'''[Unit]
Description=Recover interrupted UGripper logging configuration
After=local-fs.target
Before=rsyslog.service ugripper.service
ConditionPathExists={STATE}/pending.json
[Service]
Type=oneshot
ExecStart=/usr/bin/python3 {STATE}/recover.py recover
[Install]
WantedBy=multi-user.target
'''),
        RECOVERY_LINK: {'kind': 'symlink', 'target': RECOVERY},
    }
    for name in LEGACY_UNITS:
        result['/etc/systemd/system/' + name] = {'kind': 'symlink', 'target': '/dev/null'}
    for path in ('/etc/systemd/journald.conf.d/persistent.conf',
                 '/etc/rsyslog.d/30-persistent.conf', '/etc/logrotate.d/persistent-syslog',
                 '/etc/tmpfiles.d/logbuf.conf'):
        result[path] = {'kind': 'absent'}
    return result


class Configuration:
    def __init__(self, root=Path('/'), runner=None):
        self.root = Path(root)
        self.state = self.root / STATE.lstrip('/')
        self.runner = runner or self.command

    def path(self, name):
        return self.root / name.lstrip('/')

    def command(self, args, check=True):
        completed = subprocess.run(args, text=True, capture_output=True, timeout=35)
        if check and completed.returncode:
            raise RuntimeError(f'{args[0]} failed ({completed.returncode}): {completed.stderr[-2000:]}')
        return completed

    def run(self, *args, check=True):
        return self.runner(list(args), check=check)

    @staticmethod
    def flush_dir(path):
        fd = os.open(path, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(fd)
        finally:
            os.close(fd)

    def parents(self, path):
        # Do not follow a parent symlink into an unexpected configuration tree.
        relative = path.parent.relative_to(self.root)
        parent = self.root
        for part in relative.parts:
            parent = parent / part
            if parent.is_symlink():
                raise RuntimeError('symlink configuration parent: ' + str(parent))
            if not parent.exists():
                parent.mkdir(mode=0o755)
                self.flush_dir(parent.parent)
            if not parent.is_dir():
                raise RuntimeError('not a configuration directory: ' + str(parent))

    def read(self, name):
        path = self.path(name)
        if path.is_symlink():
            return {'kind': 'symlink', 'target': os.readlink(path)}
        if not path.exists():
            return {'kind': 'absent'}
        st = path.stat()
        if not stat.S_ISREG(st.st_mode):
            raise RuntimeError('not a regular configuration file: ' + str(path))
        return {'kind': 'file', 'data': base64.b64encode(path.read_bytes()).decode(),
                'mode': stat.S_IMODE(st.st_mode), 'uid': st.st_uid, 'gid': st.st_gid}

    def publish(self, name, value):
        path = self.path(name)
        self.parents(path)
        if self.read(name) == value:
            return
        if value['kind'] == 'absent':
            path.unlink(missing_ok=True)
            self.flush_dir(path.parent)
            return
        fd, temp = tempfile.mkstemp(prefix='.ugripper-config-', dir=path.parent)
        try:
            if value['kind'] == 'file':
                with os.fdopen(fd, 'wb') as out:
                    fd = -1
                    out.write(base64.b64decode(value['data']))
                    if os.geteuid() == 0:
                        os.fchown(out.fileno(), value['uid'], value['gid'])
                    os.fchmod(out.fileno(), value['mode'])
                    out.flush()
                    os.fsync(out.fileno())
            else:
                os.close(fd); fd = -1
                os.unlink(temp)
                os.symlink(value['target'], temp)
            os.replace(temp, path)
            self.flush_dir(path.parent)
        finally:
            if fd >= 0:
                os.close(fd)
            if os.path.lexists(temp):
                os.unlink(temp)

    def save(self, name, value):
        self.publish(STATE + '/' + name, file_data(json.dumps(value, sort_keys=True) + '\n', 0o600))

    def load(self, name):
        path = self.state / name
        current = self.root
        for part in path.relative_to(self.root).parts:
            current = current / part
            if current.is_symlink():
                raise RuntimeError('symlink configuration state path rejected')
            if not current.exists():
                return None
            metadata = current.stat()
            if metadata.st_uid != os.geteuid() or metadata.st_mode & 0o022:
                raise RuntimeError('configuration state must be owned and writable only by root')
        return json.loads(path.read_text())

    def groups(self, user):
        return set(self.run('/usr/bin/id', '-nG', user).stdout.split())

    def active_units(self):
        return [unit for unit in (*LEGACY_UNITS, *LOG_UNITS)
                if self.run('/usr/bin/systemctl', 'is-active', '--quiet', unit, check=False).returncode == 0]

    def preflight(self, user):
        if not user or any(c not in 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-' for c in user):
            raise ValueError('invalid service user')
        self.run('/usr/bin/id', user)
        for group in ('adm', 'systemd-journal', 'syslog'):
            self.run('/usr/bin/getent', 'group', group)
        for directory in ('/run/log/journal', '/tmp/logbuf'):
            if self.path(directory).is_symlink():
                raise RuntimeError('unsafe RAM log symlink: ' + directory)
        for target in ('/tmp', '/run/log'):
            fs = self.run('/usr/bin/findmnt', '-rn', '-o', 'FSTYPE', '--target', target).stdout.strip()
            if fs != 'tmpfs':
                raise RuntimeError(target + ' must reside on tmpfs')
        for helper in ('rotate_journal.sh', 'rotate_syslog.py'):
            self.run('/usr/bin/test', '-x', BASE + '/' + helper)
        # RAM directory creation is not a persistent configuration change.
        self.run('/usr/bin/install', '-d', '-m', '0750', '-o', 'syslog', '-g', 'adm', '/tmp/logbuf')
        # Validate the exact bytes before any live configuration is changed.
        with tempfile.TemporaryDirectory(prefix='ugripper-config-check-') as temp:
            for key, tool, flags in ((RSYSLOG, '/usr/sbin/rsyslogd', ['-N1', '-f']),
                                     (SUDOERS, '/usr/sbin/visudo', ['-cf'])):
                candidate = Path(temp) / Path(key).name
                candidate.write_bytes(base64.b64decode(desired(user)[key]['data']))
                candidate.chmod(0o440 if key == SUDOERS else 0o644)
                self.run(tool, *flags, str(candidate))

    def restart_and_check(self, active=None):
        self.run('/usr/bin/systemctl', 'daemon-reload')
        for unit in LOG_UNITS if active is None else [u for u in LOG_UNITS if u in active]:
            self.run('/usr/bin/systemctl', 'restart', unit)
            self.run('/usr/bin/systemctl', 'is-active', '--quiet', unit)

    def verify(self, user):
        text = self.run('/usr/bin/systemd-analyze', 'cat-config', 'systemd/journald.conf').stdout
        parser = configparser.ConfigParser(strict=False, interpolation=None)
        parser.optionxform = str
        parser.read_string(text)
        if any(parser.get('Journal', key, fallback='') != value for key, value in JOURNAL_VALUES.items()):
            raise RuntimeError('effective journald settings overridden by another configuration')
        command = self.run('/usr/bin/systemctl', 'show', 'rsyslog.service', '-p', 'ExecStart', '--value').stdout
        if '-f /etc/ugripper/rsyslog-ram.conf' not in command:
            raise RuntimeError('effective rsyslog command overridden')
        self.run('/usr/sbin/rsyslogd', '-N1', '-f', RSYSLOG)
        self.run('/usr/sbin/visudo', '-cf', SUDOERS)
        if not {'adm', 'systemd-journal'} <= self.groups(user):
            raise RuntimeError('log reader group grant failed')
        self.run('/usr/bin/test', '-f', '/tmp/logbuf/syslog.log')
        self.run('/usr/bin/sudo', '-n', '-u', user, '/usr/bin/sudo', '-n', BASE + '/rotate_journal.sh')

    def restore_pending(self, boot=False):
        pending = self.load('pending.json')
        if not pending:
            return
        guard_paths = (RECOVERY, RECOVERY_LINK)
        for name, value in reversed(list(pending['before'].items())):
            if name not in guard_paths:
                self.publish(name, value)
        current = self.groups(pending['user'])
        wanted = set(pending.get('groups_before', current - set(pending['added_groups'])))
        missing = (wanted - current) & {'adm', 'systemd-journal'}
        if missing:
            self.run('/usr/sbin/usermod', '-a', '-G', ','.join(sorted(missing)), pending['user'])
        for group in (current - wanted) & {'adm', 'systemd-journal'}:
            self.run('/usr/bin/gpasswd', '-d', pending['user'], group)
        self.run('/usr/bin/systemctl', 'daemon-reload')
        if not boot:
            # Only restore services which actually ran before the attempt.
            for unit in (*LOG_UNITS, *LEGACY_UNITS):
                if unit in pending['active']:
                    self.run('/usr/bin/systemctl', 'restart', unit)
                    self.run('/usr/bin/systemctl', 'is-active', '--quiet', unit)
                elif unit in LOG_UNITS:
                    self.run('/usr/bin/systemctl', 'stop', unit)
        else:
            # This oneshot precedes rsyslog: never synchronously restart it here.
            self.run('/usr/bin/systemctl', 'try-restart', 'systemd-journald.service')
        if 'original_before' in pending:
            self.publish(STATE + '/original.json', pending['original_before'])
        self.publish(STATE + '/pending.json', {'kind': 'absent'})
        # Keep boot recovery armed until all restoration work is durable.
        for name in guard_paths:
            if name in pending['before']:
                self.publish(name, pending['before'][name])

    def install(self, user):
        self.restore_pending()
        self.preflight(user)
        original = self.load('original.json')
        if original and original['user'] != user:
            raise RuntimeError('service user changed; restore previous logging policy first')
        values = desired(user)
        if self.path('/var/log/journal').is_symlink() or (original and '/var/log/journal' in original['before']):
            values['/var/log/journal'] = {'kind': 'absent'}
        if original:
            # Do not silently replace modifications made by another administrator.
            for name, previous in original['applied'].items():
                if self.read(name) != previous:
                    raise RuntimeError('managed configuration changed externally: ' + name)
        before = {name: self.read(name) for name in values}
        existing_groups = self.groups(user)
        pending = {'user': user, 'before': before, 'active': self.active_units(),
                   'added_groups': sorted({'adm', 'systemd-journal'} - existing_groups),
                   'groups_before': sorted(existing_groups),
                   'original_before': self.read(STATE + '/original.json')}
        self.parents(self.state / 'placeholder')
        self.state.chmod(0o700)
        # A root-owned durable recovery program remains usable across dpkg unpack.
        recovery = Path(__file__).read_text()
        self.publish(STATE + '/recover.py', file_data(recovery, 0o700))
        # Arm boot recovery before publishing a live multi-file change.
        try:
            self.publish(RECOVERY, values[RECOVERY])
            self.publish(RECOVERY_LINK, values[RECOVERY_LINK])
            self.run('/usr/bin/systemctl', 'daemon-reload')
            self.save('pending.json', pending)
            for unit in LEGACY_UNITS:
                self.run('/usr/bin/systemctl', 'stop', unit, check=False)
                if self.run('/usr/bin/systemctl', 'is-active', '--quiet', unit, check=False).returncode == 0:
                    raise RuntimeError('legacy log writer did not stop: ' + unit)
            for name, value in values.items():
                self.publish(name, value)
            if pending['added_groups']:
                self.run('/usr/sbin/usermod', '-a', '-G', ','.join(pending['added_groups']), user)
            self.run('/usr/bin/systemd-tmpfiles', '--create', '/etc/tmpfiles.d/ugripper-ram-logs.conf')
            self.restart_and_check()
            self.verify(user)
            if original is None:
                original = dict(pending)
                original['original_active'] = original.pop('active')
                original.pop('original_before')
            else:
                for name in values:
                    original['before'].setdefault(name, before[name])
                original['added_groups'] = sorted(set(original['added_groups']) | set(pending['added_groups']))
            original['applied'] = {name: self.read(name) for name in values}
            self.save('original.json', original)
            self.publish(STATE + '/pending.json', {'kind': 'absent'})
        except BaseException:
            if self.load('pending.json'):
                self.restore_pending()
            else:
                # Failed while arming recovery: no live logging policy changed.
                for name in (RECOVERY_LINK, RECOVERY):
                    self.publish(name, before[name])
                self.run('/usr/bin/systemctl', 'daemon-reload')
            raise
        print('logging configuration committed; services and effective policy verified')

    def uninstall(self):
        self.restore_pending()
        original = self.load('original.json')
        if not original:
            return
        for name, value in original['applied'].items():
            if self.read(name) != value:
                raise RuntimeError('refuse to overwrite external change during removal: ' + name)
        pending = {'user': original['user'],
                   'before': {name: self.read(name) for name in original['applied']},
                   'added_groups': [], 'groups_before': sorted(self.groups(original['user'])),
                   'active': self.active_units(),
                   'original_before': self.read(STATE + '/original.json')}
        self.save('pending.json', pending)
        try:
            for name, value in original['before'].items():
                if name not in (RECOVERY, RECOVERY_LINK):
                    self.publish(name, value)
            current = self.groups(original['user'])
            for group in original['added_groups']:
                if group in current:
                    self.run('/usr/bin/gpasswd', '-d', original['user'], group)
            self.run('/usr/bin/systemctl', 'daemon-reload')
            for unit in (*LOG_UNITS, *LEGACY_UNITS):
                if unit in original['original_active']:
                    self.run('/usr/bin/systemctl', 'restart', unit)
                    self.run('/usr/bin/systemctl', 'is-active', '--quiet', unit)
                elif unit in LOG_UNITS:
                    self.run('/usr/bin/systemctl', 'stop', unit)
            self.publish(STATE + '/original.json', {'kind': 'absent'})
            self.publish(STATE + '/pending.json', {'kind': 'absent'})
        except BaseException:
            self.restore_pending()
            raise
        for name in (RECOVERY, RECOVERY_LINK):
            self.publish(name, original['before'][name])
        print('previous logging configuration restored')


def main():
    if os.geteuid() != 0 or len(sys.argv) not in (2, 3):
        raise SystemExit('root required; expected install USER / uninstall / recover')
    action = sys.argv[1]
    if action not in ('install', 'uninstall', 'recover') or (len(sys.argv) == 3) != (action == 'install'):
        raise SystemExit('expected install USER / uninstall / recover')
    lock = Path('/run/ugripper-logging-config.lock')
    fd = os.open(lock, os.O_CREAT | os.O_RDWR | os.O_NOFOLLOW, 0o600)
    with os.fdopen(fd, 'w') as stream:
        fcntl.flock(stream, fcntl.LOCK_EX)
        for signum in (signal.SIGTERM, signal.SIGINT, signal.SIGHUP):
            signal.signal(signum, lambda *_: (_ for _ in ()).throw(InterruptedError('configuration interrupted')))
        config = Configuration()
        if action == 'install':
            config.install(sys.argv[2])
        elif action == 'uninstall':
            config.uninstall()
        else:
            config.restore_pending(boot=True)


if __name__ == '__main__':
    main()
