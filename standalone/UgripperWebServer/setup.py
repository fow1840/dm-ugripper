#!/usr/bin/env python3
"""Idempotent package setup; rebuild damaged settings and certificates in place."""
import json
import os
from pathlib import Path
import pwd
import ssl
import subprocess

user=pwd.getpwnam('ubuntu')
for path,uid,gid,mode in [('/etc/ugripper-web',0,user.pw_gid,0o750),('/var/lib/ugripper-web',0,user.pw_gid,0o755),('/var/lib/ugripper-web/uploads',user.pw_uid,user.pw_gid,0o700),('/var/lib/ugripper-web/upgrade-packages',0,user.pw_gid,0o770),('/var/lib/ugripper-maintenance',0,0,0o700),('/run/ugripper-web',0,user.pw_gid,0o750)]:
    p=Path(path);p.mkdir(parents=True,exist_ok=True);os.chown(p,uid,gid);os.chmod(p,mode)
config=Path('/etc/ugripper-web/config.json')
try:
    settings=json.loads(config.read_text())
except (FileNotFoundError,json.JSONDecodeError,UnicodeDecodeError):
    settings={}
if not isinstance(settings,dict):
    settings={}
settings.setdefault('bind','192.168.2.240');settings.setdefault('port',8080)
# Migrate password-based installations to anonymous wired access.
settings.pop('salt',None);settings.pop('password_hash',None)
tmp=config.with_suffix('.tmp')
with tmp.open('w') as stream:
    json.dump(settings,stream)
    stream.flush();os.fsync(stream.fileno())
os.chown(tmp,0,user.pw_gid);os.chmod(tmp,0o640)
tmp.replace(config)
Path('/etc/ugripper-web/initial-password').unlink(missing_ok=True)
crt=Path('/etc/ugripper-web/server.crt');key=crt.with_suffix('.key')
context=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
try:
    context.load_cert_chain(crt,key)
except (FileNotFoundError,ssl.SSLError):
    subprocess.run(['/usr/bin/openssl','req','-x509','-newkey','rsa:2048','-nodes','-days','3650','-subj','/CN=UGripper','-addext',f"subjectAltName=IP:{settings['bind']}",'-keyout',str(key),'-out',str(crt)],check=True,capture_output=True,timeout=30)
    context.load_cert_chain(crt,key)
for path in (crt,key):
    os.chown(path,0,user.pw_gid);os.chmod(path,0o640)
lock=Path('/run/ugripper/maintenance.lock');lock.touch(exist_ok=True);os.chown(lock,user.pw_uid,user.pw_gid);os.chmod(lock,0o660)

lock=Path('/run/ugripper/production.lock');lock.touch(exist_ok=True);os.chown(lock,user.pw_uid,user.pw_gid);os.chmod(lock,0o660)
