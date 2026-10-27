"""Device-owned production recording schedule. No browser heartbeat dependency."""
import contextlib
import fcntl
import json
import os
from pathlib import Path
import threading
import time
import uuid
from common import STATE, DATA, Busy, atomic_json, read_json, command, _maintenance_lock

PLAN = [('A', 120, 20, 51), ('B', 300, 20, 11)]
RECORD_LOCK = Path('/tmp/umi_recording.lock')
CONTROL = Path('/dev/shm/ugripper/umi_record_control.pipe')
RESULT = STATE/'production.json'
MUTEX = threading.Lock()
CANCEL = threading.Event()
JOB = {}


def verdict(rows):
    failures = {group: sum(r['group']==group and r['status']=='failed' for r in rows) for group in ('A','B')}
    return failures, failures['A'] < 3 and failures['B'] < 2 and sum(failures.values()) <= 3


def snapshot():
    value = read_json(RESULT)
    if value.get('status') == 'running':
        value['remaining_s'] = max(0, value.get('phase_deadline',time.time())-time.time()) + value.get('future_s',0)
    return value


def save(**fields):
    JOB.update(fields, updated=time.time())
    failures, passed = verdict(JOB.get('rounds',[]))
    JOB.update(failures=failures, threshold_passed=passed)
    atomic_json(RESULT, JOB)
    # A bounded history of reports; recordings remain in the normal data tree.
    history=STATE/'production-results';history.mkdir(exist_ok=True)
    atomic_json(history/(JOB['id']+'.json'), JOB)


def send(action):
    fd=os.open(CONTROL, os.O_WRONLY|os.O_NONBLOCK|os.O_CLOEXEC)
    try:
        if os.write(fd,(action+'\n').encode()) != len(action)+1:raise RuntimeError('record control short write')
    finally:os.close(fd)


class Cancelled(Exception): pass


def wait_until(deadline, check_cancel=True):
    while time.monotonic()<deadline:
        if check_cancel and CANCEL.wait(min(0.1,max(0,deadline-time.monotonic()))):raise Cancelled()
        if not check_cancel:time.sleep(min(0.1,max(0,deadline-time.monotonic())))


def final_path(path):
    return path.with_name(path.name[:-5]) if path.name.endswith('-temp') else path


def check_episode(path, duration):
    path=final_path(path)
    meta=read_json(path/'metadata.json')
    reasons=[]
    if not meta: reasons.append('metadata missing or invalid')
    elif meta.get('quality_check_status') != 'success': reasons.append('quality check: '+str(meta.get('quality_check_err_type') or meta.get('quality_check_status')))
    actual=meta.get('collection_duration_s',0)
    if not isinstance(actual,(int,float)) or not duration-2<=actual<=duration+10: reasons.append('recording duration mismatch: '+str(actual))
    required=meta.get('require_files',[])
    if not required:reasons.append('required file list missing')
    for name in required:
        if not isinstance(name,str) or Path(name).name != name:reasons.append('invalid required filename');continue
        file=path/name
        if not file.is_file() or file.stat().st_size==0:reasons.append('missing/empty: '+name)
    error=path/'validation_error.log'
    if error.exists():reasons.append(error.read_text(errors='replace')[-8000:])
    return dict(episode=str(path),actual_s=actual,status='failed' if reasons else 'success',reason='; '.join(reasons))


def stop_owned(path):
    live=read_json(RECORD_LOCK)
    if live and live.get('episode_dir') == str(path):
        if live.get('test_request_id'):
            send(json.dumps(dict(action='test_stop', id=live['test_request_id'], expires_ms=int(time.time()*1000)+5000)))
        else:
            send('STOP')
    deadline=time.monotonic()+90
    while RECORD_LOCK.exists() and time.monotonic()<deadline:time.sleep(0.1)
    if RECORD_LOCK.exists():raise RuntimeError('stop/finalization timeout (90 s)')


def run_round(group, number, duration, pause, future):
    row=dict(group=group,round=number,planned_s=duration,started=time.time(),status='running',reason='')
    JOB['rounds'].append(row)
    save(group=group,round=number,phase='starting',future_s=future+duration+pause,phase_deadline=time.time()+30)
    owned=None
    try:
        if RECORD_LOCK.exists():raise RuntimeError('unexpected recording already active')
        if not os.path.ismount(DATA):raise RuntimeError('data disk not mounted')
        if command(['systemctl','is-active','ugripper.service']).returncode:raise RuntimeError('recording service inactive')
        request_id = uuid.uuid4().hex
        send(json.dumps(dict(action='test_start', id=request_id, dataset='production_test', expires_ms=int(time.time()*1000)+5000)))
        deadline=time.monotonic()+30
        while time.monotonic()<deadline:
            live=read_json(RECORD_LOCK)
            if live.get('episode_dir') and live.get('test_request_id') == request_id:
                owned=Path(live['episode_dir']);row['episode']=str(owned);save();break
            # Observe a pending START even after cancel, so it can be stopped safely.
            time.sleep(0.05)
        if owned is None:raise RuntimeError('START not accepted within 30 s')
        if CANCEL.is_set():raise Cancelled()
        save(phase='recording',phase_deadline=time.time()+duration,future_s=future+pause)
        deadline=time.monotonic()+duration
        while time.monotonic()<deadline:
            if CANCEL.wait(0.1):raise Cancelled()
            if not RECORD_LOCK.exists():raise RuntimeError('recording stopped before scheduled duration')
        save(phase='finalizing',phase_deadline=time.time()+90,future_s=future+pause)
        stop_owned(owned)
        row.update(check_episode(owned,duration))
    except Cancelled:
        row.update(status='cancelled',reason='cancelled by operator');raise
    except Exception as exc:
        row.update(status='failed',reason=str(exc))
        with contextlib.suppress(Exception):
            row['log']=command(['journalctl','-u','ugripper.service','--since','@'+str(int(row['started'])),'-n','80','--no-pager'],10).stdout[-12000:]
        if owned:
            with contextlib.suppress(Exception):
                detail=check_episode(owned,duration)
                row['reason'] += '; '+detail['reason']
    finally:
        if owned:
            try:
                stop_owned(owned)
                final=final_path(owned)
                if final.exists():
                    row['episode']=str(final)
                    row['actual_s']=read_json(final/'metadata.json').get('collection_duration_s',0)
            except Exception as exc:
                row.update(status='failed',reason=row['reason']+'; '+str(exc))
        row['finished']=time.time();save()
    if RECORD_LOCK.exists():raise RuntimeError('recording still active; schedule interrupted')
    save(phase='pause',phase_deadline=time.time()+pause,future_s=future)
    wait_until(time.monotonic()+pause)


def worker(stack):
    try:
        sequence=[(g,n,d,p) for g,d,p,count in PLAN for n in range(1,count+1)]
        for index,(group,number,duration,pause) in enumerate(sequence):
            if CANCEL.is_set():raise Cancelled()
            future=sum(d+p for _,_,d,p in sequence[index+1:])
            run_round(group,number,duration,pause,future)
        _,passed=verdict(JOB['rounds'])
        save(status='passed' if passed else 'failed',phase='finished',finished=time.time(),remaining_s=0)
    except Cancelled:save(status='cancelled',phase='finished',finished=time.time(),remaining_s=0)
    except Exception as exc:save(status='interrupted',phase='finished',reason=str(exc),finished=time.time(),remaining_s=0)
    finally:
        stack.close();MUTEX.release()


def start():
    if not MUTEX.acquire(False):raise Busy('生产压测正在执行')
    stack=contextlib.ExitStack()
    try:
        guard=stack.enter_context(open('/run/ugripper/production.lock','r+'))
        try:fcntl.flock(guard,fcntl.LOCK_EX|fcntl.LOCK_NB)
        except BlockingIOError:raise Busy('设备正在录制、下载或维护，请稍后重试')
        with _maintenance_lock():
            if not os.path.ismount(DATA):raise ValueError('数据盘未挂载')
            if command(['systemctl','is-active','ugripper.service']).returncode:raise ValueError('数采服务已停止')
            usb=stack.enter_context(open('/run/usb_auto_update.lock','a'))
            try:fcntl.flock(usb,fcntl.LOCK_EX|fcntl.LOCK_NB)
            except BlockingIOError:raise Busy('U 盘升级正在执行')
            CANCEL.clear();JOB.clear();JOB.update(id=uuid.uuid4().hex,status='running',started=time.time(),rounds=[],plan=PLAN)
            save(phase='starting',phase_deadline=time.time(),future_s=sum((d+p)*n for _,d,p,n in PLAN))
            history=STATE/'production-results'
            for old in sorted(history.glob('*.json'),key=lambda p:p.stat().st_mtime)[:-20]:old.unlink()
        threading.Thread(target=worker,args=(stack,),daemon=True).start()
        return {'id':JOB['id']}
    except Exception:stack.close();MUTEX.release();raise


def cancel(jobid):
    if not MUTEX.locked() or JOB.get('id') != jobid:raise ValueError('压测任务已结束或已更换')
    CANCEL.set()
    return {'id':jobid,'cancelling':True}


def recover():
    old=read_json(RESULT)
    if old.get('status')!='running':return
    JOB.clear();JOB.update(old)
    for row in JOB.get('rounds',[]):
        if row.get('status')=='running':
            if row.get('episode'):
                with contextlib.suppress(Exception):stop_owned(Path(row['episode']))
            row.update(status='failed',reason='maintenance service interrupted',finished=time.time())
    save(status='interrupted',phase='finished',reason='maintenance service interrupted; not resumed automatically',finished=time.time(),remaining_s=0)
