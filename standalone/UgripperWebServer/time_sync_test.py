"""Persistent, device-owned stopwatch recordings using the packaged timing helper."""
import contextlib
import fcntl
import os
from pathlib import Path
import signal
import subprocess
import threading
import time
import uuid
from common import BASE, STATE, DATA, PRODUCTION_LOCK, Busy, atomic_json, read_json, command, _maintenance_lock
from production_test import RECORD_LOCK, check_episode, stop_owned, final_path

RESULT = STATE / 'time-sync.json'
SCRIPT = BASE / 'board_stopwatch_record_test.sh'
GPIO = Path('/sys/class/gpio/gpio139/value')
MUTEX = threading.Lock()
CANCEL = threading.Event()
JOB = {}


def parameters(values):
    if not isinstance(values, dict):
        raise ValueError("测试参数必须是对象")
    result = {}
    for key, default, low, high in [('duration_s',100,1,3600), ('count',1,1,100), ('delay_s',2,0,3599)]:
        value = values.get(key, default)
        if type(value) is not int or not low <= value <= high:
            raise ValueError(f'{key} 必须是 {low}–{high} 的整数')
        result[key] = value
    if result['delay_s'] >= result['duration_s']:
        raise ValueError('毫秒表延迟必须小于录制时长')
    return result


def snapshot():
    value = read_json(RESULT)
    if value.get('status') == 'running':
        value['phase_remaining_s'] = max(0, value.get('phase_deadline',time.time()) - time.time())
    return value


def save(**fields):
    JOB.update(fields, updated=time.time())
    atomic_json(RESULT, JOB)
    history = STATE / 'time-sync-results'
    history.mkdir(exist_ok=True)
    atomic_json(history / (JOB['id']+'.json'), JOB)


def reset_gpio():
    if GPIO.exists():
        GPIO.write_text('1\n')
        time.sleep(5)
        if GPIO.read_text().strip() != '1':
            raise RuntimeError('毫秒表复位失败')


def stop_process(process):
    if process.poll() is None:
        process.send_signal(signal.SIGTERM)
        try:
            process.wait(timeout=15)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            process.wait(timeout=5)


def run_round(number):
    config = JOB['settings']
    row = dict(round=number, status='running', started=time.time())
    JOB['rounds'].append(row)
    save(round=number, phase='starting', phase_deadline=time.time()+30)
    log_path = STATE / 'time-sync-results' / f"{JOB['id']}-{number}.log"
    process = None
    owned = None
    failure = None
    try:
        if RECORD_LOCK.exists():
            raise Busy('设备正在录制或收尾')
        with log_path.open('w') as output, log_path.open() as lines:
            process = subprocess.Popen(['/bin/bash', str(SCRIPT), '--board-run',
                '--duration-sec',str(config['duration_s']), '--stopwatch-delay-sec',str(config['delay_s']),
                '--target-label','web'], stdout=output, stderr=subprocess.STDOUT,
                start_new_session=True, env={'PATH':'/usr/sbin:/usr/bin:/sbin:/bin','LANG':'C.UTF-8'})
            deadline = time.monotonic()+config['duration_s']+240
            while True:
                for line in lines.readlines():
                    if line.startswith('WEB_PHASE='):
                        phase, seconds = line.strip().split('=',1)[1].split(':')
                        save(phase=phase, phase_deadline=time.time()+int(seconds))
                    elif line.startswith('episode='):
                        owned = Path(line.strip().split('=',1)[1])
                        row['episode'] = str(owned)
                        save()
                live = read_json(RECORD_LOCK).get('episode_dir')
                if live and owned is None:
                    owned = Path(live)
                    row['episode'] = str(owned)
                    save()
                if process.poll() is not None:
                    # One final read after exit catches the helper's final episode output.
                    for line in lines.readlines():
                        if line.startswith('episode='):
                            owned = Path(line.strip().split('=',1)[1])
                    break
                if CANCEL.wait(0.1):
                    raise InterruptedError('cancelled by operator')
                if time.monotonic() > deadline:
                    raise TimeoutError('stopwatch recording timeout')
            if CANCEL.is_set():
                raise InterruptedError('cancelled by operator')
            if process.returncode or owned is None:
                raise RuntimeError('毫秒表录制失败，请查看测试日志')
            row.update(check_episode(owned,config['duration_s']))
            if not read_json(final_path(owned)/'stopwatch_timing.json'):
                raise RuntimeError('stopwatch_timing.json 缺失或无效')
    except Exception as exc:
        failure = exc
        row.update(status='cancelled' if isinstance(exc,InterruptedError) else 'failed',reason=str(exc))
    finally:
        # Preserve the mutex until STOP/finalization and GPIO reset have completed.
        cleanup = []
        if process:
            cleanup.append(lambda: stop_process(process))
        if failure:
            cleanup.append(reset_gpio)
        if owned:
            cleanup.append(lambda: stop_owned(owned))
        for action in cleanup:
            try:
                action()
            except Exception as exc:
                row.update(status='failed',reason=str(exc))
        if owned:
            row['episode'] = str(final_path(owned))
        row['log'] = log_path.read_text(errors='replace')[-12000:] if log_path.exists() else ''
        row['finished'] = time.time()
        save()
    if failure:
        raise failure
    if row['status'] != 'success':
        raise RuntimeError(row.get('reason','recording failed'))


def worker(stack):
    try:
        for number in range(1, JOB['settings']['count']+1):
            if CANCEL.is_set():
                raise InterruptedError('cancelled by operator')
            run_round(number)
        save(status='completed',phase='finished',finished=time.time())
    except InterruptedError:
        save(status='cancelled',phase='finished',finished=time.time())
    except Exception as exc:
        save(status='failed',phase='finished',reason=str(exc),finished=time.time())
    finally:
        stack.close()
        MUTEX.release()


def start(values):
    config = parameters(values)
    if not MUTEX.acquire(False):
        raise Busy('时间同步测试正在执行')
    stack = contextlib.ExitStack()
    try:
        guard = stack.enter_context(open(PRODUCTION_LOCK,'r+'))
        try:
            fcntl.flock(guard,fcntl.LOCK_EX|fcntl.LOCK_NB)
        except BlockingIOError:
            raise Busy('设备正在录制、测试、下载或维护，请稍后重试')
        with _maintenance_lock():
            if not os.path.ismount(DATA):
                raise ValueError('数据盘未挂载')
            if command(['systemctl','is-active','ugripper.service']).returncode:
                raise ValueError('数采服务已停止')
            if not SCRIPT.is_file():
                raise ValueError('毫秒表测试脚本未安装')
            usb = stack.enter_context(open('/run/usb_auto_update.lock','a'))
            try:
                fcntl.flock(usb,fcntl.LOCK_EX|fcntl.LOCK_NB)
            except BlockingIOError:
                raise Busy('U 盘升级正在执行')
            from environment_config import read_settings
            CANCEL.clear()
            JOB.clear()
            JOB.update(id=uuid.uuid4().hex,status='running',started=time.time(),settings=config,
                       configuration=read_settings(),rounds=[])
            save(phase='starting',phase_deadline=time.time()+30)
            history = STATE / 'time-sync-results'
            for old in sorted(history.glob('*.json'),key=lambda p:p.stat().st_mtime)[:-20]:
                for log in history.glob(old.stem+'-*.log'):
                    log.unlink()
                old.unlink()
        threading.Thread(target=worker,args=(stack,),daemon=True).start()
        return {'id':JOB['id']}
    except Exception:
        stack.close()
        MUTEX.release()
        raise


def cancel(jobid):
    if not MUTEX.locked() or JOB.get('id') != jobid:
        raise ValueError('测试任务已结束或已更换')
    CANCEL.set()
    return {'id':jobid,'cancelling':True}


def recover():
    old = read_json(RESULT)
    if old.get('status') != 'running':
        return
    JOB.clear()
    JOB.update(old)
    errors = []
    try:
        reset_gpio()
    except Exception as exc:
        errors.append(str(exc))
    for row in JOB.get('rounds',[]):
        if row.get('status') == 'running':
            try:
                if row.get('episode'):
                    stop_owned(Path(row['episode']))
            except Exception as exc:
                errors.append(str(exc))
            row.update(status='failed',reason='maintenance service interrupted')
    save(status='interrupted',phase='finished',finished=time.time(),
         reason='维护服务中断，任务未自动续跑'+('; '+'; '.join(errors) if errors else ''))
