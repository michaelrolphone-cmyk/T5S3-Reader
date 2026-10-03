#!/usr/bin/env python3
"""Offline paired-artifact validation only. No serial imports or provisioning path."""
import argparse
import csv
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import re
import resource
import stat
import subprocess
import tempfile
import zipfile

APP_OFFSET, APP_SIZE = 0x10000, 0x640000
STORE_OFFSET, STORE_SIZE = 0xc90000, 0x360000
TABLE_HASH = '9af3af2b74e944337ba85f2b0027ee80df160579a1ab746ba0f95853f618cd60'
MAX_ARCHIVE = 24 * 1024 * 1024
MAX_EXPANDED = 32 * 1024 * 1024


def require(ok, reason):
    if not ok: raise ValueError(reason)


def digest(data): return {'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()}


def safe_name(name):
    require(isinstance(name,str) and len(name.encode()) <= 255 and '\\' not in name and '\x00' not in name
            and not name.startswith('/') and all(x not in ('','.', '..') for x in name.split('/')), 'Unsafe archive/store path')
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
    result = {}; expanded = 0
    with zipfile.ZipFile(io.BytesIO(raw)) as z:
        require(len(z.infolist()) <= count, 'Archive entry count exceeds bound')
        names = set()
        for item in z.infolist():
            name = item.filename.rstrip('/') if item.is_dir() else item.filename
            safe_name(name)
            require(name not in names, 'Duplicate archive path');names.add(name)
            mode = (item.external_attr >> 16) & 0o170000
            require(mode in (0,stat.S_IFREG,stat.S_IFDIR) and not item.flag_bits & 1, 'Link/special/encrypted entry refused')
            require(item.compress_type in (zipfile.ZIP_STORED,zipfile.ZIP_DEFLATED), 'Unsupported ZIP method')
            if item.is_dir():continue
            require(mode != stat.S_IFDIR and 0 <= item.file_size <= per_file, 'Archive file exceeds bound')
            expanded += item.file_size;require(expanded <= total,'Archive expansion exceeds bound')
            result[name] = z.read(item)
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


def decode_store(image, tool, tool_sha):
    """Run only a separately pinned local decoder, with no network or outside writes."""
    require(tool.is_file() and not tool.is_symlink() and tool.stat().st_size <= 64*1024*1024, 'Decoder path invalid')
    require(digest(tool.read_bytes())['sha256'] == tool_sha, 'Local decoder hash mismatch')
    require(Path('/usr/bin/sandbox-exec').is_file(), 'Sandboxed LittleFS decoding unavailable')
    with tempfile.TemporaryDirectory(prefix='x4-store-review-') as temporary:
        root = Path(temporary).resolve();source=root/'image.bin';source.write_bytes(image)
        output=root/'decoded';output.mkdir()
        profile='(version 1)(allow default)(deny network*)(deny file-write* (require-not (subpath '+json.dumps(str(output))+')))' 
        def limits():
            resource.setrlimit(resource.RLIMIT_FSIZE,(4*1024*1024,4*1024*1024))
            resource.setrlimit(resource.RLIMIT_CPU,(30,30))
        done = subprocess.run(['/usr/bin/sandbox-exec','-p',profile,str(tool.resolve()),'-u',str(output),'-b','4096','-p','256','-s',str(STORE_SIZE),str(source)],
                              stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,timeout=60,preexec_fn=limits)
        require(done.returncode == 0, 'Sandboxed LittleFS decode failed')
        files={};total=0;entries=0
        for parent,dirs,names in os.walk(output,followlinks=False):
            entries += len(dirs)+len(names);require(entries <= 256,'Decoded entry count exceeds bound')
            for name in dirs+names: require(not (Path(parent)/name).is_symlink(),'Decoded symlink refused')
            for name in names:
                file=Path(parent)/name;require(file.is_file() and file.stat().st_size <= 1024*1024,'Decoded file invalid')
                key=safe_name(file.relative_to(output).as_posix());data=file.read_bytes();total+=len(data)
                require(len(files) < 128 and total <= STORE_SIZE,'Decoded inventory exceeds bound');files[key]=data
        return files


def validate(raw, expected_sha, archive_sha, tool=None, tool_sha=None, decoder=decode_store):
    require(re.fullmatch('[0-9a-f]{40}',expected_sha) is not None,'Full expected source SHA required')
    require(len(raw) <= MAX_ARCHIVE and digest(raw)['sha256'] == archive_sha,'Artifact size/hash mismatch')
    files=archive_files(raw,32,APP_SIZE,MAX_EXPANDED)
    m=read_json(files['deployment.json'])
    require(m['schema'] == 1 and m['board'] == 'xteink-x4-pro' and m['source_sha'] == expected_sha,'Deployment source/board mismatch')
    require(m['provisioning_authorized'] is False,'Artifact cannot authorize provisioning')
    require(m['firmware'] == {'file':'firmware.bin','offset':APP_OFFSET,**digest(files['firmware.bin'])},'Paired firmware hash/offset mismatch')
    require(m['module_store'] == {'file':'module-store.bin','offset':STORE_OFFSET,**digest(files['module-store.bin'])}
            and len(files['module-store.bin']) == STORE_SIZE,'Paired store hash/offset/size mismatch')
    app=files['firmware.bin'];erase=(len(app)+4095)//4096*4096
    require(24 < len(app) <= APP_SIZE and erase <= APP_SIZE and app[0] == 0xe9 and int.from_bytes(app[12:14],'little') == 9
            and b'RISCRTE_BOARD_ID:xteink-x4-pro' in app,'X4 firmware identity/range invalid')
    require(digest(files['partitions.csv']) == m['partition_table'],'Partition CSV hash mismatch')
    rows=[]
    for row in csv.reader(files['partitions.csv'].decode().splitlines()):
        if not row or row[0].lstrip().startswith('#'):continue
        row=[x.strip() for x in row];require(len(row) in (5,6) and (len(row)==5 or not row[5]),'Partition flags/columns changed')
        rows.append((row[0],row[1],row[2],int(row[3],0),int(row[4],0)))
    require(rows == [('nvs','data','nvs',0x9000,0x5000),('otadata','data','ota',0xe000,0x2000),('app0','app','ota_0',APP_OFFSET,APP_SIZE),('app1','app','ota_1',0x650000,APP_SIZE),('spiffs','data','spiffs',STORE_OFFSET,STORE_SIZE),('coredump','data','coredump',0xff0000,0x10000)],'Partition geometry differs from approved layout')
    records=m['packages'];require(isinstance(records,list) and 1 <= len(records) <= 16,'Package count invalid')
    packages={};expected={'deployment.json','README.txt','partitions.csv','firmware.bin','module-store.bin'}
    for record in records:
        identity=record['id'];version=record['version'];name=safe_name(record['file'])
        require(re.fullmatch('[a-z][a-z0-9-]{0,63}',identity) and re.fullmatch('[0-9]+\\.[0-9]+\\.[0-9]+',version)
                and name == identity+'-'+version+'.rte.zip' and identity not in packages,'Package identity/filename invalid')
        key='packages/'+name;expected.add(key);packages[identity]=package_files(files[key],record)
    require(set(files) == expected,'Deployment archive inventory mismatch')
    inventory=m['files'];require(isinstance(inventory,dict) and 1 <= len(inventory) <= 128,'Store inventory invalid')
    for name in inventory:safe_name(name)
    decoded=decoder(files['module-store.bin'],tool,tool_sha)
    require({k:digest(v) for k,v in decoded.items()} == inventory,'Store contents differ from declared inventory')
    boot=read_json(decoded['boot.json']);board=read_json(decoded['board.json'])
    require(boot['board']=='board.json' and board['board_id']=='xteink-x4-pro','Store board binding mismatch')
    selected=set();store_paths={'boot.json','board.json'}
    require(isinstance(boot['drivers'],list) and 1 <= len(boot['drivers']) <= 16,'Boot driver list invalid')
    for item in boot['drivers']:
        name=safe_name(item['manifest']);parts=name.split('/')
        require(len(parts)==3 and parts[0]=='Drivers' and parts[2]=='manifest.json' and parts[1] not in selected and parts[1] in packages,'Boot package selection invalid')
        identity=parts[1];selected.add(identity)
        for entry,data in packages[identity].items():
            key='Drivers/'+identity+'/'+entry;store_paths.add(key)
            require(decoded.get(key)==data,'Store/package bytes differ')
    require(set(decoded)==store_paths,'Unexpected/missing store files')
    return {'schema':1,'validation':'pass','mode':'offline-dry-run','source_sha':expected_sha,'artifact_sha256':archive_sha,
            'provisioning_authorized':False,'deployment_state':'failure','deployment_reason':'Store compatibility and prior contents unverified; explicit provisioning approval required',
            'expected_mac':'84:c7:bb:79:e2:ac','expected_partition_sha256':TABLE_HASH,
            'proposed_regions':[dict(m['firmware'],erase_bytes=erase),dict(m['module_store'],erase_bytes=STORE_SIZE)],
            'protected_regions':[{'offset':0,'bytes':0x10000},{'offset':0x650000,'bytes':APP_SIZE},{'offset':0xff0000,'bytes':0x10000}],
            'store_replaces_entire_region':True,'selected_drivers':sorted(selected),'packages':records,
            'required_before_any_write':['Verify exact Actions/local build provenance and paired hashes','Reverify physical MAC and live partition table','Read and preserve current store; review prior contents and rollback plan','Obtain explicit approval for these exact firmware/store hashes','Use exclusive device lock, bounded writes/readback, and protected-region verification'],
            'automatic_app_only_deployment_allowed':False,'hardware_access_performed':False}


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--artifact',type=Path,required=True);p.add_argument('--expected-sha',required=True)
    p.add_argument('--artifact-sha256',required=True);p.add_argument('--decoder',type=Path,required=True);p.add_argument('--decoder-sha256',required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    try:
        require(a.artifact.is_file() and not a.artifact.is_symlink() and a.artifact.stat().st_size<=MAX_ARCHIVE,'Artifact path/size invalid')
        result=validate(a.artifact.read_bytes(),a.expected_sha,a.artifact_sha256,a.decoder,a.decoder_sha256)
    except Exception as error:result={'validation':'failure','deployment_state':'failure','reason':str(error),'provisioning_authorized':False,'hardware_access_performed':False}
    a.output.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
    return 0 if result['validation']=='pass' else 1

if __name__=='__main__':raise SystemExit(main())
