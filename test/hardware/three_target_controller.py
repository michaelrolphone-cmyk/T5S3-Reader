"""Pinned exact-head controller with separate targets and heartbeat cleanup.

Run only an independently reviewed installed copy with private configuration.
Build artifacts are data only. Missing, paused or busy boards are never replaced
by another board. This scheduler has no repository checkout or self-update path.
"""
import argparse
import fcntl
import hashlib
import io
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time
import zipfile

sys.path.insert(0,str(Path(__file__).parent/'cam'))
import trusted_controller as github
import heartbeat_policy as device
from device_locks import locks

WORKFLOW='three-target-build.yml'
MAX_ARCHIVE=14_000_000

def private_json(path):
    device.require(path.is_file() and not path.is_symlink() and path.stat().st_size<=16384
                   and not path.stat().st_mode&0o077,'Configuration must be private regular JSON')
    return json.loads(path.read_text())

def candidate(gh,number,sha,target):
    repository=gh.call('')
    workflow=gh.call('/actions/workflows/'+WORKFLOW)
    device.require(workflow['path']=='.github/workflows/'+WORKFLOW,'Wrong build workflow')
    data=gh.call(f'/actions/workflows/{WORKFLOW}/runs?event=pull_request&head_sha={sha}&per_page=100')
    device.require(data['total_count']<=100,'Workflow history exceeds bound')
    runs=[r for r in data['workflow_runs'] if r['head_sha']==sha and r['event']=='pull_request'
          and r['workflow_id']==workflow['id'] and r['repository']['id']==repository['id']
          and r['head_repository']['id']==repository['id'] and r['actor']['login']==github.OWNER
          and r.get('triggering_actor',r['actor'])['login']==github.OWNER
          and any(p['number']==number and p['head']['sha']==sha and p['head']['repo']['id']==repository['id']
                  and p['base']['repo']['id']==repository['id'] for p in r.get('pull_requests',[]))]
    if not runs: return None
    device.require(len(runs)==1,'Ambiguous exact-head build')
    run=runs[0]
    if run['status']!='completed': return None
    # A failed sibling matrix job does not erase a completed target artifact.
    artifacts=gh.call(f"/actions/runs/{run['id']}/artifacts?per_page=100")
    device.require(artifacts['total_count']<=100,'Artifact count exceeds bound')
    matches=[a for a in artifacts['artifacts'] if a['name']==f'ci-target-{target}-{sha}' and not a['expired']
             and 0<a['size_in_bytes']<=MAX_ARCHIVE]
    device.require(len(matches)==1,'Target build artifact absent or ambiguous')
    return run,matches[0]

def unpack(gh,run,artifact,sha,target,folder):
    raw=gh.call(f"/actions/artifacts/{artifact['id']}/zip",limit=MAX_ARCHIVE)
    with zipfile.ZipFile(io.BytesIO(raw)) as archive:
        entries=archive.infolist()
        device.require(len(entries)==3 and {x.filename for x in entries}=={'candidate.bin','heartbeat.bin','manifest.json'},'Unexpected archive entries')
        device.require(all(not x.is_dir() and (x.external_attr>>16)&0o170000 != 0o120000 and not x.flag_bits&1
                           and 0<x.file_size<=(4096 if x.filename=='manifest.json' else device.BOARDS[target][2]) for x in entries),'Unsafe or oversized entry')
        manifest=json.loads(archive.read('manifest.json'))
        device.require(manifest['schema']==1 and manifest['target']==target and manifest['source_sha']==sha
                       and manifest['run_id']==run['id'] and manifest['run_attempt']==run['run_attempt'],'Artifact provenance mismatch')
        device.require(set(manifest['images'])=={'candidate','heartbeat'},'Image manifest mismatch')
        payloads={name:archive.read(name+'.bin') for name in manifest['images']}
        for name,data in payloads.items():
            info=manifest['images'][name]
            device.require(info['file']==name+'.bin' and info['offset']==0x10000 and info['bytes']==len(data)
                           and info['sha256']==device.digest(data),'Artifact image hash/offset mismatch')
    folder.mkdir(mode=0o700)
    for name,data in payloads.items(): (folder/(name+'.bin')).write_bytes(data)
    device.save(folder/'manifest.json',manifest)
    return manifest

def status(gh,path,result):
    if result.get('status_id'): return
    record=result.get('device',{})
    passed=(record.get('result')=='pass' and record.get('candidate_readback_equal') is True
            and record.get('protected_equal') is True and record.get('heartbeat_restored') is True
            and record.get('heartbeat_readback_equal') is True and isinstance(record.get('heartbeat_health'),dict))
    state='success' if passed else 'failure' if record.get('heartbeat_restored') else 'error'
    response=gh.call('/statuses/'+result['source_sha'],'POST',{
        'context':result['target']+' hardware / heartbeat cleanup','state':state,
        'description':'Candidate verified; heartbeat ready' if passed else 'Hardware failed; inspect private evidence'})
    result.update(status_id=response['id'],status_state=state)
    device.save(path,result)

def maintain(job,root):
    target=job['target']; binding=private_json(Path(job['binding']))
    transport=device.Transport(binding,target,root)
    # First try passive observation under the exact same device locks.
    try:
        with locks(transport.port(),device.BOARDS[target][0]):
            device.healthy(transport.observe(8),target)
            return {'state':'heartbeat-ready'}
    except BlockingIOError:
        return {'state':'busy'}
    except (RuntimeError,OSError):
        pass
    # Reacquire locks and recheck every identity before any corrective write.
    folder=root/'maintenance'/target/str(time.time_ns())
    return device.transaction(target,binding,Path(job['heartbeat']),job['heartbeat_sha256'],folder)

def once(gh,number,job,root):
    target=job['target']; sha=github.eligible_source(gh,number)
    folder=root/'candidates'/target/sha; path=folder/'result.json'
    if path.exists():
        prior=json.loads(path.read_text())
        device.require(prior['target']==target and prior['source_sha']==sha,'Journal identity mismatch')
        status(gh,path,prior)
        return
    device.require(not folder.exists(),'Incomplete candidate journal; complete heartbeat recovery first')
    found=candidate(gh,number,sha,target)
    if found is None: return
    folder.mkdir(mode=0o700,parents=True)
    result={'schema':1,'source_sha':sha,'target':target,'pr':number,'result':'failed'}
    try:
        run,artifact=found
        manifest=unpack(gh,run,artifact,sha,target,folder/'artifact')
        result.update(run_id=run['id'],run_attempt=run['run_attempt'],artifact_id=artifact['id'],firmware_sha256=manifest['images']['candidate']['sha256'])
        device.require(github.eligible_source(gh,number)==sha,'PR changed during artifact acceptance')
        gh.call('/statuses/'+sha,'POST',{'context':target+' hardware / heartbeat cleanup','state':'pending','description':'Exact target artifact accepted; hardware running'})
        binding=private_json(Path(job['binding']))
        result['device']=device.transaction(target,binding,Path(job['heartbeat']),job['heartbeat_sha256'],folder/'device',
                                             folder/'artifact/candidate.bin',manifest['images']['candidate']['sha256'])
        result['result']=result['device']['result']
    except Exception as error:
        result['error']=f'{type(error).__name__}: {error}'[:400]
    finally:
        device.save(path,result)
    status(gh,path,result)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--config',type=Path,required=True)
    p.add_argument('--evidence-root',type=Path,required=True)
    p.add_argument('--pr',type=int)
    p.add_argument('--maintenance-only',action='store_true')
    args=p.parse_args()
    config=private_json(args.config)
    jobs=config['jobs']
    device.require(config['schema']==1 and 0<len(jobs)<=3 and len({j['target'] for j in jobs})==len(jobs),'Invalid target configuration')
    for name,expected in config['pins'].items():
        file=Path(__file__).parent/name
        device.require(not Path(name).is_absolute() and '..' not in Path(name).parts and not file.is_symlink()
                       and device.digest(file.read_bytes())==expected,'Installed host code pin changed')
    for job in jobs:
        device.require(job['target'] in device.BOARDS and isinstance(job['enabled'],bool),'Invalid job')
        device.image_bytes(Path(job['heartbeat']),job['heartbeat_sha256'],job['target'],True)
    root=args.evidence_root; root.mkdir(mode=0o700,parents=True,exist_ok=True)
    with (root/'scheduler.lock').open('a+') as handle:
        try: fcntl.flock(handle,fcntl.LOCK_EX|fcntl.LOCK_NB)
        except BlockingIOError: return 0
        gh=None; numbers=[]
        if not args.maintenance_only:
            token=os.environ.get('GH_TOKEN')
            if token is None:
                found=subprocess.run(['/usr/bin/security','find-generic-password','-w','-s','riscrte-cam-ci','-a',github.OWNER],capture_output=True,text=True,timeout=10)
                device.require(found.returncode==0,'Existing CI credential unavailable')
                token=found.stdout.strip()
            gh=github.GitHub(token)
            if args.pr: numbers=[args.pr]
            else:
                prs=gh.call('/pulls?state=open&per_page=100')
                device.require(len(prs)<100,'PR page bound exceeded')
                numbers=[x['number'] for x in github.bounded_owner_prs(prs)]
        summary=[]
        for job in jobs:
            if not job['enabled'] or Path(job['pause']).exists():
                summary.append({'target':job['target'],'state':'paused'}); continue
            try:
                # Incomplete earlier work cannot bypass heartbeat cleanup.
                health=maintain(job,root)
                summary.append({'target':job['target'],'maintenance':health})
                if health.get('state')=='busy' or health.get('result')=='failed': continue
                for number in numbers:
                    try: once(gh,number,job,root)
                    except github.RateLimited: raise
                    except (ValueError,RuntimeError,OSError) as error:
                        summary.append({'target':job['target'],'pr':number,'error':type(error).__name__+': '+str(error)[:250]})
            except Exception as error:
                summary.append({'target':job['target'],'error':type(error).__name__+': '+str(error)[:250]})
        device.save(root/'last-scan.json',{'schema':1,'time_unix':int(time.time()),'targets':summary})
    return 0

if __name__=='__main__': sys.exit(main())
