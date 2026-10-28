"""Durable inspection reports and bounded, off-thread joint notifications."""
import json
import os
from pathlib import Path
import re
import threading
import time
import urllib.request
from common import STATE, atomic_json, read_json, ugripper_log

HISTORY = STATE / 'inspection-results'
MAX_REPORTS = 100
RETRY_DELAYS = (1, 2, 5, 10, 30)
WAKE = threading.Event()


def report_path(jobid):
    if not isinstance(jobid,str) or not re.fullmatch('[0-9a-f]{32}',jobid):
        raise ValueError('invalid_task_id')
    return HISTORY / (jobid+'.json')


def store(job):
    # Encoder inspection uses only the fixed latest snapshot, never per-task history.
    if job.get('kind')=='encoder':return
    HISTORY.mkdir(exist_ok=True)
    atomic_json(report_path(job['id']), job)
    reports=sorted(HISTORY.glob('*.json'),key=lambda p:p.stat().st_mtime)
    reports=[p for p in reports if re.fullmatch('[0-9a-f]{32}.json',p.name)]
    for old in reports[:-MAX_REPORTS]:
        old.unlink(missing_ok=True)
        for receipt in HISTORY.glob(old.stem+'.*.delivery'):
            receipt.unlink(missing_ok=True)
    WAKE.set()


def cleanup_encoder_history():
    # Startup only, before callback dispatch or inspection workers are started.
    for path in HISTORY.glob('*.json'):
        if re.fullmatch(r'[0-9a-f]{32}\.json',path.name) and read_json(path).get('kind')=='encoder':
            path.unlink(missing_ok=True)
            for receipt in HISTORY.glob(path.stem+'.*.delivery'):
                receipt.unlink(missing_ok=True)
    # Atomic staging files are uncommitted and have no active writers at startup.
    for path in HISTORY.glob('*.new'):
        path.unlink(missing_ok=True)


def lookup(jobid):
    return read_json(report_path(jobid))


def compact(job):
    if job.get('kind')!='trajectory':return job
    state=job.get('status')
    if state=='running':state=job.get('phase','starting')
    if state=='interrupted':state='failed'
    return dict(id=job['id'],status=state,episode=job.get('episode'),code=job.get('code'))


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


def deliver_once(now=None):
    url=os.environ.get('UGRIPPER_INSPECTION_CALLBACK_URL','')
    if not url:return
    now=time.time() if now is None else now
    for path in sorted(HISTORY.glob('*.json')):
        job=read_json(path)
        if not re.fullmatch('[0-9a-f]{32}',str(job.get('id',''))):continue
        for label,event in job.get('events',{}).items():
            if label not in ('started','finished'):continue
            receipt=HISTORY/(job['id']+'.'+label+'.delivery')
            state=read_json(receipt)
            attempt=state.get('attempts',0)
            if state.get('delivered'):continue
            # Preserve start-before-finish ordering, including across worker restarts.
            if label=='finished' and 'started' in job.get('events',{}):
                start=read_json(HISTORY/(job['id']+'.started.delivery'))
                if not start.get('delivered') and start.get('attempts',0)<len(RETRY_DELAYS):continue
            if attempt>=len(RETRY_DELAYS) or now<state.get('next_at',0):continue
            next_state=dict(attempts=attempt+1,next_at=now+RETRY_DELAYS[attempt],delivered=False)
            # Reserve attempt before networking: process death cannot create an unbounded retry loop.
            atomic_json(receipt,next_state)
            try:
                headers={'Content-Type':'application/json'}
                token=os.environ.get('UGRIPPER_INSPECTION_CALLBACK_TOKEN','')
                if token:headers['Authorization']='Bearer '+token
                request=urllib.request.Request(url,data=json.dumps(event).encode(),headers=headers,method='POST')
                with urllib.request.build_opener(NoRedirect()).open(request,timeout=3) as response:
                    if response.status not in (200,204):raise ValueError('callback_not_acknowledged')
                next_state.update(delivered=True)
            except Exception:
                # Do not log the configured URL or token.
                if attempt+1==len(RETRY_DELAYS):
                    ugripper_log('WARNING',f'inspection notification exhausted id={job["id"]} event={label}',component='maintenance')
            atomic_json(receipt,next_state)


def run():
    while True:
        try:deliver_once()
        except Exception:
            ugripper_log('WARNING','inspection notification dispatch failed',component='maintenance')
        WAKE.wait(1);WAKE.clear()


def start_dispatcher():
    threading.Thread(target=run,name='inspection-notifications',daemon=True).start()
