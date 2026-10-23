#!/usr/bin/env python3
"""Apply a conservative RK3588 USB IRQ fallback; preserve firmware policies."""
import fcntl
import json
import os
from pathlib import Path
import re
import subprocess
import sys

from uvc_workqueue_affinity import cpu_list, mask_value, select_big_cores


def usb_irqs(proc):
    rows = {}
    for line in (proc / 'interrupts').read_text().splitlines():
        fields = line.split()
        if not fields or not re.fullmatch(r'\d+:', fields[0]):
            continue
        index = 1
        counts = []
        while index < len(fields) and fields[index].isdigit():
            counts.append(int(fields[index])); index += 1
        label = ' '.join(fields[index:])
        if not re.search(r'\b(?:xhci|ehci|ohci|dwc3)', label, re.I):
            continue
        # Unused PCI MSI vectors are not the active controller IRQ.
        if 'ITS-MSI' in label and not sum(counts):
            continue
        rows[int(fields[0][:-1])] = label
    return rows


def ordered_usb_irqs(sys_root, rows):
    # Match usb-irq.sh: bus order, deduplicate shared USB2/USB3 controller IRQs.
    ordered = []
    buses = [p for p in (sys_root / 'bus/usb/devices').glob('usb*') if p.name[3:].isdigit()]
    for bus in sorted(buses, key=lambda p: int(p.name[3:])):
        found = []
        for parent in bus.resolve().parents:
            if re.fullmatch(r'[0-9a-fA-F]{4}:[0-9a-fA-F]{2}:[0-9a-fA-F]{2}\.[0-9]', parent.name):
                found = [int(p.name) for p in (parent / 'msi_irqs').glob('*') if p.name.isdigit()]
                irq_path = parent / 'irq'
                if irq_path.exists():
                    found.append(int(irq_path.read_text().strip()))
                break
        if not found:
            found = [irq for irq, label in rows.items() if re.search(r':' + bus.name + r'\b', label)]
        for irq in sorted(set(found)):
            if irq in rows and irq not in ordered:
                ordered.append(irq)
    ordered.extend(irq for irq in sorted(rows) if irq not in ordered)
    return ordered


def plan(sys_root, proc):
    online = cpu_list((sys_root / 'devices/system/cpu/online').read_text())
    all_mask = sum(1 << cpu for cpu in online)
    big_mask = select_big_cores(sys_root, all_mask)
    if big_mask == all_mask:
        return []
    big = [cpu for cpu in sorted(online) if big_mask & (1 << cpu)]
    default = mask_value((proc / 'irq/default_smp_affinity').read_text()) & all_mask
    default_cpus = {cpu for cpu in online if default & (1 << cpu)}
    changes = []
    rows = usb_irqs(proc)
    ordered = ordered_usb_irqs(sys_root, rows)
    base, extra = divmod(len(ordered), len(big))
    assignments = [cpu for index, cpu in enumerate(big) for _ in range(base + (index < extra))]
    for irq, cpu in zip(ordered, assignments):
        label = rows[irq]
        path = proc / f'irq/{irq}/smp_affinity_list'
        try:
            previous = path.read_text().strip()
            current = cpu_list(previous)
        except (OSError, ValueError):
            continue
        if current and current <= set(big):
            print(f'USB IRQ {irq}: preserve existing big-core affinity {previous}')
        elif current == default_cpus or current == online:
            changes.append(dict(irq=irq, label=label, previous=previous, applied=str(cpu)))
        else:
            print(f'USB IRQ {irq}: preserve explicit affinity {previous}')
    return changes


def save(state, payload):
    temporary = state.with_suffix('.tmp')
    temporary.write_text(json.dumps(payload))
    temporary.replace(state)


def restore(proc, state):
    if not state.exists():
        return
    data = json.loads(state.read_text())
    boot = (proc / 'sys/kernel/random/boot_id').read_text().strip()
    if data['boot_id'] != boot:
        state.unlink(); return
    labels = usb_irqs(proc)
    remaining = []
    for entry in data['entries']:
        path = proc / f"irq/{entry['irq']}/smp_affinity_list"
        try:
            if labels.get(entry['irq']) == entry['label'] and path.read_text().strip() == entry['applied']:
                path.write_text(entry['previous'] + '\n')
                print(f"USB IRQ {entry['irq']}: restored {entry['previous']}")
            else:
                print(f"USB IRQ {entry['irq']}: preserve external change")
        except FileNotFoundError:
            pass
        except OSError as exc:
            remaining.append(entry)
            print(f"USB IRQ {entry['irq']}: restore failed: {exc}", file=sys.stderr)
    if remaining:
        data['entries'] = remaining; save(state, data)
    else:
        state.unlink()


def apply(sys_root, proc, state, irqbalance_active=False):
    # Repeated starts must not reclaim an externally changed IRQ or lose ownership.
    if state.exists():
        print('USB IRQ affinity: existing session state retained; no reapply')
        return
    changes = plan(sys_root, proc)
    if not changes:
        return
    if irqbalance_active:
        raise RuntimeError('irqbalance is active; USB IRQ fallback skipped to avoid competing policies')
    data = {'boot_id': (proc / 'sys/kernel/random/boot_id').read_text().strip(), 'entries': []}
    for entry in changes:
        data['entries'].append(entry)
        save(state, data)
        path = proc / f"irq/{entry['irq']}/smp_affinity_list"
        try:
            # Do not overwrite a firmware/administrator update since discovery.
            if path.read_text().strip() != entry['previous']:
                data['entries'].pop(); save(state, data); continue
            path.write_text(entry['applied'] + '\n')
            if path.read_text().strip() != entry['applied']:
                raise RuntimeError('kernel affinity readback mismatch')
            print(f"USB IRQ {entry['irq']}: {entry['previous']} -> {entry['applied']}")
        except (OSError, RuntimeError) as exc:
            print(f"USB IRQ {entry['irq']}: {exc}", file=sys.stderr)


def main():
    if len(sys.argv) != 2 or sys.argv[1] not in ('apply', 'restore'):
        raise ValueError('expected apply or restore')
    runtime = Path('/run/ugripper-usb-irq-affinity')
    runtime.mkdir(mode=0o700, exist_ok=True)
    info = runtime.lstat()
    if runtime.is_symlink() or info.st_uid != 0 or info.st_mode & 0o077:
        raise RuntimeError('unsafe IRQ state directory')
    with (runtime / 'lock').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        state = runtime / 'state.json'
        if sys.argv[1] == 'restore':
            restore(Path('/proc'), state); return
        setting = os.environ.get('UGRIPPER_USB_IRQ_BIG_CORES', 'true').strip().lower()
        if setting not in ('true', 'false'):
            raise ValueError('UGRIPPER_USB_IRQ_BIG_CORES must be true or false')
        if setting == 'false':
            restore(Path('/proc'), state); return
        # Also catch manually launched irqbalance outside its systemd unit.
        balance = subprocess.run(['pgrep', '-x', 'irqbalance'], stdout=subprocess.DEVNULL).returncode == 0
        apply(Path('/sys'), Path('/proc'), state, balance)


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, KeyError, RuntimeError) as exc:
        print('USB IRQ affinity: ' + str(exc), file=sys.stderr)
        sys.exit(1)
