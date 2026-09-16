#!/usr/bin/env python3
"""Privileged local worker. No TCP listener or client-provided command arguments."""
import contextlib
import fcntl
import hashlib
import json
import os
from pathlib import Path
import pwd
import re
import shutil
import socketserver
import stat
import threading
import time
import uuid
from common import BASE, ROOT, STATE, RUN, MAX_UPLOAD, ALLOW, Busy, atomic_json, read_json, command, operation_lock
from production import main_camera_dfu as dfu
from environment_config import validate_settings, write_settings

MUTEX = threading.Lock()
PYTHON = Path('/usr/bin/python3')
PRIVATE = Path('/var/lib/ugripper-maintenance')
JOB = {}

def report(stage, **fields):
    JOB.update(stage=stage, updated=time.time(), **fields)
    atomic_json(STATE/'task.json', JOB)

def run(args, timeout=60):
    p = command(args, timeout)
    with (PRIVATE / (JOB['id'] + '.log')).open('a') as log:
        log.write('$ ' + ' '.join(map(str,args)) + '\n' + p.stdout + p.stderr + '\n')
    if p.returncode:
        raise RuntimeError(f'{Path(str(args[0])).name} 执行失败，退出码 {p.returncode}；请获取日志')
    return p.stdout

def validate_hex(path):
    # Check each Intel HEX checksum before the vendor loader allocates an address span.
    base = 0; addresses = []; eof = False
    for line in path.read_text('ascii').splitlines():
        if not line.strip(): continue
        if eof or not line.startswith(':'): raise ValueError('HEX 格式错误')
        b = bytes.fromhex(line[1:])
        if len(b) < 5 or len(b) != b[0]+5 or sum(b)%256: raise ValueError('HEX 校验失败')
        count, kind, addr = b[0], b[3], int.from_bytes(b[1:3], 'big')
        if kind == 0 and count:
            addresses.extend((base+addr,base+addr+count-1))
        elif kind == 1: eof = True
        elif kind == 4: base = int.from_bytes(b[4:-1], 'big') << 16
        elif kind == 2: base = int.from_bytes(b[4:-1], 'big') << 4
        elif kind not in (0,3,5): raise ValueError('不支持的 HEX 记录')
    if not eof or not addresses or max(addresses)-min(addresses) > 2*1024*1024:
        raise ValueError('HEX 地址范围或结束记录异常')

def camera_version(side):
    return dfu.parse_current_camera_version(run([BASE/'bin/linux_read_version','--device',f'/dev/cam_{side}']))

def camera_identity(side):
    port = dfu.uvc_side_to_usb_port(side)
    usb = Path('/sys/bus/usb/devices')/port
    serial = (usb/'serial').read_text().strip()
    output = run([BASE/'bin/main_camera_xu_tool','--device',f'/dev/cam_{side}','--read-sn'])
    # Preserve exact SN response and USB identity across a partial upgrade.
    return {'port':port,'serial':serial,'sn':output.strip(), 'packet':(usb/'bMaxPacketSize0').read_text().strip()}

def camera_regression(before, after):
    # iSerial contains the firmware label and legitimately changes on upgrade.
    # The DFU adapter separately verifies the image's version against the device.
    fields = ('port', 'sn', 'packet')
    changed = [key for key in fields if not before.get(key) or before.get(key) != after.get(key)]
    return {'ok': not changed, 'changed': changed, 'before': before, 'after': after}


def camera_event(side, event):
    with (PRIVATE / (JOB['id'] + '.log')).open('a') as log:
        log.write(json.dumps({'side': side, **event}, ensure_ascii=False) + '\n')
    if event.get('type') == 'state':
        report(f'{side}: {event["message"]}')


def execute(kind, path, force=False, side=None, settings=None):
    if kind == 'configuration':
        report('保存设备配置')
        return write_settings(settings)
    if kind == 'software':
        report('检查软件包')
        fields = run(['dpkg-deb','-f',path,'Package','Architecture','Version']).splitlines()
        info = dict(line.split(': ',1) for line in fields if ': ' in line)
        if info.get('Package') not in ALLOW or info.get('Architecture') not in ('arm64','all'):
            raise ValueError('软件包名称或架构不在允许范围')
        run(['dpkg-deb','--info',path])
        # Offline resolver validates dependency closure before executing any package scripts.
        run(['dpkg','--no-act','--install',path],120)
        report('安装软件', package=info['Package'], target_version=info['Version'])
        (RUN/'self-installing').touch()
        run(['dpkg','--install',path],600)
        installed = run(['dpkg-query','-W','-f=${Status}\t${Version}',info['Package']]).strip()
        if installed != 'install ok installed\t'+info['Version']: raise RuntimeError('安装后版本校验失败')
        return {'package':info['Package'],'version':info['Version']}
    if kind == 'main_camera':
        report('检查左右主摄和固件')
        if not all(Path(f'/dev/cam_{s}').exists() for s in ('left','right')):
            raise ValueError('左右主摄必须全部在线')
        if not os.access(BASE/'bin/dfu-util',os.X_OK): raise ValueError('包内 DFU 不可用')
        image_version = dfu.read_image_version(path,version_reader=BASE/'bin/linux_read_version')
        before = {s:camera_identity(s) for s in ('left','right')}
        result = {}
        for side in ('left','right'):
            def regression(s):
                after = camera_identity(s)
                return camera_regression(before[s], after)
            updater = dfu.MainCameraDfuUpdater(firmware_dir=path.parent,
                version_reader=BASE/'bin/linux_read_version', dfu_trigger=BASE/'bin/main_camera_dfu_trigger',
                dfu_util=BASE/'bin/dfu-util', read_device_version=camera_version, regression_check=regression,
                event_sink=lambda e: camera_event(side, e))
            try:
                result[side] = updater.upgrade(side,path,force=force).to_dict()
            except dfu.DfuError as exc:
                result[side] = {'status': 'failed', **exc.to_dict()}
                report('单侧失败', results=dict(result))
                raise
            report('单侧完成', results=dict(result))
        return result
    if kind == 'gripper':
        validate_hex(path)
        if not all(Path(f'/dev/{s}_gripper').exists() for s in ('left','right')):
            raise ValueError('左右夹爪必须全部在线')
        for s in ('left','right'):
            args=[PYTHON,BASE/'production/ota_update.py','--port',f'/dev/{s}_gripper','--check-only']
            probe=command(args,20)
            with (PRIVATE/(JOB['id']+'.log')).open('a') as log: log.write(probe.stdout+probe.stderr)
            # Older application firmware acknowledges RV as unsupported (valid fixed NACK).
            # It can still accept OTA. No response or an unrelated frame remains a hard failure.
            if probe.returncode and 'a5 a5 52 f2 00 00 a5 05' not in probe.stdout:
                raise RuntimeError(f'{s} 夹爪没有有效版本响应')
        result = {}
        for side in ('left','right'):
            report(f'升级{side}夹爪')
            output = run([PYTHON,BASE/'production/ota_update.py','--port',f'/dev/{side}_gripper','--file',path],180)
            version = re.findall(r'升级后版本\s*:\s*(\S+)',output)
            if not version or '两次查询一致' not in output: raise RuntimeError('夹爪版本回读不稳定')
            result[side] = version[-1]
            report('单侧完成', results=dict(result))
        if result['left'] != result['right']: raise RuntimeError('左右夹爪升级后版本不一致')
        return result
    if kind == 'zeroing':
        if side not in ('left','right'): raise ValueError('必须指定左侧或右侧编码器')
        if not Path(f'/dev/{side}_encoder').exists(): raise ValueError('所选编码器未在线')
        report(f'标零 {side}')
        out=run([BASE/'bin/zeroing',side],120)
        if 'Last Pos: unavailable' in out or '[SUCCESS]' not in out:
            raise RuntimeError('标零后没有有效位置回读')
        return {side:'success'}
    raise ValueError('未知维护操作')

def worker(kind, path, stack, force=False, side=None, settings=None):
    active=[]
    try:
        # Guard survives postinst attempts to start the recording service: its flock blocks starts.
        for service in ('ugripper-network-monitor.service','ugripper.service'):
            if command(['systemctl','is-active',service]).stdout.strip() in ('active','activating','reloading'):
                active.append(service)
                run(['systemctl','stop',service],45)
        report('正在执行', status='running')
        result=execute(kind,path,force=force,side=side,settings=settings)
        report('操作完成',status='success',result=result)
    except Exception as exc:
        report('操作失败',status='failed',message=str(exc))
        with (PRIVATE/(JOB['id']+'.log')).open('a') as f: f.write(repr(exc)+'\n')
    finally:
        cleaned = False
        try:
            if path: path.unlink(missing_ok=True)
            cleaned = True
            restore=[]
            for service in reversed(active):
                try:
                    if command(['systemctl','start',service],45).returncode: restore.append(service)
                except Exception: restore.append(service)
            if restore: report('服务恢复失败',status='failed',message=', '.join(restore))
            report(JOB['stage'], finished=time.time(), upload_cleaned=cleaned)
        except Exception as exc:
            try: report('清理或恢复失败',status='failed',message=str(exc),upload_cleaned=cleaned,finished=time.time())
            except Exception: pass
        finally:
            (RUN/'self-installing').unlink(missing_ok=True)
            stack.close()
            MUTEX.release()
        # Self-upgrade must not kill this worker before dpkg and cleanup complete.
        if kind=='software': command(['systemctl','restart','--no-block','ugripper-maintenance.service'])


def submit(req):
    kind=req.get('kind')
    if kind not in ('software','main_camera','gripper','zeroing','configuration'): raise ValueError('未知任务')
    if kind=='zeroing' and req.get('fixture')!='FIXTURE_READY': raise ValueError('必须确认已安装指定标零治具')
    if kind=='zeroing' and req.get('side') not in ('left','right'): raise ValueError('必须指定左侧或右侧编码器')
    if kind=='configuration': validate_settings(req.get('settings'))
    if not MUTEX.acquire(False): raise Busy('已有维护任务执行中')
    stack=contextlib.ExitStack();path=None
    try:
        stack.enter_context(operation_lock())
        usb=stack.enter_context(open('/run/usb_auto_update.lock','a'))
        try: fcntl.flock(usb,fcntl.LOCK_EX|fcntl.LOCK_NB)
        except BlockingIOError: raise Busy('U 盘升级正在执行')
        jobid=uuid.uuid4().hex
        if kind in ('software','main_camera','gripper'):
            token=req.get('upload','')
            if not re.fullmatch(r'[0-9a-f]{32}',token): raise ValueError('无效上传编号')
            source=STATE/'uploads'/token
            fd=os.open(source,os.O_RDONLY|os.O_NOFOLLOW|os.O_NONBLOCK)
            with os.fdopen(fd,'rb') as src:
                info=os.fstat(src.fileno())
                if not stat.S_ISREG(info.st_mode) or not 0<info.st_size<MAX_UPLOAD: raise ValueError('文件过大或格式错误')
                if shutil.disk_usage(PRIVATE).free < info.st_size*3+64*1024*1024: raise ValueError('系统盘可用空间不足')
                path=PRIVATE/(jobid+{'software':'.deb','main_camera':'.img','gripper':'.hex'}[kind])
                with path.open('xb') as dst:
                    remaining=info.st_size
                    while remaining:
                        chunk=src.read(min(remaining,1024*1024))
                        if not chunk: raise ValueError('上传文件发生变化')
                        dst.write(chunk); remaining-=len(chunk)
                    if src.read(1): raise ValueError('上传文件发生变化')
                if path.stat().st_size!=info.st_size: raise ValueError('上传文件发生变化')
            source.unlink(missing_ok=True)
            if kind=='gripper': validate_hex(path)
        JOB.clear();JOB.update(id=jobid,kind=kind,status='running',started=time.time())
        if path: JOB['sha256']=dfu.sha256_file(path)
        report('已接收')
        threading.Thread(target=worker,args=(kind,path,stack,req.get("force") is True,req.get("side"),req.get("settings")),daemon=False).start()
        return {'id':jobid}
    except Exception:
        if path: path.unlink(missing_ok=True)
        stack.close();MUTEX.release();raise

class Handler(socketserver.StreamRequestHandler):
    def handle(self):
        try:
            self.request.settimeout(10)
            req=json.loads(self.rfile.readline(4096))
            if req.get('action')=='submit': value=submit(req)
            elif req.get('action')=='tactile_reset':
                from tactile_panel import reset_baseline
                from camera_snapshot import SNAPSHOTS
                job=req.get('id','')
                if not isinstance(job,str) or not re.fullmatch(r'[0-9a-f]{32}',job): raise ValueError('任务无效')
                if req.get('confirm') is not True: raise ValueError('请确认盖板正常且无接触')
                with operation_lock():
                    value=reset_baseline(SNAPSHOTS/job,req.get('camera'),req.get('scope'),req.get('serial'))
            elif req.get('action')=='snapshot':
                from camera_snapshot import submit_snapshot
                value=submit_snapshot(req.get('kind'))
            elif req.get('action')=='logs':
                # Restricted journal export to a fixed file; never accept a path or command.
                value={'journal':command(['journalctl','-u','ugripper.service','-u','ugripper-webserver.service','-u','ugripper-maintenance.service','-n','5000','--no-pager'],20).stdout[-512*1024:]}
                for log in sorted(PRIVATE.glob('*.log'), key=lambda p:p.stat().st_mtime)[-5:]:
                    value['journal'] += '\n'+log.name+'\n'+log.read_text(errors='replace')[-32000:]
            else: raise ValueError('不支持的请求')
            response=dict(ok=True,**value)
        except Exception as exc: response=dict(ok=False,error=str(exc),busy=isinstance(exc,Busy))
        self.wfile.write(json.dumps(response).encode()+b'\n')

if __name__=='__main__':
    PRIVATE.mkdir(mode=0o700,exist_ok=True)
    from camera_snapshot import SNAPSHOTS
    shutil.rmtree(SNAPSHOTS, ignore_errors=True)
    for name in ('web_reset_request.json','web_reset_response.json','web_reset.gray'):
        Path('/var/lib/ugripper/tactile_state',name).unlink(missing_ok=True)
    (RUN/'self-installing').unlink(missing_ok=True)
    # Old uploads are never resumed after interruption; firmware outcome stays explicit.
    old=read_json(STATE/'task.json')
    if old.get('status')=='running':
        old.update(status='failed',stage='维护服务中断',message='请检查设备后重新执行，未自动重试烧录')
        atomic_json(STATE/'task.json',old)
    for p in PRIVATE.iterdir():
        if p.suffix in ('.img','.hex','.deb'): p.unlink()
    address=RUN/'maintenance.sock';address.unlink(missing_ok=True)
    with socketserver.ThreadingUnixStreamServer(str(address),Handler) as srv:
        os.chown(address,0,pwd.getpwnam('ubuntu').pw_gid);os.chmod(address,0o660)
        srv.serve_forever()
