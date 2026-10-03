#!/usr/bin/env python3
"""Explicit one-shot ec0c099 X4 pair test. Offline preparation is the default.

Never dispatched by the scheduler. --approval-sha is only supplied after actual
human approval; it does not infer consent from a validated artifact. Cleanup
always targets the approved heartbeat app, retaining the new module store.
"""
import argparse
import fcntl
import hashlib
import json
from pathlib import Path
import re
import signal
import sys
import time

import x4_deployment_plan as plan

SOURCE = 'ec0c09991f8f7babc69b3da0681b0b3f1e99c3ab'
ARCHIVE = '7297963f921e1f879a48dbe0a9492664ca841e1f5c499f2c3fdbbc6894d7f099'
FIRMWARE = '608de27d168101cb41742463b0704e9fc52706b8fb0cd850d17d0937b19468be'
STORE = '41602792e679248568b8ea88e4dc8a0d2c02fd8730e573d978ba8cd98f53cc72'
BACKUP = 'dcf62b9b64da4f61a20ff9a05039cc31a1786d15918d1d5646e0f5d51fbcd7b5'
HEARTBEAT = '11df3398023cc3d3dddfc3fd6c8e3c78445f6959434a636ee51623247a49dcd6'
PREFIX = 'fbf4e73554f83ee2a880de711d4c462c2f961e1612f74eb9cee4a2cf9d13c9d4'
PHASES = ('elf-relocate-begin','elf-relocated','hardware-start-begin','hardware-started')


def require(ok, why):
    if not ok: raise ValueError(why)


def digest(data): return hashlib.sha256(data).hexdigest()


def prepare(artifact):
    require(artifact.is_file() and not artifact.is_symlink() and artifact.stat().st_size<=plan.MAX_ARCHIVE,'Artifact path/size invalid')
    raw=artifact.read_bytes();require(digest(raw)==ARCHIVE,'Unapproved artifact digest')
    files=plan.archive_files(raw,32,plan.APP_SIZE,plan.MAX_EXPANDED)
    m=plan.read_json(files['deployment.json'])
    require(m['source_sha']==SOURCE and m['board']=='xteink-x4-pro','Unapproved source/board')
    require(digest(files['firmware.bin'])==FIRMWARE and len(files['firmware.bin'])==5948464,'Unapproved firmware')
    require(digest(files['module-store.bin'])==STORE and len(files['module-store.bin'])==plan.STORE_SIZE,'Unapproved store')
    boot=plan.read_json(files['deployment.json'])
    expected={}
    for r in boot['packages']:
        if r['id']=='x4pro-battery': continue
        p=plan.package_files(files['packages/'+r['file']],r)
        expected[r['id']]={'version':r['version'],'bytes':len(p['driver.elf']),
            'origin':'/bootfs/Drivers/'+r['id']+'/driver.elf'}
    require(len(expected)==7,'Selected driver inventory changed')
    return files,expected


def assess(lines, expected, validate_home):
    require(not any(x in row for row in lines for x in ('Guru Meditation','Backtrace:','abort()','Task watchdog','task_wdt','failure=')),'Candidate panic/provider failure')
    home=validate_home(lines)
    origins={};phases={};milestones={}
    for row in lines:
        stamp=re.match(r'^\[(\d+)\]',row);ms=int(stamp[1]) if stamp else None
        m=re.search(r'\[BOOTFS\] registered origin=(\S+) id=([a-z0-9-]+) version=([0-9.]+) bytes=(\d+)$',row)
        if m:
            origin,identity,version,size=m.groups()
            require(identity in expected and identity not in origins,'Unexpected/duplicate external provider')
            require({'origin':origin,'version':version,'bytes':int(size)}==expected[identity],'External provider origin/version/length mismatch')
            origins[identity]={'origin':origin,'version':version,'bytes':int(size),'device_ms':ms}
        m=re.search(r'PROVREF id=([a-z0-9-]+) stage=([a-z-]+)$',row)
        if m and m[1] in expected and m[2] in PHASES:
            phases.setdefault(m[1],{}).setdefault(m[2],[]).append(ms)
        for label in ('diagnostic entered','storage.volume mounted=1','input.touch ready=1','boot splash present=1','home activity scheduled=1','home present=1'):
            if '[X4] '+label in row: milestones.setdefault(label,[]).append(ms)
    require(set(origins)==set(expected),'Missing external provider registrations')
    for identity in expected:
        stages=phases.get(identity,{})
        require(all(len(stages.get(p,[]))==1 for p in PHASES),'Provider activation phases incomplete or repeated')
        stamps=[stages[p][0] for p in PHASES]
        require(all(x is not None for x in stamps) and stamps==sorted(stamps),'Provider phase timestamps invalid')
        stages['relocate_ms']=stamps[1]-stamps[0];stages['hardware_start_ms']=stamps[3]-stamps[2]
    require(len(milestones.get('input.touch ready=1',[]))==1,'Touch startup missing')
    ready=[int(m[1]) for row in lines if (m:=re.search(r'\[X4\] heartbeat ready=([01])',row))]
    require(1 in ready and all(x==1 for x in ready[ready.index(1):]),'Readiness regressed')
    return {'external_providers':origins,'provider_phase_timing':phases,'device_milestones_ms':milestones,'shared_home':home}


def make_transport(h, binding, folder, timings):
    class PairedTransport(h.Transport):
        def command(self,label,*args,**kwargs):
            start=time.monotonic()
            try:return super().command(label,*args,**kwargs)
            finally:timings.append({'phase':label,'host_elapsed_ms':round((time.monotonic()-start)*1000)})

        def observe(self,seconds):
            import serial
            port=self.port();pending=bytearray();lines=[];count=0
            handle=serial.Serial(port=None,baudrate=115200,timeout=.5,write_timeout=5,exclusive=True)
            handle.dtr=handle.rts=False;handle.port=port
            with handle:
                end=time.monotonic()+seconds
                while time.monotonic()<end:
                    require(self.port()==port,'USB changed during observation')
                    chunk=handle.read(2048);count+=len(chunk);require(count<=1048576,'Serial byte bound exceeded');pending.extend(chunk)
                    while b'\n' in pending:
                        row,_,pending=pending.partition(b'\n');row=row.decode('utf-8','replace').strip()
                        require(len(row)<=2048 and len(lines)<2000,'Serial line bound exceeded')
                        if any(s in row for s in ('RTE_HEARTBEAT ','[BOOTFS]','[X4]','PROVREF ','Guru Meditation','Backtrace:','abort()','Task watchdog','task_wdt')):lines.append(row)
                    require(len(pending)<=2048,'Unterminated serial line')
            self.serial_bytes+=count
            return lines
    return PairedTransport(binding,'x4',folder)


def cycle(h,t,folder,firmware,store,heartbeat,expected,validate_home):
    result={'source_sha':SOURCE,'result':'failed','cleanup_policy':'heartbeat; retain candidate store; preserve original backup','timings':t.timings if hasattr(t,'timings') else []}
    write_attempted=False;prefix=None;baseline_confirmed=False
    try:
        t.identity();prefix=t.read('protected-before',0,0x10000)
        require(digest(prefix)==PREFIX,'Protected prefix differs from reviewed baseline')
        h.check_layout(prefix,'x4',5951488)
        require(digest(t.read('baseline-before',0x10000,247936))==HEARTBEAT,'Current app is not approved heartbeat')
        baseline_confirmed=True
        prior=t.read('store-before',plan.STORE_OFFSET,plan.STORE_SIZE)
        require(digest(prior)==BACKUP,'Existing store changed; preserve/review it before replacement')
        private=folder/'original-store.bin';private.write_bytes(prior);private.chmod(0o600)
        result['original_store_sha256']=BACKUP
        result['write_attempted']=write_attempted=True;h.save(folder/'result.json',result)
        # Stay in ROM until both bounded regions independently verify.
        t.identity()
        t.command('paired-write','write-flash','--flash-mode','keep','--flash-freq','keep','--flash-size','keep',
                  '0x10000',firmware,'0xc90000',store,timeout=600)
        t.command('paired-verify','verify-flash','0x10000',firmware,'0xc90000',store,timeout=600)
        result['paired_readback_equal']=True;h.save(folder/'result.json',result)
        lines=t.boot(90)
        result['candidate_checks']=assess(lines,expected,validate_home)
        result['result']='pass'
    except BaseException as error:
        result['error']=f'{type(error).__name__}: {error}'[:400]
    finally:
        if write_attempted:
            try:
                require(t.read('protected-pre-cleanup',0,0x10000)==prefix,'Protected region changed; cleanup write refused')
                t.write('heartbeat-cleanup',heartbeat)
                result['heartbeat_readback_equal']=True
                require(t.read('protected-after',0,0x10000)==prefix,'Protected region changed')
                result['protected_equal']=True
                # Boot heartbeat even if a failed pair left an incomplete store.
                result['heartbeat_health']=h.healthy(t.boot(10),'x4');result['heartbeat_restored']=True
                retained=t.read('store-after',plan.STORE_OFFSET,plan.STORE_SIZE)
                result['retained_store_sha256']=digest(retained)
                require(digest(retained)==STORE,'Candidate store incomplete or changed; backup retained for recovery')
                # read-flash leaves ROM active: return to heartbeat once more.
                result['final_heartbeat_health']=h.healthy(t.boot(10),'x4')
            except BaseException as error:
                result['cleanup_error']=f'{type(error).__name__}: {error}'[:400];result['result']='failed'
                # A failed store read/check can leave ROM active; try bounded baseline boot.
                if result.get('heartbeat_readback_equal'):
                    try:result['final_heartbeat_health']=h.healthy(t.boot(10),'x4')
                    except BaseException as e:result['final_boot_error']=str(e)[:400]
        elif baseline_confirmed:
            try:result['final_heartbeat_health']=h.healthy(t.boot(10),'x4')
            except BaseException as e:result['final_boot_error']=str(e)[:400]
        h.save(folder/'result.json',result)
    return result


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--artifact',type=Path,required=True);p.add_argument('--runtime-root',type=Path)
    p.add_argument('--approval-sha');p.add_argument('--out',type=Path)
    a=p.parse_args();files,expected=prepare(a.artifact)
    if a.approval_sha is None:
        print(json.dumps({'mode':'offline-prepared','source_sha':SOURCE,'firmware_sha256':FIRMWARE,'store_sha256':STORE,'expected':expected,'hardware_access_performed':False},indent=2));return 0
    require(a.approval_sha==SOURCE,'Approval must identify the exact frozen source')
    require(a.runtime_root and a.out,'Explicit runtime root and fresh evidence directory required')
    config=json.loads((a.runtime_root/'private/config.json').read_text())
    for name,value in config['pins'].items():require(digest((a.runtime_root/'pinned'/name).read_bytes())==value,'Installed runtime pin mismatch')
    sys.path.insert(0,str((a.runtime_root/'pinned').resolve()))
    import heartbeat_policy as h
    sys.path.insert(0,str((a.runtime_root/'pinned/x4').resolve()))
    from x4_ci_device import validate_boot
    job=next(j for j in config['jobs'] if j['target']=='x4')
    require(not Path(job['pause']).exists(),'Manual device ownership pause present')
    baseline=Path(job['heartbeat']);require(digest(baseline.read_bytes())==HEARTBEAT,'Baseline file changed')
    with (a.runtime_root/'private/evidence/scheduler.lock').open('a+') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
        require(not Path(job['pause']).exists(),'Manual ownership changed')
        binding=json.loads(Path(job['binding']).read_text());a.out.mkdir(mode=0o700,parents=True,exist_ok=False)
        timings=[];t=make_transport(h,binding,a.out,timings);t.timings=timings
        fw=a.out/'firmware.bin';fw.write_bytes(files['firmware.bin'])
        store=a.out/'module-store.bin';store.write_bytes(files['module-store.bin'])
        hb=a.out/'heartbeat.bin';hb.write_bytes(baseline.read_bytes())
        def interrupted(signum,frame):raise KeyboardInterrupt('Termination requested')
        signal.signal(signal.SIGTERM,interrupted)
        with h.locks(t.port(),h.BOARDS['x4'][0]):result=cycle(h,t,a.out,fw,store,hb,expected,validate_boot)
        print(json.dumps(result,indent=2));return 0 if result['result']=='pass' else 1

if __name__=='__main__':raise SystemExit(main())
