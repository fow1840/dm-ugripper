"""Session-owned recording commands and transition-only shared-memory replies."""
import json
import os
from pathlib import Path
import time
from common import atomic_json, read_json

REQUEST = Path('/dev/shm/ugripper/test_control_request.json')
RESULT = Path('/dev/shm/ugripper/test_control_result.json')


class ControlError(RuntimeError):
    def __init__(self, code, message=''):
        self.code = code
        super().__init__(message or code)


class RecordingRequest:
    def __init__(self, jobid, dataset, send, lock, cancelled):
        self.id, self.dataset = jobid, dataset
        self.send, self.lock, self.cancelled = send, lock, cancelled
        self.stop_deadline = None
        self.stop_sent = False
        self.sent = False
        self.pid = None

    def reply(self):
        value = read_json(RESULT)
        if value.get('id') != self.id:
            return {}
        if self.pid is None:
            self.pid = value.get('pid')
        if self.pid and value.get('pid') != self.pid:
            raise ControlError('runtime_restarted')
        return value

    def live(self):
        value = read_json(self.lock)
        return value if value.get('test_request_id') == self.id else {}

    def cancel(self):
        # A new request replacing this one also cancels an older in-flight START.
        value = read_json(REQUEST)
        if value.get('id') == self.id and not value.get('cancelled'):
            atomic_json(REQUEST, dict(value, cancelled=True))

    def start(self):
        expires = int(time.time()*1000)+5000
        atomic_json(REQUEST, dict(id=self.id, cancelled=False, expires_ms=expires))
        self.send(json.dumps(dict(action='test_start', id=self.id, dataset=self.dataset, expires_ms=expires)))
        self.sent = True
        admission_deadline = time.monotonic()+5
        start_deadline = None
        while True:
            if self.cancelled.is_set():
                self.cancel()
                raise ControlError('cancelled')
            reply = self.reply()
            phase = reply.get('status')
            if phase in ('rejected', 'failed', 'cancelled'):
                raise ControlError(reply.get('code') or phase, reply.get('message', ''))
            live = self.live()
            if phase == 'recording' and live.get('state') == 'recording' and live.get('episode_dir'):
                return Path(live['episode_dir'])
            if phase == 'recording' and not live:
                raise ControlError('recording_ended_during_start')
            now = time.monotonic()
            if phase == 'starting' and start_deadline is None:
                start_deadline = now+65
            if self.pid:
                try:
                    os.kill(self.pid, 0)
                except ProcessLookupError:
                    self.cancel()
                    raise ControlError('runtime_stopped')
            if now >= (start_deadline if start_deadline is not None else admission_deadline):
                self.cancel()
                raise ControlError('start_timeout' if start_deadline else 'command_not_accepted')
            self.cancelled.wait(.1)

    def stop(self):
        if not self.sent:
            return
        self.cancel()
        if self.stop_deadline is None:
            self.stop_deadline = time.monotonic()+90
        while True:
            live = self.live()
            reply = self.reply()
            if not live and (reply.get('status') in ('rejected', 'failed', 'cancelled', 'recording')
                             or (not reply and time.time()*1000 > read_json(REQUEST).get('expires_ms', 0))):
                return
            if self.pid:
                try:
                    os.kill(self.pid, 0)
                except ProcessLookupError:
                    raise ControlError('runtime_stopped')
            remaining = self.stop_deadline-time.monotonic()
            if remaining <= 0:
                raise ControlError('finalization_timeout')
            if live.get('state') == 'recording' and not self.stop_sent:
                self.send(json.dumps(dict(action='test_stop', id=self.id,
                                          expires_ms=int(time.time()*1000)+5000)))
                self.stop_sent = True
            time.sleep(min(.1, remaining))
