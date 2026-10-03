"""Pinned RiscRTE runtime adapter; Actions artifacts are bounded data, never host code."""
import io
import json
import re
import time
import zipfile
from pathlib import Path
import heartbeat_policy as device
import three_target_controller as controller
from three_target_controller import github

REPOSITORY = 'michaelrolphone-cmyk/RiscRTE'
REPOSITORY_ID = 1402583471
CONTEXT = 'ESP32-CAM hardware / heartbeat cleanup'
PROFILES = {
    'cam-nosd': ('runtime-cam','cam-hardware-build.yml','cam-app-candidate-',CONTEXT),
    'x4': ('runtime-x4','x4-hardware-build.yml','x4-app-candidate-','X4 hardware / runtime heartbeat cleanup'),
}
BUILD_WAIT_SECONDS = 600
FILES = {'firmware.bin', 'default.elf', 'board.json', 'boot.json', 'manifest.json'}
LIMIT = 0x1f0000


def unpack(raw, run, sha, folder, target="cam-nosd"):
    device.require(target in ("cam-nosd","x4"), "Unknown runtime target")
    limit = device.BOARDS[target][2]
    device.require(len(raw) <= 14_000_000, 'Runtime archive exceeds bound')
    with zipfile.ZipFile(io.BytesIO(raw)) as archive:
        entries = archive.infolist()
        device.require(len(entries) == 5 and {x.filename for x in entries} == FILES, 'Runtime archive entries invalid')
        device.require(all(not x.is_dir() and (x.external_attr >> 16) & 0o170000 != 0o120000
                           and not x.flag_bits & 1 and 0 < x.file_size <= (16384 if x.filename.endswith('.json') else limit)
                           for x in entries), 'Unsafe runtime archive entry')
        payload = {x.filename: archive.read(x) for x in entries}
    manifest = json.loads(payload['manifest.json'])
    device.require(manifest['schema'] == 1 and manifest['target'] == target
                   and manifest['source_sha'] == sha and manifest['run_id'] == run['id']
                   and manifest['run_attempt'] == run['run_attempt']
                   and manifest['boot_backend'] == 'embedded-readonly'
                   and manifest['flash_bytes'] == 16777216 and manifest['memory_type'] == 'qio_opi',
                   'Runtime manifest provenance/profile mismatch')
    app = payload['firmware.bin']
    device.require(len(app) > 24 and app[0] == 0xe9 and int.from_bytes(app[12:14], 'little') == 9
                   and (len(app) + 4095) // 4096 * 4096 <= limit, 'Runtime app chip/range invalid')
    device.require(('RTE_SOURCE='+sha).encode() in app, 'Runtime source marker mismatch')
    device.require(manifest['firmware'] == {'file':'firmware.bin','bytes':len(app),
                   'sha256':device.digest(app),'offset':0x10000}, 'Runtime app hash/offset mismatch')
    items = manifest['payloads']
    device.require(isinstance(items, list) and len(items) == 3
                   and {x['file'] for x in items} == {'default.elf','board.json','boot.json'}, 'Runtime payload inventory invalid')
    ranges = []
    for item in items:
        data = payload[item['file']]; offset = item['image_offset']
        device.require(type(offset) is int and offset >= 24 and offset + len(data) <= len(app)
                       and item == {'file':item['file'],'bytes':len(data),'sha256':device.digest(data),'image_offset':offset}
                       and app[offset:offset + len(data)] == data, 'Embedded payload bytes/hash/range mismatch')
        ranges.append((offset,offset + len(data)))
    ranges.sort()
    device.require(all(a[1] <= b[0] for a,b in zip(ranges,ranges[1:])), 'Embedded payload ranges overlap')
    device.require(payload['default.elf'].startswith(b'\x7fELF'), 'Default payload is not ELF')
    board = json.loads(payload['board.json']); boot = json.loads(payload['boot.json'])
    device.require(isinstance(board,dict) and isinstance(boot,dict), 'Embedded configuration is not an object')
    device.require(board.get('buses') == [] and board.get('devices') == [] and boot.get('drivers') == []
                   and boot.get('default_app') == 'default.elf', 'Runtime heartbeat fixture must have an empty hardware graph')
    folder.mkdir(mode=0o700)
    (folder/'candidate.bin').write_bytes(app)
    device.save(folder/'manifest.json',manifest)
    return manifest


def publish(gh, path, record, state, reason):
    reason = reason[:140]
    # Preserve completed hardware evidence even when GitHub is unreachable.
    device.save(path,record)
    if record.get('status_state') != state or record.get('status_description') != reason:
        response = gh.call('/statuses/' + record['source_sha'], 'POST',
                           {'context':PROFILES[record.get('target','cam-nosd')][3],'state':state,'description':reason})
        record.update(status_id=response['id'],status_state=state,status_description=reason)
    device.save(path,record)


def recover_active(config, job, root, target="cam-nosd"):
    """Recover an interrupted transaction even if its PR moved to a new head."""
    active = root/PROFILES[target][0]/'active.json'
    if not active.exists(): return None
    if not config['enabled']: return config.get('unavailable_reason','Runtime execution disabled')
    if Path(job['pause']).exists(): return 'Runtime execution paused; interrupted cleanup remains queued'
    prior = json.loads(active.read_text())
    device.require(prior.get('target') == target and github.SHA.fullmatch(prior.get('source_sha','')), 'Active runtime journal invalid')
    folder = root/PROFILES[target][0]/prior['source_sha']
    try:
        binding = controller.private_json(Path(job['binding']))
        recovery = device.transaction(target,binding,Path(job['heartbeat']),job['heartbeat_sha256'],folder/('recovery-'+str(time.time_ns())))
    except Exception as error:
        return 'Interrupted runtime cleanup failed: '+str(error)
    if not (recovery.get('result') == 'pass' and recovery.get('heartbeat_restored') and recovery.get('protected_equal')):
        return 'Interrupted runtime cleanup failed: '+str(recovery.get('cleanup_error') or recovery.get('error') or 'incomplete evidence')
    path = folder/'result.json'
    record = json.loads(path.read_text()) if path.exists() else {'schema':1,'source_sha':prior['source_sha'],'repository':REPOSITORY,'target':target,'attempted':True,'passed':False}
    record.update(recovery=recovery,recovery_complete=True)
    device.save(path,record)
    active.unlink()
    return None

def scan(token, config, jobs, root, target="cam-nosd"):
    device.require(target in PROFILES, "Unknown runtime target")
    directory,workflow,artifact_prefix,_ = PROFILES[target]
    device.require(type(config.get('enabled')) is bool, 'Runtime enabled flag must be boolean')
    job = next(j for j in jobs if j['target'] == target)
    gh = github.GitHub(token, repository=REPOSITORY)
    repository = gh.call('')
    device.require(repository['id'] == REPOSITORY_ID and repository['full_name'] == REPOSITORY, 'Runtime repository identity mismatch')
    prs = gh.call('/pulls?state=open&per_page=100')
    device.require(len(prs) < 100, 'Runtime PR page bound exceeded')
    eligible = [p for p in prs if p['user']['login'] == github.OWNER
                and (p['head'].get('repo') or {}).get('full_name') == REPOSITORY]
    device.require(len(eligible) <= github.MAX_OWNER_PRS, 'Runtime PR count exceeds bound')
    recovery_error = recover_active(config,job,root,target)
    outcomes = []
    for pr in eligible:
        sha = github.eligible_source(gh,pr['number'])
        folder = root/directory/sha; folder.mkdir(mode=0o700,parents=True,exist_ok=True)
        path = folder/'result.json'
        record = json.loads(path.read_text()) if path.exists() else {'schema':1,'source_sha':sha,'pr':pr['number'],'repository':REPOSITORY,'target':target}
        record.setdefault('target',target)
        device.require(record['target'] == target and record['source_sha'] == sha and record['repository'] == REPOSITORY, 'Runtime journal identity mismatch')
        if record.get('attempted'):
            if not recovery_error and config['enabled'] and not Path(job['pause']).exists() and not (record.get('device') or {}).get('heartbeat_restored') and not record.get('recovery_complete'):
                binding = controller.private_json(Path(job['binding']))
                try:
                    recovery = device.transaction(target,binding,Path(job['heartbeat']),job['heartbeat_sha256'],folder/('recovery-'+str(time.time_ns())))
                    record.update(recovery=recovery,recovery_complete=recovery.get('heartbeat_restored') is True)
                except Exception as error:
                    record.update(passed=False,terminal_reason='FAILED: cleanup recovery: '+str(error))
                device.save(path,record)
            state = 'success' if record.get('passed') else 'failure'
            publish(gh,path,record,state,record.get('terminal_reason','FAILED: interrupted hardware attempt'))
            outcomes.append({'sha':sha,'state':state});continue
        try:
            device.require(config['enabled'], config.get('unavailable_reason','Runtime execution disabled'))
            device.require(not recovery_error,recovery_error)
            device.require(not Path(job['pause']).exists(), 'Runtime execution paused')
            binding = controller.private_json(Path(job['binding']))
            device.Transport(binding,target,folder).port()  # Enumeration only; absence fails promptly.
            found = controller._candidate(gh,pr['number'],sha,target,workflow,artifact_prefix+sha)
            if found is None:
                first = record.setdefault('build_wait_started',int(time.time()))
                device.require(time.time()-first < BUILD_WAIT_SECONDS,'Exact-head artifact unavailable after bounded build wait')
                publish(gh,path,record,'pending','Waiting for exact-head build; limited to 10 minutes')
                outcomes.append({'sha':sha,'state':'waiting-for-build'});continue
            run, artifact = found
            device.require(run['conclusion'] == 'success','Exact-head runtime build failed')
            raw = gh.call(f"/actions/artifacts/{artifact['id']}/zip",limit=14_000_000)
            if artifact.get('digest'):device.require(artifact['digest'] == 'sha256:'+device.digest(raw),'Runtime ZIP digest mismatch')
            # A previous artifact-only failure never authorizes replacing its evidence.
            dest = folder/('artifact-'+str(run['id'])+'-'+str(run['run_attempt']))
            device.require(not dest.exists(), 'Previous artifact acceptance incomplete; inspect evidence')
            manifest = unpack(raw,run,sha,dest,target)
            device.require(github.eligible_source(gh,pr['number']) == sha,'Runtime PR head changed')
            record.update(attempted=True,run_id=run['id'],run_attempt=run['run_attempt'],artifact_id=artifact['id'],firmware_sha256=manifest['firmware']['sha256'])
            device.save(path,record)
            publish(gh,path,record,'pending','Exact-head runtime candidate accepted; bounded hardware test running')
            binding = controller.private_json(Path(job['binding']))
            active = root/PROFILES[target][0]/'active.json'
            device.save(active,{'schema':1,'target':target,'source_sha':sha})
            result = device.transaction(target,binding,Path(job['heartbeat']),job['heartbeat_sha256'],folder/'device',
                                        dest/'candidate.bin',manifest['firmware']['sha256'],candidate_profile='runtime-heartbeat')
            record['device'] = result
            device.save(path,record)
            if result.get('heartbeat_restored') and result.get('protected_equal'): active.unlink()
            record['passed'] = (result['result'] == 'pass' and result.get('candidate_readback_equal') is True
                                and result.get('protected_equal') is True and result.get('heartbeat_restored') is True
                                and result.get('heartbeat_readback_equal') is True and bool(result.get('candidate_checks')))
            record['terminal_reason'] = 'Candidate heartbeat and cleanup verified' if record['passed'] else 'FAILED: '+str(result.get('cleanup_error') or result.get('error') or 'hardware evidence incomplete')
        except github.RateLimited:
            raise
        except Exception as error:
            record.update(passed=False,terminal_reason='FAILED: '+str(error))
        state = 'success' if record.get('passed') else 'failure'
        publish(gh,path,record,state,record['terminal_reason'])
        outcomes.append({'sha':sha,'state':state})
    return {'repository':REPOSITORY,'target':target,'heads':outcomes}
