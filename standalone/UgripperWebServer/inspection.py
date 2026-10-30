"""Operator-guided encoder cycles and session-owned trajectory recordings."""
import contextlib
import fcntl
import json
import os
import re
from pathlib import Path
import threading
import time
import uuid
from common import STATE, DATA, PRODUCTION_LOCK, Busy, atomic_json, read_json, operation_lock, _maintenance_lock, command
from production_test import RECORD_LOCK, send, final_path
from encoder_probe import stream_encoders
from recording_request import RecordingRequest, ControlError
from inspection_events import store, lookup, cleanup_encoder_history

RESULT = STATE / 'inspection.json'
MUTEX = threading.Lock()
STOP = threading.Event()
JOB = {}
REVERSAL_S = 0.1
MIN_REVERSAL = 500
CLOSED_REGION_MIN = -1000
OPEN_REGION_MAX = -3000
TIMEOUT_S = 180


class CycleCheck:
    """Commit each half-cycle's extreme only after confirmed reverse travel."""
    def __init__(self):
        self.phase = 'prepare_open'
        self.rows = []
        self.value = None
        self.peak = None
        self.reverse_since = None
        self.reverse_peak = None

    def interrupt(self):
        # Missing samples cannot confirm a continuous reversal.
        self.reverse_since = None
        self.reverse_peak = None

    def update(self, raw, now):
        value = raw - 65536 if raw >= 32768 else raw
        self.value = value
        if self.phase == 'done':return
        closed = self.phase == 'close'
        extreme = max if closed else min
        self.peak = value if self.peak is None else extreme(self.peak, value)
        # Initial opening is preparation; its peak is not one of the five cycles.
        # Recognize an endpoint region before counting a reversal. These broad
        # regions are separate from the pass limits so real out-of-range peaks fail.
        if (closed and self.peak < CLOSED_REGION_MIN) or (not closed and self.peak > OPEN_REGION_MAX):
            self.interrupt()
            return
        reverse = self.peak-value if closed else value-self.peak
        if reverse < MIN_REVERSAL:
            self.interrupt()
            return
        opposite = min if closed else max
        self.reverse_peak = value if self.reverse_peak is None else opposite(self.reverse_peak, value)
        if self.reverse_since is None:self.reverse_since = now
        if now-self.reverse_since < REVERSAL_S:return
        if self.phase != 'prepare_open':
            self.record_endpoint(self.peak, closed)
        self.phase = 'done' if len(self.rows)==10 else ('open' if closed else 'close')
        # Include all samples used to confirm reversal in the next half-cycle.
        self.peak = self.reverse_peak
        self.interrupt()

    def record_endpoint(self, endpoint, closed):
        low, high = (-100,150) if closed else (-4200,-3650)
        code = None
        if endpoint < low:
            code = 'closed_below_range' if closed else 'open_above_range'
        elif endpoint > high:
            code = 'closed_above_range' if closed else 'open_below_range'
        self.rows.append(dict(cycle=len(self.rows)//2+1, endpoint='closed' if closed else 'open',
                              value=endpoint, limits=[low,high], status='fail' if code else 'pass', code=code))

    def report(self):
        return dict(phase=self.phase, value=self.value, completed_cycles=len(self.rows)//2, checks=self.rows)


def snapshot():
    return read_json(RESULT)


def save(**fields):
    JOB.update(fields, updated_at_ms=int(time.time()*1000))
    store(JOB)
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


def encoder_hardware_rule():
    cached=read_json(Path('/dev/shm/ugripper/firmware_status.json'))
    if not isinstance(cached,dict):cached={}
    stamp=cached.get('updated_at_ms')
    fresh=type(stamp) in (int,float) and 0<=time.time()*1000-stamp<=15000
    grippers=cached.get('gripper',{}) if fresh else {}
    if not isinstance(grippers,dict):grippers={}
    identities={};limits={};failures=[]
    for side in ('left','right'):
        item=grippers.get(side,{})
        if not isinstance(item,dict):item={}
        sn=item.get('serial') if item.get('connected') is True else None
        valid=isinstance(sn,str) and re.fullmatch(r'DAG[A-Z0-9]{13}',sn)
        version={'1':'2.5','2':'2.5','3':'2.9'}.get(sn[4]) if valid else None
        code=None if version else 'gripper_sn_unsupported' if valid else 'gripper_sn_unavailable'
        limit={'2.5':2,'2.9':1}.get(version)
        identities[side]=dict(serial=sn,hardware_version=version,consistency_limit_deg=limit)
        limits[side]=limit
        if code:failures.append(dict(side=side,code=code))
    if {v['hardware_version'] for v in identities.values()}=={'2.5','2.9'}:
        for side,item in identities.items():
            if item['hardware_version']=='2.5':failures.append(dict(side=side,code='gripper_version_mismatch'))
    return identities,limits,failures


def encoder_report(checks, limit, rule_error=None, terminal=False):
    sides={};failures=[]
    if rule_error:
        failures.extend(rule_error if isinstance(rule_error,list) else [dict(code=rule_error)])
    for side,check in checks.items():
        report=check.report()
        side_limit=limit.get(side) if isinstance(limit,dict) else limit
        for row in check.rows:
            if row['status']=='fail':
                failures.append(dict(side=side,cycle=row['cycle'],endpoint=row['endpoint'],code=row['code']))
        consistency=[]
        for endpoint in ('closed','open'):
            values=[r['value'] for r in check.rows if r['endpoint']==endpoint]
            # Compare peaks only within this endpoint group, across rounds.
            difference=max((abs(a-b) for a in values for b in values),default=0) if values else None
            code=None
            if len(values)<5:
                status='incomplete' if terminal else 'pending'
                if terminal:code='cycles_incomplete'
            elif side_limit is None:
                status='unavailable'
            else:
                # Compare before rounding; equality is a pass.
                status='fail' if difference*360>side_limit*65536 else 'pass'
                if status=='fail':code=endpoint+'_consistency_exceeded'
            consistency.append(dict(endpoint=endpoint,count=len(values),
                peaks_deg=[round(v*360/65536,6) for v in values],
                max_difference_deg=round(difference*360/65536,6) if difference is not None else None,
                limit_deg=side_limit,status=status,code=code))
            if code:failures.append(dict(side=side,endpoint=endpoint,code=code))
        sides[side]=dict(report,consistency=consistency)
    return dict(sides=sides,failures=failures)


def recorder_session():
    try:
        result=command(['systemctl','show','ugripper.service','--property=InvocationID','--property=ActiveState'],timeout=2)
        fields=dict(line.split('=',1) for line in result.stdout.splitlines() if '=' in line)
        return fields.get('InvocationID') if result.returncode==0 and fields.get('ActiveState')=='active' else None
    except Exception:
        return None


def interrupt_encoder(report=None):
    report=dict(report or {})
    failures=list(report.get('failures',JOB.get('failures',[])))
    if not any(f.get('code')=='service_interrupted' for f in failures):
        failures.append(dict(code='service_interrupted'))
    sides={}
    for side,value in report.get('sides',JOB.get('sides',{})).items():
        groups=[dict(item,status='incomplete',code='cycles_incomplete') if item.get('status')=='pending' else item for item in value.get('consistency',[])]
        sides[side]=dict(value,consistency=groups)
    report.update(failures=failures,sides=sides)
    save(status='interrupted',result='incomplete',phase='finished',code='service_interrupted',
         finished_at_ms=int(time.time()*1000),**report)


def encoder_completion_feedback(side):
    try:
        send(json.dumps(dict(action='encoder_side_done',id=JOB['id'],side=side,expires_ms=int(time.time()*1000)+5000)))
    except (OSError,RuntimeError):
        # Feedback must not abort the remaining hand or change its test result.
        from common import ugripper_log
        ugripper_log('WARNING','encoder completion feedback unavailable side='+side,component='maintenance')


def encoder_worker(sides):
    checks = {side:CycleCheck() for side in sides}
    session=recorder_session()
    version,limit,rule_error=encoder_hardware_rule()
    save(grippers=version,consistency_limits_deg=limit,**encoder_report(checks,limit,rule_error))
    if not session:
        interrupt_encoder(encoder_report(checks,limit,rule_error,terminal=True));return
    try:
        with contextlib.closing(stream_encoders(duration=TIMEOUT_S, sides=sides, acquire_lock=False)) as samples:
            last_save = 0
            feedback_sent=set()
            next_session_check=0
            for sample in samples:
                if STOP.is_set():
                    save(status='cancelled',result='incomplete',code='cycles_incomplete',
                         **encoder_report(checks,limit,rule_error,terminal=True));return
                now = time.monotonic()
                if now>=next_session_check:
                    if recorder_session()!=session:
                        interrupt_encoder(encoder_report(checks,limit,rule_error,terminal=True));return
                    current_grippers,_,_=encoder_hardware_rule()
                    if current_grippers!=version:
                        report=encoder_report(checks,limit,rule_error,terminal=True)
                        report['failures'].append(dict(code='gripper_identity_changed'))
                        save(status='interrupted',result='incomplete',phase='finished',code='gripper_identity_changed',**report);return
                    next_session_check=now+1
                for side, check in checks.items():
                    value = sample[side]
                    if value.get('status') != 'ok':
                        check.interrupt()
                        continue
                    check.update(value['position'], now)
                done=all(c.phase=='done' for c in checks.values())
                finished_sides={s for s,c in checks.items() if c.phase=='done'}-feedback_sent
                if now-last_save >= .2 or done or finished_sides:
                    report=encoder_report(checks,limit,rule_error)
                    save(**report)
                    last_save = now
                    for side in sorted(finished_sides):
                        encoder_completion_feedback(side)
                        feedback_sent.add(side)
                if done:
                    save(status='completed',result='fail' if report['failures'] else 'pass',code=None);return
        save(status='completed',result='incomplete',code='cycles_incomplete',
             **encoder_report(checks,limit,rule_error,terminal=True))
    except Exception:
        report=encoder_report(checks,limit,rule_error,terminal=True)
        if recorder_session()!=session:
            interrupt_encoder(report);return
        report['failures'].append(dict(code='encoder_read_failed'))
        save(**report)
        raise


def notify(label, status, code=None):
    events=dict(JOB.get('events',{}))
    if label in events:return
    event=dict(event='trajectory_recording_'+label, id=JOB['id'], status=status, code=code)
    if label=='finished':event['episode']=JOB.get('episode')
    events[label]=event
    save(events=events)


def trajectory_worker():
    jobid = JOB['id']
    request = RecordingRequest(jobid, 'trajectory_test', send, RECORD_LOCK, STOP)
    owned = None
    started = False
    try:
        owned = request.start()
        started = True
        save(episode=str(owned), phase='recording')
        notify('started', 'recording')
        while not STOP.wait(.2):
            live=read_json(RECORD_LOCK)
            if live.get('test_request_id') != jobid:break
            if live.get('state') != JOB.get('phase'):save(phase=live.get('state','recording'))
        save(phase='finalizing')
        request.stop()
        final=final_path(owned)
        meta=read_json(final/'metadata.json')
        passed=meta.get('quality_check_status')=='success' and meta.get('test_dataset_name')=='trajectory_test'
        save(status='completed' if passed else 'failed', phase='finished', episode=str(final),
             code=None if passed else (meta.get('quality_check_err_type') or 'episode_invalid'))
        notify('finished',JOB['status'],JOB.get('code'))
    except Exception as exc:
        code = exc.code if isinstance(exc, ControlError) else 'recording_failed'
        save(message=str(exc))
        try:request.stop()
        except Exception as cleanup:
            save(cleanup_code=getattr(cleanup, 'code', 'cleanup_failed'), cleanup_message=str(cleanup))
        # Never promise a final directory when cleanup did not complete.
        final=final_path(owned) if owned else None
        save(status='failed',phase='finished',code=code,episode=str(final) if final and final.exists() else None)
        notify('finished' if started else 'started','failed',code)


def worker(stack, kind, sides):
    try:
        if kind=='encoder':encoder_worker(sides)
        else:trajectory_worker()
    except Exception as exc:
        save(status='failed', result='error', code='encoder_read_failed' if kind=='encoder' else 'trajectory_failed', message=str(exc))
        if kind=='trajectory':notify('finished' if 'started' in JOB.get('events',{}) and JOB['events']['started']['status']=='recording' else 'started','failed','trajectory_failed')
    finally:
        save(finished_at_ms=int(time.time()*1000))
        stack.close()
        MUTEX.release()


def start(kind, side='both'):
    if kind not in ('encoder','trajectory'):raise ValueError('unknown_inspection')
    if side != 'both':raise ValueError('inspection_requires_both_sides')
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
        sides=('left','right')
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
    if not previous:
        cleanup_encoder_history()
        RESULT.with_suffix('.new').unlink(missing_ok=True)
        return
    # History is committed first; recover its newest state after a partial save.
    historical=lookup(previous['id'])
    if historical and historical.get('updated_at_ms',0)>=previous.get('updated_at_ms',0):
        previous=historical
    cleanup_encoder_history()
    RESULT.with_suffix('.new').unlink(missing_ok=True)
    JOB.clear();JOB.update(previous)
    if previous.get('status')!='running':
        if previous.get('kind')=='trajectory' and previous.get('status') in ('completed','failed'):
            started=previous.get('events',{}).get('started',{}).get('status')=='recording'
            notify('finished' if started else 'started',previous['status'],previous.get('code'))
        return
    if JOB.get('kind')=='encoder':
        interrupt_encoder();return
    try:
        if JOB.get('kind')=='trajectory':stop_owned(JOB['id'])
        save(status='interrupted', result='incomplete', code='service_interrupted')
    except Exception as exc:
        save(status='interrupted', result='error', code='finalization_timeout', message=str(exc))

    if JOB.get('kind')=='trajectory':
        started=JOB.get('events',{}).get('started',{}).get('status')=='recording'
        save(status='failed',code=JOB.get('code') or 'service_interrupted')
        notify('finished' if started else 'started','failed',JOB['code'])
