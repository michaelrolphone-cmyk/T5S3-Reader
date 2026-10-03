"""Pinned RiscRTE CAM adapter; Actions artifacts are bounded data, never host code."""
import io
import json
import re
import zipfile
from pathlib import Path
import heartbeat_policy as device
import three_target_controller as controller
from three_target_controller import github

REPOSITORY = 'michaelrolphone-cmyk/RiscRTE'
REPOSITORY_ID = 1402583471
CONTEXT = 'ESP32-CAM hardware / heartbeat cleanup'
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
    for name in ('board.json','boot.json'):
        device.require(isinstance(json.loads(payload[name]),dict), 'Embedded configuration is not an object')
    folder.mkdir(mode=0o700)
    (folder/'candidate.bin').write_bytes(app)
    device.save(folder/'manifest.json',manifest)
    return manifest


def publish(gh, path, record, state, reason):
    reason = reason[:140]
    if record.get('status_state') != state or record.get('status_description') != reason:
        response = gh.call('/statuses/' + record['source_sha'], 'POST',
                           {'context':CONTEXT,'state':state,'description':reason})
        record.update(status_id=response['id'],status_state=state,status_description=reason)
    device.save(path,record)


def scan(token, config, jobs, root):
    device.require(type(config.get('enabled')) is bool, 'Runtime enabled flag must be boolean')
    job = next(j for j in jobs if j['target'] == 'cam-nosd')
    gh = github.GitHub(token, repository=REPOSITORY)
    repository = gh.call('')
    device.require(repository['id'] == REPOSITORY_ID and repository['full_name'] == REPOSITORY, 'Runtime repository identity mismatch')
    prs = gh.call('/pulls?state=open&per_page=100')
    device.require(len(prs) < 100, 'Runtime PR page bound exceeded')
    eligible = [p for p in prs if p['user']['login'] == github.OWNER
                and (p['head'].get('repo') or {}).get('full_name') == REPOSITORY]
    device.require(len(eligible) <= github.MAX_OWNER_PRS, 'Runtime PR count exceeds bound')
    outcomes = []
    for pr in eligible:
        sha = github.eligible_source(gh,pr['number'])
        folder = root/'runtime-cam'/sha; folder.mkdir(mode=0o700,parents=True,exist_ok=True)
        path = folder/'result.json'
        record = json.loads(path.read_text()) if path.exists() else {'schema':1,'source_sha':sha,'pr':pr['number'],'repository':REPOSITORY}
        device.require(record['source_sha'] == sha and record['repository'] == REPOSITORY, 'Runtime journal identity mismatch')
        if record.get('attempted'):
            if config['enabled'] and not (record.get('device') or {}).get('heartbeat_restored') and not record.get('recovery_complete'):
                binding = controller.private_json(Path(job['binding']))
                import time
                try:
                    recovery = device.transaction('cam-nosd',binding,Path(job['heartbeat']),job['heartbeat_sha256'],folder/('recovery-'+str(time.time_ns())))
                    record.update(recovery=recovery,recovery_complete=recovery.get('heartbeat_restored') is True)
                except Exception as error:
                    record.update(passed=False,terminal_reason='FAILED: cleanup recovery: '+str(error))
                device.save(path,record)
            state = 'success' if record.get('passed') else 'failure'
            publish(gh,path,record,state,record.get('terminal_reason','FAILED: interrupted hardware attempt'))
            outcomes.append({'sha':sha,'state':state});continue
        try:
            device.require(config['enabled'], config.get('unavailable_reason','CAM execution disabled'))
            device.require(not Path(job['pause']).exists(), 'CAM execution paused')
            found = controller._candidate(gh,pr['number'],sha,'cam-nosd','cam-hardware-build.yml','cam-app-candidate-'+sha)
            device.require(found is not None,'Exact-head CAM artifact unavailable or build incomplete')
            run, artifact = found
            device.require(run['conclusion'] == 'success','Exact-head CAM build failed')
            raw = gh.call(f"/actions/artifacts/{artifact['id']}/zip",limit=5_000_000)
            if artifact.get('digest'):device.require(artifact['digest'] == 'sha256:'+device.digest(raw),'Runtime ZIP digest mismatch')
            # A previous artifact-only failure never authorizes replacing its evidence.
            dest = folder/('artifact-'+str(run['id'])+'-'+str(run['run_attempt']))
            device.require(not dest.exists(), 'Previous artifact acceptance incomplete; inspect evidence')
            manifest = unpack(raw,run,sha,dest)
            device.require(github.eligible_source(gh,pr['number']) == sha,'Runtime PR head changed')
            record.update(attempted=True,run_id=run['id'],run_attempt=run['run_attempt'],artifact_id=artifact['id'],firmware_sha256=manifest['firmware']['sha256'])
            device.save(path,record)
            publish(gh,path,record,'pending','Exact-head CAM candidate accepted; bounded hardware test running')
            binding = controller.private_json(Path(job['binding']))
            result = device.transaction('cam-nosd',binding,Path(job['heartbeat']),job['heartbeat_sha256'],folder/'device',
                                        dest/'candidate.bin',manifest['firmware']['sha256'],candidate_profile='runtime-heartbeat')
            record['device'] = result
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
    return {'repository':REPOSITORY,'heads':outcomes}
