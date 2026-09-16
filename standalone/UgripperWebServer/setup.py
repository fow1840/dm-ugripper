#!/usr/bin/env python3
"""Idempotent package setup; keep network settings and certificate across reinstall."""
import json
import os
from pathlib import Path
import pwd
import subprocess

user=pwd.getpwnam('ubuntu')
for path,uid,gid,mode in [('/etc/ugripper-web',0,user.pw_gid,0o750),('/var/lib/ugripper-web',0,user.pw_gid,0o755),('/var/lib/ugripper-web/uploads',user.pw_uid,user.pw_gid,0o700),('/var/lib/ugripper-maintenance',0,0,0o700),('/run/ugripper-web',0,user.pw_gid,0o750)]:
    p=Path(path);p.mkdir(parents=True,exist_ok=True);os.chown(p,uid,gid);os.chmod(p,mode)
config=Path('/etc/ugripper-web/config.json')
settings=json.loads(config.read_text()) if config.exists() else {}
settings.setdefault('bind','192.168.2.240');settings.setdefault('port',8080)
# Migrate password-based installations to anonymous wired access.
settings.pop('salt',None);settings.pop('password_hash',None)
config.write_text(json.dumps(settings))
Path('/etc/ugripper-web/initial-password').unlink(missing_ok=True)
os.chown(config,0,user.pw_gid);os.chmod(config,0o640)
crt=Path('/etc/ugripper-web/server.crt');key=crt.with_suffix('.key')
if not key.exists() or not crt.exists():
    bind=json.loads(config.read_text())['bind']
    subprocess.run(['/usr/bin/openssl','req','-x509','-newkey','rsa:2048','-nodes','-days','3650','-subj','/CN=UGripper','-addext',f'subjectAltName=IP:{bind}','-keyout',str(key),'-out',str(crt)],check=True,capture_output=True)
os.chown(key,0,user.pw_gid);os.chmod(key,0o640)
lock=Path('/run/ugripper/maintenance.lock');lock.touch(exist_ok=True);os.chown(lock,user.pw_uid,user.pw_gid);os.chmod(lock,0o660)

lock=Path('/run/ugripper/production.lock');lock.touch(exist_ok=True);os.chown(lock,user.pw_uid,user.pw_gid);os.chmod(lock,0o660)
