#!/usr/bin/env python3
"""Exercise production SD boot package validation with a host file reader."""
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
 # Both board profiles pass the same production reader; no ELF execution here.
 t5=root/'dist/t5s3-independent-packages/sdcard'
 if t5.exists():
  t5fixture=d/'t5boot';shutil.copytree(t5,t5fixture)
  subprocess.run([str(binary),str(t5fixture),'t5s3-pro'],check=True,timeout=20)
  # The ordinary catalog route must retain the same ABI3 source selection as
  # board staging; exercise the actual loader, not only archive hash checks.
  catalog_display=root/'dist/packages/display-epd-video'
  if (catalog_display/'manifest.json').exists():
   target=t5fixture/'Drivers/display-epd-video'
   shutil.rmtree(target);shutil.copytree(catalog_display,target)
   subprocess.run([str(binary),str(t5fixture),'t5s3-pro'],check=True,timeout=20)
   (target/'manifest.json').unlink()
   rejected=subprocess.run([str(binary),str(t5fixture),'t5s3-pro'],capture_output=True,timeout=20)
   assert rejected.returncode!=0,'ABI3 ordinary package admitted without its source manifest'
   print('Ordinary catalog ABI3 package admission and missing-manifest rejection: PASS')
 fixture=d/'sdcard';shutil.copytree(root/'dist/x4-independent-packages/sdcard',fixture)
 def run(ok):
  p=subprocess.run([str(binary),str(fixture),'xteink-x4-pro'],capture_output=True,text=True,timeout=20)
  assert (p.returncode==0)==ok,p.stdout+p.stderr
  return p.stdout
 print(run(True).strip())
 boot=fixture/'System/Config/boot.json'; original=boot.read_bytes(); config=json.loads(original)
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
 # Coherent ABI2 metadata reaches the manager unchanged; mismatches and
 # unsupported/coerced revisions fail before registration of this provider.
 profile=manifest.parent/'provider-abi.v1'; original_profile=profile.read_bytes()
 def revision_case(value,profile_revision,ok):
  config['os_cpu_abi']=value;manifest.write_text(json.dumps(config))
  profile.write_bytes(original_profile.replace(b'os-cpu-abi=1',f'os-cpu-abi={profile_revision}'.encode()))
  for entry in metadata['entries']:
   if entry['name'] in ('manifest.json','provider-abi.v1'):
    payload=(manifest.parent/entry['name']).read_bytes()
    entry['size_bytes']=len(payload);entry['sha256']=hashlib.sha256(payload).hexdigest()
  package.write_text(json.dumps(metadata))
  return run(ok)
 assert 'os_cpu_abi=2' in revision_case(2,2,True)
 for value,profile_revision in [(1,2),(2,1),(4,4),(None,1),(True,1),('2',2)]:
  revision_case(value,profile_revision,False)
 # Duplicate ABI keys must not silently select the last value, even when
 # the package digest coherently describes those exact malformed bytes.
 config['os_cpu_abi']=1
 malformed=json.dumps(config).replace('"os_cpu_abi": 1','"os_cpu_abi": 2, "os_cpu_abi": 1')
 manifest.write_text(malformed);profile.write_bytes(original_profile)
 for entry in metadata['entries']:
  if entry['name'] in ('manifest.json','provider-abi.v1'):
   payload=(manifest.parent/entry['name']).read_bytes()
   entry['size_bytes']=len(payload);entry['sha256']=hashlib.sha256(payload).hexdigest()
 package.write_text(json.dumps(metadata));run(False)
 # The shared gate also protects the boot profile itself.
 manifest.write_text(json.dumps(config))
 for entry in metadata['entries']:
  if entry['name']=='manifest.json':
   payload=manifest.read_bytes();entry['size_bytes']=len(payload);entry['sha256']=hashlib.sha256(payload).hexdigest()
 package.write_text(json.dumps(metadata));run(True)
 # Build a deterministic duplicate from parsed JSON, independent of whitespace.
 boot_config=json.loads(original);boot.write_text(json.dumps(boot_config)[:-1]+',"board":"xteink-x4-pro"}')
 run(False);boot.write_bytes(original)
 print('Bootstrap revision selection, manifest/profile mismatch and invalid revisions: PASS')
 print('External-file origin, valid packages, corrupt/missing ELF, version mismatch, traversal, independent version update: PASS')
assert not (root/'src/platform/x4pro_embedded.c').exists()
assert 'x4_embedded' not in (root/'src/platform/X4DiagnosticBoot.cpp').read_text()
assert 'loadPlatformSdPackages' in (root/'src/platform/X4DiagnosticBoot.cpp').read_text()
# Keep the ownership barrier and real read-only parser regression in the
# existing host CI entrypoint, without adding a second workflow/controller.
for check in ('handoff_test.py', 'read_only_fat_test.py', 'artifact_test.py'):
 subprocess.run([sys.executable,str(root/'test/bootstrap_store'/check)],check=True,timeout=30)
