#!/usr/bin/env python3
"""Login-free Ethernet HTTPS interface with per-operation maintenance protection."""
import contextlib
import fcntl
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import re
import secrets
import socket
import ssl
import stat
import tarfile
import tempfile
import threading
import time
from urllib.parse import urlsplit, parse_qs, quote
from common import BASE, STATE, DATA, MAX_UPLOAD, Busy, read_json, rpc, operation_lock
from common import ugripper_log, log_exception
from telemetry import status
from encoder_probe import read_encoders, stream_encoders

CONFIG={}
MAINTENANCE_PASSWORD_SHA256='6d809b4f1a434d31097ae94f22991f7e8bcb93b116e92f031292f59f9b800d63'

def open_data(relative):
    """Walk a mounted data root using openat; never follow a path symlink."""
    if not os.path.ismount(DATA): raise ValueError('数据盘未挂载')
    parts=relative.split('/') if relative else []
    if any(p in ('','.','..') or p.endswith('-temp') for p in parts): raise ValueError('不允许访问该路径')
    fd=os.open(DATA,os.O_RDONLY|os.O_DIRECTORY|os.O_NOFOLLOW)
    try:
        for part in parts:
            nxt=os.open(part,os.O_RDONLY|os.O_NOFOLLOW|os.O_NONBLOCK,dir_fd=fd)
            os.close(fd);fd=nxt
        return fd
    except Exception:
        os.close(fd);raise

def add_archive(tar, fd, name):
    info=os.fstat(fd)
    if stat.S_ISREG(info.st_mode):
        member=tarfile.TarInfo(name);member.size=info.st_size;member.mtime=info.st_mtime;member.mode=0o644
        with os.fdopen(os.dup(fd),'rb') as stream: tar.addfile(member,stream)
    elif stat.S_ISDIR(info.st_mode):
        for child in os.listdir(fd):
            if child.endswith('-temp'): continue
            try: cfd=os.open(child,os.O_RDONLY|os.O_NOFOLLOW|os.O_NONBLOCK,dir_fd=fd)
            except OSError: continue
            try: add_archive(tar,cfd,name+'/'+child)
            finally: os.close(cfd)

class Handler(BaseHTTPRequestHandler):
    server_version='UGripper'
    def log_request(self,code='-',size='-'):
        # Successful browser polling is not a service event.
        if isinstance(code,int) and code >= 400:
            ugripper_log('WARNING', f'HTTP {code} {urlsplit(self.path).path}', component='web')
    def log_message(self,fmt,*args):
        ugripper_log('INFO', fmt % args, component='web')
    def log_error(self,fmt,*args):
        ugripper_log('ERROR', fmt % args, component='web', error_code='UGR-00-01')
    def setup(self):
        super().setup();self.connection.settimeout(45)
        self.connection.do_handshake()
    def headers_out(self,code,content,length=None,extra=None):
        self.send_response(code)
        self.send_header('Content-Type',content)
        self.send_header('Cache-Control','no-store')
        self.send_header('X-Content-Type-Options','nosniff')
        self.send_header('X-Frame-Options','DENY')
        self.send_header('Content-Security-Policy',"default-src 'self'; script-src 'self'; style-src 'self'; frame-ancestors 'none'; base-uri 'none'")
        if length is not None:self.send_header('Content-Length',str(length))
        for k,v in (extra or {}).items():self.send_header(k,v)
        self.end_headers()
    def json(self,value,code=200,extra=None):
        b=json.dumps(value,ensure_ascii=False).encode();self.headers_out(code,'application/json; charset=utf-8',len(b),extra);self.wfile.write(b)
    def body(self,maximum):
        if self.headers.get('Transfer-Encoding'):raise ValueError('不支持分块上传')
        raw=self.headers.get('Content-Length','')
        if not raw.isdecimal():raise ValueError('缺少有效文件大小')
        size=int(raw)
        if not 0<size<maximum:raise ValueError('文件必须小于 200 MiB，且不能为空')
        return size
    def dispatch(self,post=False):
        path=urlsplit(self.path).path
        if not post and path == '/api/v1/tactile/alarm-mask':
            from tactile_panel import alarm_mask_png
            query = parse_qs(urlsplit(self.path).query)
            value = lambda key: query.get(key, [''])[0]
            b = alarm_mask_png(value('id'), value('camera'), value('scope'))
            self.headers_out(200, 'image/png', len(b)); self.wfile.write(b); return
        if not post and path in ('/tactile-normal.png','/tactile-damaged.png'):
            b=(BASE/'static'/path[1:]).read_bytes()
            self.headers_out(200,'image/png',len(b));self.wfile.write(b);return
        if not post and path in ('/','/app.js','/i18n.js','/style.css'):
            name={'/':'index.html','/i18n.js':'i18n.js','/app.js':'app.js','/style.css':'style.css'}[path]
            b=(BASE/'static'/name).read_bytes()
            self.headers_out(200,{'/':'text/html; charset=utf-8','/i18n.js':'text/javascript; charset=utf-8','/app.js':'text/javascript; charset=utf-8','/style.css':'text/css'}[path],len(b));self.wfile.write(b);return
        if post:
            # Public fixed header forces cross-origin browser requests to preflight.
            # No CORS is enabled; this is request-origin protection, not authentication.
            if self.headers.get('X-UGripper-Request')!='1':return self.json({'error':'请求校验失败'},403)
            origin=self.headers.get('Origin')
            if origin and origin!='https://'+self.headers.get('Host',''):return self.json({'error':'请求来源错误'},403)
            if path in ('/api/v1/upload','/api/v1/execute','/api/v1/camera/version-confirm'):
                password=self.headers.get('X-UGripper-Maintenance-Password','')
                digest=hashlib.sha256(password.encode('utf-8')).hexdigest()
                if not secrets.compare_digest(digest,MAINTENANCE_PASSWORD_SHA256):
                    # Reject before reading uploads or dispatching any worker action.
                    self.close_connection=True
                    return self.json({'error':'维护密码错误，请重新输入'},403)
            if path=='/api/v1/upload':return self.upload()
            size=self.body(4096);body=json.loads(self.rfile.read(size))
            if path=='/api/v1/tactile/reset':
                if body.get('confirm') is not True:raise ValueError('请确认盖板正常且无接触')
                return self.json(rpc(dict(body,action='tactile_reset')))
            if path=='/api/v1/snapshot':
                return self.json(rpc(dict(action='snapshot',kind=body.get('kind'))),202)
            if path=='/api/v1/encoder/stream':return self.encoder_stream()
            if path=='/api/v1/time-sync/start':
                return self.json(rpc(dict(action='time_sync_start',settings=body)),202)
            if path=='/api/v1/time-sync/cancel':
                return self.json(rpc(dict(action='time_sync_cancel',id=body.get('id'))))
            if path=='/api/v1/production/start':
                if body.get('confirm') is not True:raise ValueError('请确认维护操作')
                return self.json(rpc(dict(action='production_start')),202)
            if path=='/api/v1/production/cancel':return self.json(rpc(dict(action='production_cancel',id=body.get('id'))))
            if path=='/api/v1/encoder/read':return self.json(read_encoders())
            if path=='/api/v1/camera/version-cancel':
                return self.json(rpc(dict(action='camera_version_confirm',id=body.get('id'),
                                          token=body.get('token'),accepted=False)))
            if path=='/api/v1/camera/version-confirm':
                return self.json(rpc(dict(action='camera_version_confirm',id=body.get('id'),
                                          token=body.get('token'),accepted=body.get('accepted'))))
            if path=='/api/v1/execute':
                if body.get('confirm') is not True:raise ValueError('请确认维护操作')
                try:
                    result=rpc(dict(action='submit',kind=body.get('kind'),upload=body.get('upload'),fixture=body.get('fixture'),side=body.get('side'),settings=body.get('settings'),force=body.get('force') is True))
                finally:
                    token=body.get('upload','')
                    if isinstance(token,str) and re.fullmatch(r'[0-9a-f]{32}',token):
                        (STATE/'uploads'/token).unlink(missing_ok=True)
                return self.json(result,202)
        else:
            if path=='/api/v1/snapshot':return self.snapshot_file()
            if path=='/api/v1/status':return self.json(status())
            if path=='/api/v1/data':return self.list_data()
            if path=='/api/v1/download':return self.download()
            if path=='/api/v1/logs':return self.logs()
        self.json({'error':'接口不存在'},404)
    def do_GET(self):self.safe_dispatch(False)
    def do_POST(self):self.safe_dispatch(True)
    def safe_dispatch(self,post):
        try:self.dispatch(post)
        except Busy as exc:self.json({'error':str(exc)},409)
        except (ValueError,OSError) as exc:
            if isinstance(exc,(BrokenPipeError,ConnectionResetError,socket.timeout)):return
            self.json({'error':str(exc) if isinstance(exc,ValueError) else '文件或服务暂不可用'},400)
        except Exception:
            log_exception('web')
            self.json({'error':'操作失败，请获取日志'},500)
    def encoder_stream(self):
        # A slow or disconnected consumer cannot keep serial ports indefinitely.
        with contextlib.closing(stream_encoders()) as samples:
            first=next(samples)
            self.connection.settimeout(1)
            self.headers_out(200,'application/x-ndjson')
            try:
                self.wfile.write((json.dumps(first)+'\n').encode());self.wfile.flush()
                for sample in samples:
                    self.wfile.write((json.dumps(sample)+'\n').encode());self.wfile.flush()
            except (ValueError, OSError) as exc:
                with contextlib.suppress(OSError):
                    self.wfile.write((json.dumps({'error':str(exc)})+'\n').encode());self.wfile.flush()
            finally: self.close_connection=True

    def snapshot_file(self):
        query=parse_qs(urlsplit(self.path).query)
        job=query.get('id',[''])[0];name=query.get('file',['result.json'])[0]
        if not re.fullmatch(r'[0-9a-f]{32}',job):raise ValueError('抓图任务无效')
        if name!='result.json' and not re.fullmatch(r'[A-Za-z0-9_-]+\.(?:jpg|png)',name):raise ValueError('图片名称无效')
        path=STATE/'snapshots'/job/name
        with path.open('rb') as source:
            data=source.read(16*1024*1024)
        self.headers_out(200,'application/json; charset=utf-8' if name=='result.json' else 'image/png' if name.endswith('.png') else 'image/jpeg',len(data))
        self.wfile.write(data)

    def upload(self):
        size=self.body(MAX_UPLOAD);token=secrets.token_hex(16);path=STATE/'uploads'/token
        with operation_lock():
            free=os.statvfs(STATE).f_bavail*os.statvfs(STATE).f_frsize
            if free<size*3+64*1024*1024:raise ValueError('系统盘空间不足')
            # One outstanding upload per device; expired files cannot accumulate.
            for old in (STATE/'uploads').iterdir():old.unlink(missing_ok=True)
            try:
                with path.open('xb') as stream:
                    os.chmod(path,0o600);remaining=size;digest=hashlib.sha256()
                    while remaining:
                        chunk=self.rfile.read(min(1024*1024,remaining))
                        if not chunk:raise ValueError('上传中断')
                        stream.write(chunk);digest.update(chunk);remaining-=len(chunk)
                self.json(dict(upload=token,size=size,sha256=digest.hexdigest()),201)
            except Exception:path.unlink(missing_ok=True);raise
    def data_path(self):return parse_qs(urlsplit(self.path).query).get('path',[''])[0]
    def list_data(self):
        with operation_lock():
            fd=open_data(self.data_path())
            try:
                entries=[]
                query=parse_qs(urlsplit(self.path).query)
                offset=max(0,int(query.get('offset',['0'])[0]))
                names=sorted(os.listdir(fd),reverse=True)
                for name in names[offset:offset+200]:
                    if name.endswith('-temp'):continue
                    info=os.stat(name,dir_fd=fd,follow_symlinks=False)
                    if stat.S_ISREG(info.st_mode) or stat.S_ISDIR(info.st_mode):
                        entries.append(dict(name=name,size=info.st_size,directory=stat.S_ISDIR(info.st_mode)))
                self.json({'entries':entries,'offset':offset,'has_more':offset+200<len(names)})
            finally:os.close(fd)
    def download(self):
        with operation_lock():
            relative=self.data_path();fd=open_data(relative)
            try:
                info=os.fstat(fd);name=Path(relative).name or 'data'
                if stat.S_ISDIR(info.st_mode):
                    self.headers_out(200,'application/x-tar',extra={'Content-Disposition':"attachment; filename*=UTF-8''"+quote(name+'.tar')})
                    with tarfile.open(fileobj=self.wfile,mode='w|') as tar:add_archive(tar,fd,name)
                elif stat.S_ISREG(info.st_mode):
                    start=0;end=info.st_size-1;code=200
                    request=self.headers.get('Range')
                    if request:
                        m=re.fullmatch(r'bytes=(\d+)-(\d*)',request)
                        if not m:raise ValueError('不支持该下载范围')
                        start=int(m[1]);end=min(int(m[2]),end) if m[2] else end
                        if start>end: return self.json({'error':'下载范围无效'},416)
                        code=206
                    extra={'Content-Disposition':"attachment; filename*=UTF-8''"+quote(name),'Accept-Ranges':'bytes'}
                    if code==206:extra['Content-Range']=f'bytes {start}-{end}/{info.st_size}'
                    self.headers_out(code,'application/octet-stream',end-start+1,extra)
                    os.lseek(fd,start,os.SEEK_SET);remaining=end-start+1
                    while remaining:
                        chunk=os.read(fd,min(1024*1024,remaining))
                        if not chunk:break
                        self.wfile.write(chunk);remaining-=len(chunk)
                else:raise ValueError('不支持该文件类型')
            finally:os.close(fd)
    def logs(self):
        with operation_lock(), tempfile.TemporaryFile() as archive:
            import io
            journal=rpc({'action':'logs'})['journal'].encode()
            with tarfile.open(fileobj=archive,mode='w:gz') as tar:
                member=tarfile.TarInfo('system.log');member.size=len(journal);tar.addfile(member,io.BytesIO(journal))
                folder=Path('/var/log/ugripper')
                if folder.exists():
                    for p in folder.rglob('*.log'):
                        if p.is_symlink() or p.stat().st_size>20*1024*1024:continue
                        try:tar.add(p,arcname='ugripper/'+str(p.relative_to(folder)),recursive=False)
                        except PermissionError:pass
                # Export only the committed immutable set; this read creates no
                # USB files and shares the event export lock during streaming.
                from system_log_snapshot import committed_files
                with open('/tmp/ugripper-system-log.lock','a') as snapshot_lock:
                    fcntl.flock(snapshot_lock,fcntl.LOCK_EX)
                    root=DATA/'system_journal'
                    if os.path.ismount(DATA) and (root/'manifest.json').is_file():
                        manifest=json.loads((root/'manifest.json').read_text())
                        member=tarfile.TarInfo('system_journal/manifest.json')
                        payload=(json.dumps(manifest)+'\n').encode();member.size=len(payload)
                        tar.addfile(member,io.BytesIO(payload))
                        for relative in committed_files(manifest):
                            fd=open_data('system_journal/'+relative)
                            try:add_archive(tar,fd,'system_journal/'+relative)
                            finally:os.close(fd)
            size=archive.tell();archive.seek(0)
            self.headers_out(200,'application/gzip',size,{'Content-Disposition':'attachment; filename="ugripper-logs.tar.gz"'})
            import shutil;shutil.copyfileobj(archive,self.wfile,1024*1024)

class Server(ThreadingHTTPServer):
    daemon_threads=True
    def handle_error(self,request,client_address):
        log_exception('web')
    def __init__(self,*args):self.slots=threading.BoundedSemaphore(8);super().__init__(*args)
    def service_actions(self):
        if time.time()-getattr(self,'last_cleanup',0)<60:return
        self.last_cleanup=time.time()
        for path in (STATE/'uploads').iterdir():
            try:
                if time.time()-path.stat().st_mtime>900:path.unlink(missing_ok=True)
            except OSError:pass
    def get_request(self):
        sock, address = self.socket.accept()
        sock.settimeout(45)
        return self.context.wrap_socket(sock, server_side=True, do_handshake_on_connect=False), address
    def process_request(self,request,client_address):
        if not self.slots.acquire(False):request.close();return
        try:super().process_request(request,client_address)
        except Exception:self.slots.release();raise
    def process_request_thread(self,*args):
        try:super().process_request_thread(*args)
        finally:self.slots.release()

if __name__=='__main__':
    from ugripper_logger import install_exception_logging
    install_exception_logging('web')
    for old in (STATE/'uploads').iterdir(): old.unlink(missing_ok=True)
    CONFIG=read_json('/etc/ugripper-web/config.json')
    # Explicit Ethernet address only, never a wildcard or Wi-Fi listener.
    bind=CONFIG.get('bind','192.168.2.240')
    if bind in ('0.0.0.0','::',''):raise RuntimeError('必须配置指定网口地址')
    context=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER);context.minimum_version=ssl.TLSVersion.TLSv1_2
    context.load_cert_chain('/etc/ugripper-web/server.crt','/etc/ugripper-web/server.key')
    with Server((bind,CONFIG.get('port',8080)),Handler) as srv:
        srv.context=context
        srv.serve_forever()
