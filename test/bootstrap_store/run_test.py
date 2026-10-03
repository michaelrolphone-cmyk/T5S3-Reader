#!/usr/bin/env python3
"""Exercise production POSIX bootstrap reads and ordinary-package validation."""
import hashlib,json,os,shutil,subprocess,tempfile,sys
from pathlib import Path
root=Path(__file__).resolve().parents[2]
include=Path(sys.argv[1]) if len(sys.argv)>1 else root/'.pio/libdeps/xteink-x4-pro/ArduinoJson/src'
crypto=os.environ.get('OPENSSL_PREFIX')
flags=['-lcrypto']
if crypto: flags=['-I'+crypto+'/include','-L'+crypto+'/lib','-Wl,-rpath,'+crypto+'/lib',*flags]
with tempfile.TemporaryDirectory(dir="/tmp") as t:
 d=Path(t); binary=d/'test'
 subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-Wno-overloaded-virtual',
  '-I'+str(root/'test/bootstrap_store/stubs'),'-I'+str(root/'test/storage_volume/stubs'),
  '-I'+str(root/'src'),'-I'+str(root/'sdk/driver'),'-I'+str(include),
  str(root/'test/bootstrap_store/test.cpp'),str(root/'src/runtime/drivers/BootstrapModuleStore.cpp'),*flags,'-o',str(binary)],check=True)
 fixture=d/'bootfs';shutil.copytree(root/'dist/x4-independent-packages/bootfs',fixture)
 def run(ok):
  p=subprocess.run([str(binary),str(fixture),'xteink-x4-pro'],capture_output=True,text=True,timeout=20)
  assert (p.returncode==0)==ok,p.stdout+p.stderr
  return p.stdout
 print(run(True).strip())
 boot=fixture/'boot.json'; original=boot.read_bytes(); config=json.loads(original)
 config['drivers'][0]['manifest']='../escape.json';boot.write_text(json.dumps(config));run(False);boot.write_bytes(original)
 driver=fixture/'Drivers/x4pro-sd/driver.elf'; data=driver.read_bytes();driver.write_bytes(data[:-1]+bytes([data[-1]^1]));run(False);driver.write_bytes(data)
 driver.rename(driver.with_suffix('.missing'));run(False);driver.with_suffix('.missing').rename(driver)
 manifest=fixture/'Drivers/x4pro-sd/manifest.json';data=manifest.read_bytes();config=json.loads(data);config['version']='99.0.0';manifest.write_text(json.dumps(config));run(False);manifest.write_bytes(data)
 # A coherent external package update is admitted by the same reader binary.
 # This tests file/package independence, not execution of a different driver.
 reader_digest=hashlib.sha256(binary.read_bytes()).digest()
 package=manifest.parent/'.package.json';metadata=json.loads(package.read_text())
 config['version']='0.2.2';manifest.write_text(json.dumps(config))
 metadata['version']=config['version']
 for entry in metadata['entries']:
  if entry['name']=='manifest.json':
   entry['size_bytes']=len(manifest.read_bytes())
   entry['sha256']=hashlib.sha256(manifest.read_bytes()).hexdigest()
 package.write_text(json.dumps(metadata));run(True)
 assert hashlib.sha256(binary.read_bytes()).digest()==reader_digest
 print('External-file origin, valid packages, corrupt/missing ELF, version mismatch, traversal, independent version update: PASS')
assert not (root/'src/platform/x4pro_embedded.c').exists()
assert 'x4_embedded' not in (root/'src/platform/X4DiagnosticBoot.cpp').read_text()
assert 'loadBootstrapPackages' in (root/'src/platform/X4DiagnosticBoot.cpp').read_text()
