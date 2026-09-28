#!/usr/bin/env python3
"""Bound Ego USB time source. Wire format follows vendor time-sync-server.

ACM I/O has its own thread: ADB status queries cannot delay NTP replies.
The APK owns correction policy (currently CLOCK_REALTIME steps, not slew).
"""
import fcntl
from pathlib import Path
import signal
import struct
import threading
import time

import serial
from serial.tools import list_ports
import ego_recording_worker as worker

RUNTIME = Path('/dev/shm/ugripper')
BINDING = RUNTIME / 'ego_binding.json'
STATUS = RUNTIME / 'ego_time_sync.json'
HOST_STATUS = Path('/run/ugripper-time-source.json')
HOST_MAX_AGE_S = 86400
SAMPLE_MAX_AGE_S = 15
BOOT_ID = Path('/proc/sys/kernel/random/boot_id').read_text().strip()
SYNC_PACKAGE = worker.PACKAGE
HEADER = b'\xa0\x01'
NTP_EPOCH = 2208988800


def ntp_now():
    seconds, nanos = divmod(time.time_ns(), 1000000000)
    return ((seconds + NTP_EPOCH) << 32) | ((nanos << 32) // 1000000000)


def response(request, received):
    packet = bytearray(40)
    packet[:4] = HEADER + b'\x02\x00'
    packet[4:16] = request[4:16]  # seq + origin, exact echo
    struct.pack_into('>Q', packet, 16, received)
    struct.pack_into('>Q', packet, 24, ntp_now())
    return packet


def pop_packet(buffer):
    """Retain partial packets; resynchronise malformed/fragmented streams."""
    start = buffer.find(HEADER)
    if start < 0:
        buffer[:] = buffer[-1:] if buffer.endswith(HEADER[:1]) else b''
        return None
    del buffer[:start]
    if len(buffer) < 24:
        return None
    packet = bytes(buffer[:24])
    del buffer[:24]
    return packet


def host_ready(sample, now=None, realtime=None):
    now = time.monotonic() if now is None else now
    realtime = time.time() if realtime is None else realtime
    try:
        elapsed = now - sample['monotonic_s']
        return (sample['boot_id'] == BOOT_ID and sample['status'] == 'ok'
                and 0 <= elapsed <= HOST_MAX_AGE_S
                and abs(realtime - sample['realtime_s'] - elapsed) < .1)
    except (KeyError, TypeError, ValueError):
        return False


def bound_port(identity, ports=None):
    ports = list_ports.comports() if ports is None else ports
    matches = [p.device for p in ports if p.vid == 0x18d1 and p.pid == 0x903b
               and p.serial_number == identity]
    return matches[0] if len(matches) == 1 else None


class Session:
    def __init__(self, identity, port):
        self.identity, self.port = identity, port
        self.stop = threading.Event()
        self.lock = threading.Lock()
        self.state = {'status': 'waiting', 'result_count': 0, 'good_samples': 0,
                      'request_count': 0, 'request_batches': 0}
        self.confirmed_batches = 0
        self.previous_batch = 0
        self.thread = threading.Thread(target=self.run, name='ego-acm', daemon=True)
        self.thread.start()

    def snapshot(self):
        with self.lock:
            return dict(self.state)

    def close(self):
        self.stop.set()
        self.thread.join(timeout=2)

    def run(self):
        try:
            with serial.Serial(self.port, 2000000, timeout=.1, write_timeout=.5,
                               exclusive=True) as stream:
                stream.reset_input_buffer()
                stream.write(b'\xee')
                buffer = bytearray()
                heartbeat = time.monotonic()
                while not self.stop.is_set():
                    buffer.extend(stream.read(max(1, min(stream.in_waiting, 4096))))
                    while (packet := pop_packet(buffer)) is not None:
                        received = ntp_now()
                        kind = packet[2]
                        if kind in (1, 4):
                            data = response(packet, received)
                            if stream.write(data) != len(data):
                                raise RuntimeError('short ACM write')
                            stream.flush()
                            now = time.monotonic()
                            with self.lock:
                                new_batch = now - self.state.get('last_request_monotonic_s', -1e9) > .05
                                self.state.update(
                                    request_count=self.state['request_count'] + 1,
                                    request_batches=self.state['request_batches'] + int(new_batch),
                                    last_request_monotonic_s=now)
                        elif kind == 3:
                            round_number, offset_us, rtt_us = struct.unpack('>iiI', packet[4:16])
                            good = abs(offset_us) <= 5000 and rtt_us <= 10000
                            with self.lock:
                                count = self.state['good_samples']
                                if round_number == 1:
                                    count = 0
                                self.state.update(status='measured', round=round_number,
                                    offset_ms=offset_us / 1000, rtt_ms=rtt_us / 1000,
                                    result_count=self.state['result_count'] + 1,
                                    good_samples=count + 1 if good else 0,
                                    result_monotonic_s=time.monotonic())
                        elif kind == 5:
                            with self.lock:
                                self.state.update(status='waiting', good_samples=0)
                    if time.monotonic() - heartbeat >= 1:
                        stream.write(b'\xee')
                        heartbeat = time.monotonic()
        except Exception as exc:
            with self.lock:
                self.state.update(status='disconnected', error=str(exc), good_samples=0)


def combined_status(session, applied, host, now=None):
    now = time.monotonic() if now is None else now
    fresh = 0 <= now - session.get('result_monotonic_s', -1e9) <= SAMPLE_MAX_AGE_S
    good = (host and fresh and session.get('status') == 'measured'
            and session.get('good_samples', 0) >= 5 and applied.get('status') == 'ok')
    return dict(session, status='ok' if good else 'waiting', mode='usb_acm',
                host_time_ready=host, sample_fresh=fresh, applied=applied)


def main():
    RUNTIME.mkdir(parents=True, exist_ok=True)
    lock = (RUNTIME / 'ego_time_sync.lock').open('a')
    fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    stopping = threading.Event()
    for sig in (signal.SIGTERM, signal.SIGINT):
        signal.signal(sig, lambda *_: stopping.set())
    session = None
    last_identity = ''
    clock_anchor = time.time() - time.monotonic()
    try:
        while not stopping.is_set():
            try:
                binding = worker.read_status(BINDING)
                identity = str(binding.get('serial') or '')
                # Serve the backpack clock even offline or when PTP/NTP owns it.
                host = True
                anchor = time.time() - time.monotonic()
                clock_changed = abs(anchor - clock_anchor) >= .1
                clock_anchor = anchor
                port = bound_port(identity) if identity else None
                if session and (clock_changed or identity != session.identity or port != session.port
                                or not session.thread.is_alive()):
                    session.close()
                    session = None
                state = dict(status='unbound' if not identity else 'disconnected')
                if identity and host and port and session is None:
                    adb = worker.Adb(identity)
                    package = worker.select_app(adb)
                    if package == SYNC_PACKAGE:
                        session = Session(identity, port)
                    else:
                        state.update(status='disconnected', mode='usb_acm')
                if session:
                    applied = worker.read_serial_time_sync(worker.Adb(identity))
                    observed = session.snapshot()
                    # This APK does not emit RESULT packets. Count only new request
                    # batches confirmed by its applied-status endpoint, never repeated polls.
                    if observed['result_count'] == 0:
                        batch = observed['request_batches']
                        if applied.get('status') != 'ok':
                            session.confirmed_batches = 0
                        elif batch > session.previous_batch:
                            session.confirmed_batches += 1
                        session.previous_batch = batch
                        observed.update(status='measured', good_samples=session.confirmed_batches,
                            offset_ms=applied.get('final_offset_ms'), rtt_ms=applied.get('rtt_ms'),
                            result_monotonic_s=observed.get('last_request_monotonic_s', -1e9),
                            sample_source='request_batch_and_apk_status')
                    state = combined_status(observed, applied, host)
                elif identity and binding.get('app', {}).get('package', worker.PACKAGE) == worker.PACKAGE:
                    state.update(status='disconnected', mode='usb_acm')
                # Binding may have changed while ADB waited: never publish old identity as ready.
                if worker.read_status(BINDING).get('serial', '') != identity:
                    if session:
                        session.close()
                        session = None
                    continue
                state.update(serial=identity, package=SYNC_PACKAGE if session else
                             binding.get('app', {}).get('package', ''),
                             clock_source='backpack_system',
                             port=port, boot_id=BOOT_ID, updated_at_ms=worker.now_ms(),
                             updated_monotonic_s=time.monotonic())
                worker.write_json_atomic(STATUS, state)
                label = f"{identity}:{state['status']}"
                if label != last_identity:
                    worker.ugripper_log('INFO', label, component='ego_time_sync')
                    last_identity = label
            except Exception as exc:
                if session:
                    session.close()
                    session = None
                worker.write_json_atomic(STATUS, dict(status='error', error=str(exc),
                    updated_at_ms=worker.now_ms(), updated_monotonic_s=time.monotonic(), boot_id=BOOT_ID))
            stopping.wait(1)
    finally:
        if session:
            session.close()
        worker.write_json_atomic(STATUS, {'status': 'stopped', 'updated_at_ms': worker.now_ms()})
        lock.close()


if __name__ == '__main__':
    from ugripper_logger import install_exception_logging
    install_exception_logging('ego_time_sync')
    main()
