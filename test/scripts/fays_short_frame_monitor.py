#!/usr/bin/env python3
"""Read-only Fays USB frame monitor. No service/config/recording changes."""
import argparse
import collections
import json
import os
from pathlib import Path
import signal
import threading
import time

MAGIC = bytes.fromhex('cefabebacefabeba')


class Analyzer:
    def __init__(self, expected):
        self.expected = expected
        self.current = {}
        self.counts = collections.defaultdict(collections.Counter)
        self.bad = []

    def feed(self, row):
        f = row['line'].split()
        if len(f) < 6 or f[2] != 'C' or not f[3].startswith('Bi:'):
            return
        key = f[3]
        length = int(f[5])
        raw = bytes.fromhex(''.join(f[7:])) if len(f) > 7 and f[6] == '=' else b''
        header = len(raw) >= 12 and raw[0] == 12 and raw[4:12] == MAGIC
        if header:
            prev = self.current.get(key)
            if prev:
                self.counts[key]['frames'] += 1
                self.counts[key][str(prev['bytes'])] += 1
                if prev['bytes'] != self.expected or prev['usb_errors'] or prev['flags'] & 64:
                    self.counts[key]['anomalies'] += 1
                    if len(self.bad) < 1000:
                        self.bad.append(dict(endpoint=key, **prev))
            self.current[key] = dict(wall=row['wall'], kernel_us=int(f[1]), bytes=0,
                                     flags=raw[1], usb_errors=0)
        if key in self.current:
            self.current[key]['bytes'] += length
            self.current[key]['usb_errors'] += int(f[4] != '0')

    def report(self):
        # First partial frame and last unclosed frame are deliberately excluded.
        return dict(expected_bytes=self.expected, endpoints=dict(self.counts),
                    anomalies=self.bad, anomaly_details_limit=1000)


def discover():
    targets = []
    for alias in ('stereo_left', 'stereo_right'):
        video = Path('/dev', alias).resolve()
        device = Path('/sys/class/video4linux', video.name, 'device').resolve()
        for parent in (device, *device.parents):
            if (parent / 'busnum').exists() and (parent / 'devnum').exists():
                targets.append((int((parent / 'busnum').read_text()),
                                int((parent / 'devnum').read_text()), 1))
                break
        else:
            raise RuntimeError('Cannot discover /dev/' + alias + '; use --target BUS:DEV:EP')
    return targets


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--duration', type=int, default=180, help='monitor seconds (1..1800)')
    p.add_argument('--target', action='append', help='bulk IN BUS:DEV:EP; repeat for both cameras')
    p.add_argument('--expected-bytes', type=int, default=513292, help='Fays 640x800 incl. metadata/UVC header')
    p.add_argument('--output', help='new output directory; never overwrite')
    p.add_argument('--analyze', help='offline analysis of previous usb.json or raw.jsonl')
    args = p.parse_args()
    analyzer = Analyzer(args.expected_bytes)
    if args.analyze:
        with open(args.analyze) as source:
            if args.analyze.endswith('.jsonl'):
                for line in source:
                    analyzer.feed(json.loads(line))
            else:
                for row in json.load(source)['rows']:
                    analyzer.feed(row)
        print(json.dumps(analyzer.report(), indent=2))
        return
    if not 1 <= args.duration <= 1800:
        p.error('--duration must be 1..1800')
    targets = [tuple(map(int, s.split(':'))) for s in args.target] if args.target else discover()
    if any(len(t) != 3 for t in targets):
        p.error('--target must be BUS:DEV:EP')
    buses = sorted({t[0] for t in targets})
    allowed = set(targets)
    out = Path(args.output or ('/tmp/fays-short-' + time.strftime('%Y%m%d-%H%M%S')))
    out.mkdir(parents=True, exist_ok=False)
    stop = threading.Event()
    lock = threading.Lock()
    errors = []
    byte_count = 0
    files = {}
    def stats():
        return {str(b): Path('/sys/kernel/debug/usb/usbmon/%ss' % b).read_text().strip() for b in buses}
    # Fail before saying ready if usbmon cannot be opened.
    try:
        for bus in buses:
            files[bus] = os.open('/sys/kernel/debug/usb/usbmon/%su' % bus, os.O_RDONLY)
        before = stats()
    except Exception:
        for fd in files.values():
            os.close(fd)
        raise
    raw_file = (out / 'raw.jsonl').open('x')
    def reader(bus, fd):
        nonlocal byte_count
        try:
            while not stop.is_set():
                data = os.read(fd, 65536).decode('ascii')
                if not data:
                    raise RuntimeError('usbmon EOF')
                for line in data.splitlines():
                    f = line.split()
                    if len(f) < 6 or not f[3].startswith('Bi:'):
                        continue
                    if tuple(map(int, f[3].split(':')[1:])) not in allowed:
                        continue
                    row = dict(wall=time.time(), bus=bus, line=line)
                    encoded = json.dumps(row) + '\n'
                    with lock:
                        if stop.is_set():
                            return
                        byte_count += len(encoded)
                        if byte_count > 256 * 1024 * 1024:
                            raise RuntimeError('256 MiB capture limit reached; shorten duration')
                        raw_file.write(encoded)
                        analyzer.feed(row)
        except Exception as exc:
            with lock:
                errors.append(str(exc))
            stop.set()
    signal.signal(signal.SIGINT, lambda *_: stop.set())
    signal.signal(signal.SIGTERM, lambda *_: stop.set())
    threads = [threading.Thread(target=reader, args=(b, fd), daemon=True) for b, fd in files.items()]
    for thread in threads:
        thread.start()
    print('READY targets=%s duration=%ss; start/stop recording normally' % (targets, args.duration), flush=True)
    stop.wait(args.duration)
    stop.set()
    with lock:
        raw_file.close()
        report = analyzer.report()
        after = stats()
        for bus in before:
            def lost(value):
                fields = value.split()
                return int(fields[fields.index('text_lost') + 1])
            if lost(after[bus]) != lost(before[bus]):
                errors.append('usbmon text_lost changed on bus ' + bus)
        seen = {tuple(map(int, key.split(':')[1:])) for key in report['endpoints']}
        if allowed - seen:
            errors.append('No closed frames for targets: ' + str(sorted(allowed - seen)))
        report.update(errors=errors, usbmon_before=before, usbmon_after=after, targets=targets,
                      capture_valid=not errors)
        (out / 'summary.json').write_text(json.dumps(report, indent=2) + '\n')
    for key, counts in report['endpoints'].items():
        print('%s frames=%d anomalies=%d' % (key, counts['frames'], counts['anomalies']))
    print('Evidence: %s; verify text_lost unchanged in summary.json' % out)
    if errors or not report['endpoints']:
        raise SystemExit('INCOMPLETE: capture errors or no closed Fays frames; inspect summary.json')


if __name__ == '__main__':
    main()
