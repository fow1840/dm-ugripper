"""Operator-guided encoder cycles and session-owned trajectory recordings."""
import contextlib
from collections import deque
import fcntl
import json
import os
from pathlib import Path
import statistics
import threading
import time
import uuid
from common import STATE, DATA, PRODUCTION_LOCK, Busy, atomic_json, read_json, operation_lock, _maintenance_lock, command
from production_test import RECORD_LOCK, send, final_path
from encoder_probe import stream_encoders

RESULT = STATE / 'inspection.json'
MUTEX = threading.Lock()
STOP = threading.Event()
JOB = {}
STABLE_S = 0.5
JITTER = 80
MIN_TRAVEL = 500
TIMEOUT_S = 180


class CycleCheck:
    """Detect stable endpoints by travel, not by entering the passing interval."""
    def __init__(self):
        self.samples = deque()
        self.phase = 'prepare_open'
        self.anchor = None
        self.rows = []
        self.value = None
        self.last_endpoint = None

    def update(self, raw, now):
        value = raw - 65536 if raw >= 32768 else raw
        self.value = value
        self.samples.append((now, value))
        while self.samples and now - self.samples[0][0] > STABLE_S + 0.1:
            self.samples.popleft()
        if self.phase == 'done' or now-self.samples[0][0] < STABLE_S:
            return
        values = [v for _, v in self.samples]
        if max(values)-min(values) > JITTER:
            return
        endpoint = int(statistics.median(values))
        if self.phase == 'prepare_open':
            # Broad negative preparation region; range validation happens each cycle.
            if endpoint > -1000:
                return
            self.anchor = endpoint
            self.phase = 'close'
            self.last_endpoint = now
            self.samples.clear()
            return
        travel = endpoint-self.anchor
        if self.phase == 'close' and travel < MIN_TRAVEL:
            return
        if self.phase == 'open' and travel > -MIN_TRAVEL:
            return
        closed = self.phase == 'close'
        low, high = (-150,150) if closed else (-4200,-3800)
        code = None
        if endpoint < low:
            code = 'closed_below_range' if closed else 'open_above_range'
        elif endpoint > high:
            code = 'closed_above_range' if closed else 'open_below_range'
        self.rows.append(dict(cycle=len(self.rows)//2+1, endpoint='closed' if closed else 'open',
                              value=endpoint, limits=[low,high], status='fail' if code else 'pass', code=code))
        self.anchor = endpoint
        self.last_endpoint = now
        self.samples.clear()
        self.phase = 'done' if len(self.rows)==10 else ('open' if closed else 'close')

    def report(self):
        return dict(phase=self.phase, value=self.value, completed_cycles=len(self.rows)//2, checks=self.rows)


def snapshot():
    return read_json(RESULT)


def save(**fields):
    JOB.update(fields, updated_at_ms=int(time.time()*1000))
    atomic_json(RESULT, JOB)


def control(action, jobid, dataset='trajectory_test'):
    send(json.dumps(dict(action=action, id=jobid, dataset=dataset, expires_ms=int(time.time()*1000)+5000)))


def stop_owned(jobid):
    live = read_json(RECORD_LOCK)
    if live.get('test_request_id') != jobid:
        return
    control('test_stop', jobid)
    deadline = time.monotonic()+90
    while time.monotonic()<deadline:
        if read_json(RECORD_LOCK).get('test_request_id') != jobid:
            return
        time.sleep(.1)
    raise RuntimeError('finalization_timeout')


def encoder_worker(sides):
    checks = {side:CycleCheck() for side in sides}
    with contextlib.closing(stream_encoders(duration=TIMEOUT_S, sides=sides, acquire_lock=False)) as samples:
        last_save = 0
        for sample in samples:
            if STOP.is_set():
                save(status='cancelled', result='incomplete', code='cycles_incomplete'); return
            now = time.monotonic()
            for side, check in checks.items():
                value = sample[side]
                if value.get('status') != 'ok':
                    check.samples.clear()
                    continue
                check.update(value['position'], now)
            if now-last_save >= .2 or all(c.phase=='done' for c in checks.values()):
                save(sides={s:c.report() for s,c in checks.items()})
                last_save = now
            if all(c.phase=='done' for c in checks.values()):
                passed = all(r['status']=='pass' for c in checks.values() for r in c.rows)
                save(status='completed', result='pass' if passed else 'fail', code=None); return
    save(status='completed', result='incomplete', code='cycles_incomplete',
         sides={s:dict(c.report(), code='position_unstable' if len(c.samples)>1 and max(v for _,v in c.samples)-min(v for _,v in c.samples)>JITTER else 'cycles_incomplete') for s,c in checks.items()})


def trajectory_worker():
    jobid = JOB['id']
    owned = None
    try:
        control('test_start', jobid)
        deadline = time.monotonic()+65
        while time.monotonic()<deadline:
            live = read_json(RECORD_LOCK)
            if live.get('test_request_id') == jobid and live.get('episode_dir'):
                owned = Path(live['episode_dir'])
                save(episode=str(owned), phase=live.get('state','starting'))
                if live.get('state') == 'recording' or STOP.is_set():break
            elif owned is not None:
                break
            time.sleep(.1)
        if owned is None:raise RuntimeError('start_not_accepted')
        while not STOP.wait(.2):
            live=read_json(RECORD_LOCK)
            if live.get('test_request_id') != jobid:break
            if live.get('state') != JOB.get('phase'):save(phase=live.get('state','recording'))
        save(phase='finalizing')
        stop_owned(jobid)
        final=final_path(owned)
        meta=read_json(final/'metadata.json')
        passed=meta.get('quality_check_status')=='success' and meta.get('test_dataset_name')=='trajectory_test'
        save(status='completed' if passed else 'failed', result='pass' if passed else 'fail',
             phase='finished', episode=str(final), code=None if passed else (meta.get('quality_check_err_type') or 'episode_invalid'))
    finally:
        stop_owned(jobid)


def worker(stack, kind, sides):
    try:
        if kind=='encoder':encoder_worker(sides)
        else:trajectory_worker()
    except Exception as exc:
        save(status='failed', result='error', code='encoder_read_failed' if kind=='encoder' else 'trajectory_failed', message=str(exc))
    finally:
        save(finished_at_ms=int(time.time()*1000))
        stack.close()
        MUTEX.release()


def start(kind, side='both'):
    if kind not in ('encoder','trajectory'):raise ValueError('unknown_inspection')
    if side not in ('left','right','both'):raise ValueError('invalid_side')
    if not MUTEX.acquire(False):raise Busy('点检正在执行')
    stack=contextlib.ExitStack()
    try:
        if kind=='encoder':
            stack.enter_context(operation_lock())
        else:
            guard=stack.enter_context(open(PRODUCTION_LOCK,'r+'))
            try:fcntl.flock(guard,fcntl.LOCK_EX|fcntl.LOCK_NB)
            except BlockingIOError:raise Busy('设备正在测试')
            with _maintenance_lock():
                if not os.path.ismount(DATA):raise ValueError('数据盘未挂载')
                if command(['systemctl','is-active','ugripper.service']).returncode:raise ValueError('数采服务已停止')
                usb=stack.enter_context(open('/run/usb_auto_update.lock','a'))
                try:fcntl.flock(usb,fcntl.LOCK_EX|fcntl.LOCK_NB)
                except BlockingIOError:raise Busy('U盘升级正在执行')
        STOP.clear();JOB.clear()
        sides=('left','right') if side=='both' else (side,)
        JOB.update(id=uuid.uuid4().hex, kind=kind, status='running', result=None,
                   phase='starting', started_at_ms=int(time.time()*1000), sides={})
        save()
        jobid=JOB['id']
        threading.Thread(target=worker,args=(stack,kind,sides),daemon=True).start()
        return dict(id=jobid, status_url='/api/v1/tasks/inspection/'+jobid)
    except Exception:
        stack.close();MUTEX.release();raise


def stop(kind, jobid):
    if not MUTEX.locked() or JOB.get('id')!=jobid or JOB.get('kind')!=kind:raise ValueError('inspection_not_active')
    STOP.set()
    return dict(id=jobid, stopping=True)


def recover():
    previous=snapshot()
    if previous.get('status')!='running':return
    JOB.clear();JOB.update(previous)
    try:
        if JOB.get('kind')=='trajectory':stop_owned(JOB['id'])
        save(status='interrupted', result='incomplete', code='service_interrupted')
    except Exception as exc:
        save(status='interrupted', result='error', code='finalization_timeout', message=str(exc))
