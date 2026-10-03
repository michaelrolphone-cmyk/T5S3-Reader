#!/usr/bin/env python3
"""Offline schema-2 ordinary-SD deployment validation. No hardware/write path.

Schema-1 internal driver stores are deliberately rejected after the owner
placement correction. Artifact validation never grants staging/flash permission.
"""
import argparse
import csv
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import re
import stat
import time
import zipfile

APP_OFFSET, APP_SIZE = 0x10000, 0x640000
STORE_OFFSET, STORE_SIZE = 0xc90000, 0x360000
TABLE_HASH = '9af3af2b74e944337ba85f2b0027ee80df160579a1ab746ba0f95853f618cd60'
BOARD_IDS = ('xteink-x4-pro', 't5s3-pro')
MAX_ARCHIVE = 24 * 1024 * 1024
MAX_EXPANDED = 32 * 1024 * 1024


def require(ok, reason):
    if not ok: raise ValueError(reason)


def digest(data): return {'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()}


def safe_name(name):
    require(isinstance(name,str) and len(name.encode()) <= 255 and '\\' not in name and '\x00' not in name
            and all(32 <= ord(c) < 127 for c in name) and ':' not in name
            and not name.startswith('/') and all(x not in ('','.', '..') and not x.endswith((' ','.'))
                                                for x in name.split('/')), 'Unsafe archive/SD path')
    return name


def read_json(data):
    require(len(data) <= 65536, 'JSON exceeds bound')
    def unique(pairs):
        out = {}
        for key,value in pairs:
            require(key not in out,'Duplicate JSON key');out[key] = value
        return out
    return json.loads(data,object_pairs_hook=unique)


def archive_files(raw, count, per_file, total):
    result = {}; expanded = 0; started = time.monotonic()
    with zipfile.ZipFile(io.BytesIO(raw)) as z:
        require(len(z.infolist()) <= count, 'Archive entry count exceeds bound')
        names = set(); folded = set(); components = {}
        for item in z.infolist():
            name = item.filename[:-1] if item.is_dir() else item.filename
            safe_name(name)
            require(time.monotonic()-started < 15, 'Archive validation deadline exceeded')
            require(name not in names, 'Duplicate archive path');names.add(name)
            require(name.casefold() not in folded, 'Case-colliding archive path');folded.add(name.casefold())
            parts=name.split('/')
            for end in range(1,len(parts)+1):
                path='/'.join(parts[:end]);key=path.casefold()
                require(key not in components or components[key]==path,'Case-colliding archive directory')
                components[key]=path
            mode = (item.external_attr >> 16) & 0o170000
            require(mode in (0,stat.S_IFREG,stat.S_IFDIR) and not item.flag_bits & 1, 'Link/special/encrypted entry refused')
            require(item.compress_type in (zipfile.ZIP_STORED,zipfile.ZIP_DEFLATED), 'Unsupported ZIP method')
            if item.is_dir():continue
            require(mode != stat.S_IFDIR and 0 <= item.file_size <= per_file, 'Archive file exceeds bound')
            expanded += item.file_size;require(expanded <= total,'Archive expansion exceeds bound')
            result[name] = z.read(item)
            time.sleep(0)  # bounded host work yields between entries
        for name in names:
            require(not any(str(p) in result for p in PurePosixPath(name).parents if str(p) != '.'), 'File/directory path collision')
    return result


def package_files(raw, record):
    require(digest(raw) == {k:record[k] for k in ('bytes','sha256')}, 'Package archive hash mismatch')
    files = archive_files(raw,17,1024*1024,4*1024*1024)
    manifest = read_json(files['.package.json'])
    require(type(manifest.get('schema')) is int and manifest['schema'] == 1
            and manifest['kind'] == 'driver' and manifest['id'] == record['id']
            and manifest['version'] == record['version'] and manifest['architecture'] == 'xtensa-esp32s3'
            and manifest['artifact'] == 'driver.elf', 'Ordinary driver package identity mismatch')
    entries = manifest['entries'];require(isinstance(entries,list) and 1 <= len(entries) <= 16, 'Package inventory invalid')
    declared = {'.package.json'}
    for item in entries:
        name = safe_name(item['name']);require('/' not in name and name not in declared, 'Package entry duplicate/nested')
        declared.add(name)
        require(name in files and digest(files[name]) == {'bytes':item['size_bytes'],'sha256':item['sha256']}, 'Package payload hash mismatch')
    require(declared == set(files), 'Package inventory differs from archive')
    driver = read_json(files['manifest.json'])
    require(driver['id'] == record['id'] and driver['version'] == record['version'], 'Driver manifest identity mismatch')
    elf = files['driver.elf']
    require(len(elf) >= 52 and elf[:6] == b'\x7fELF\x01\x01' and int.from_bytes(elf[16:18],'little') == 3
            and int.from_bytes(elf[18:20],'little') == 94, 'Driver is not Xtensa ET_DYN')
    return files


def sd_files(raw, manifest):
    """Validate the authoritative nested SD archive and exact declared inventory."""
    require(manifest.get('driver_medium') == 'sd' and manifest.get('sd_root') == 'sdcard',
            'Ordinary SD placement required')
    require(manifest.get('sd_archive') == {'file':'sdcard.zip', **digest(raw)}, 'SD archive hash/size mismatch')
    inventory=manifest['files']
    require(isinstance(inventory,dict) and 1 <= len(inventory) <= 128, 'SD inventory invalid')
    decoded=archive_files(raw,160,1024*1024,4*1024*1024)
    require(set(decoded) == {'sdcard/'+safe_name(name) for name in inventory}, 'SD archive inventory mismatch')
    decoded={name[len('sdcard/'):]:data for name,data in decoded.items()}
    require({name:digest(data) for name,data in decoded.items()} == inventory, 'SD contents differ from declared inventory')
    return decoded


def validate(raw, expected_sha, archive_sha, *, board_id='xteink-x4-pro'):
    require(board_id in BOARD_IDS, 'Unsupported explicit board profile')
    require(re.fullmatch('[0-9a-f]{40}',expected_sha) is not None,'Full expected source SHA required')
    require(len(raw) <= MAX_ARCHIVE and digest(raw)['sha256'] == archive_sha,'Artifact size/hash mismatch')
    files=archive_files(raw,32,APP_SIZE,MAX_EXPANDED)
    m=read_json(files['deployment.json'])
    require(type(m.get('schema')) is int and m['schema'] == 2, 'Only schema-2 SD deployment is supported; internal flash driver stores are retired')
    require(m['board'] == board_id and m['source_sha'] == expected_sha,'Deployment source/board mismatch')
    require(set(m)=={'schema','board','source_sha','provisioning_authorized','driver_medium','firmware','sd_root','sd_archive','partition_table','files','packages'}, 'Deployment manifest fields invalid')
    require(m['provisioning_authorized'] is False,'Artifact cannot authorize provisioning')
    require(m['firmware'] == {'file':'firmware.bin','offset':APP_OFFSET,**digest(files['firmware.bin'])},'Paired firmware hash/offset mismatch')
    app=files['firmware.bin'];erase=(len(app)+4095)//4096*4096
    require(24 < len(app) <= APP_SIZE and erase <= APP_SIZE and app[0] == 0xe9 and int.from_bytes(app[12:14],'little') == 9
            and ('RISCRTE_BOARD_ID:'+board_id).encode() in app,'Firmware board identity/range invalid')
    require(digest(files['partitions.csv']) == m['partition_table'],'Partition CSV hash mismatch')
    rows=[]
    for row in csv.reader(files['partitions.csv'].decode().splitlines()):
        if not row or row[0].lstrip().startswith('#'):continue
        row=[x.strip() for x in row];require(len(row) in (5,6) and (len(row)==5 or not row[5]),'Partition flags/columns changed')
        rows.append((row[0],row[1],row[2],int(row[3],0),int(row[4],0)))
    require(rows == [('nvs','data','nvs',0x9000,0x5000),('otadata','data','ota',0xe000,0x2000),('app0','app','ota_0',APP_OFFSET,APP_SIZE),('app1','app','ota_1',0x650000,APP_SIZE),('spiffs','data','spiffs',STORE_OFFSET,STORE_SIZE),('coredump','data','coredump',0xff0000,0x10000)],'Partition geometry differs from approved layout')
    records=m['packages'];require(isinstance(records,list) and 1 <= len(records) <= 16,'Package count invalid')
    packages={};expected={'deployment.json','README.txt','partitions.csv','firmware.bin','sdcard.zip'}
    for record in records:
        identity=record['id'];version=record['version'];name=safe_name(record['file'])
        require(re.fullmatch('[a-z][a-z0-9-]{0,63}',identity) and re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+',version)
                and name == identity+'-'+version+'.rte.zip' and identity not in packages,'Package identity/filename invalid')
        key='packages/'+name;expected.add(key);packages[identity]=package_files(files[key],record)
    require(set(files) == expected,'Deployment archive inventory mismatch')
    decoded=sd_files(files['sdcard.zip'],m)
    boot=read_json(decoded['System/Config/boot.json']);board=read_json(decoded['System/Config/board.json'])
    require(boot['board']=='System/Config/board.json' and board['board_id']==board_id,'SD board binding mismatch')
    selected=set();sd_paths={'System/Config/boot.json','System/Config/board.json'}
    require(isinstance(boot['drivers'],list) and 1 <= len(boot['drivers']) <= 16,'Boot driver list invalid')
    for item in boot['drivers']:
        name=safe_name(item['manifest']);parts=name.split('/')
        require(len(parts)==3 and parts[0]=='Drivers' and parts[2]=='manifest.json'
                and parts[1] not in selected and parts[1] in packages,'Boot package selection invalid')
        selected.add(parts[1])
    for record in records:
        identity=record['id']
        for entry,data in packages[identity].items():
            key='Drivers/'+identity+'/'+entry;sd_paths.add(key)
            require(decoded.get(key)==data,'SD/package bytes differ')
        inbox='Packages/Inbox/'+record['file'];sd_paths.add(inbox)
        require(decoded.get(inbox)==files['packages/'+record['file']], 'SD Inbox/package bytes differ')
    require(set(decoded)==sd_paths,'Unexpected/missing SD files')
    return {'schema':2,'validation':'pass','mode':'offline-dry-run','board':board_id,'source_sha':expected_sha,'artifact_sha256':archive_sha,
            'provisioning_authorized':False,'deployment_state':'pending','deployment_reason':'SD placement and candidate boot require separate physical verification',
            'driver_medium':'sd','sd_archive':m['sd_archive'],'sd_files':m['files'],'sd_file_count':len(decoded),
            'expected_mac':'84:c7:bb:79:e2:ac' if board_id=='xteink-x4-pro' else None,
            'expected_partition_sha256':TABLE_HASH if board_id=='xteink-x4-pro' else None,
            'physical_binding_established':False,
            'proposed_regions':[dict(m['firmware'],erase_bytes=erase)],
            'protected_regions':[{'offset':0,'bytes':0x10000},{'offset':0x650000,'bytes':APP_SIZE},
                                 {'offset':STORE_OFFSET,'bytes':STORE_SIZE},{'offset':0xff0000,'bytes':0x10000}],
            'store_replaces_entire_region':False,'selected_drivers':sorted(selected),'packages':records,
            'required_before_any_write':['Verify exact Actions/local build provenance and paired hashes',
                'Identify explicit offline SD volume; back up conflicting target files and preserve unrelated data',
                'Stage and read back exact SD inventory including hidden ordinary metadata',
                'Reverify physical MAC and live partition table before separately authorized app-only flash',
                'Never erase or write the former internal driver-store region'],
            'automatic_app_only_deployment_allowed':False,'hardware_access_performed':False,'hardware_validation':'failed','hardware_validation_reason':'Unavailable: no physical candidate execution performed'}


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--artifact',type=Path,required=True);p.add_argument('--expected-sha',required=True)
    p.add_argument('--artifact-sha256',required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--board',choices=BOARD_IDS,default='xteink-x4-pro',help='Explicit artifact identity; T5 software validation does not establish physical binding')
    a=p.parse_args()
    try:
        require(a.artifact.is_file() and not a.artifact.is_symlink() and a.artifact.stat().st_size<=MAX_ARCHIVE,'Artifact path/size invalid')
        result=validate(a.artifact.read_bytes(),a.expected_sha,a.artifact_sha256,board_id=a.board)
    except Exception as error:result={'validation':'failure','deployment_state':'failure','reason':str(error),'provisioning_authorized':False,'hardware_access_performed':False,'hardware_validation':'failed','hardware_validation_reason':'Unavailable: no physical candidate execution performed'}
    a.output.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
    return 0 if result['validation']=='pass' else 1

if __name__=='__main__':raise SystemExit(main())
