#!/usr/bin/env python3
"""Compile unchanged provider chain and owner input bodies; mock physical USB only.

Graph admission is pre-established by the existing loader stub; no package
discovery or target ELF execution is modeled. Optional timeout advancement is
deterministic source-path evidence, never measured USB/ESP32 latency.
"""
import argparse,hashlib,json,os,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('checkout',type=Path);p.add_argument('output',type=Path);p.add_argument('--sanitize',action='store_true');a=p.parse_args()
r=a.checkout.resolve();o=a.output.resolve();o.mkdir(parents=True,exist_ok=True)
paths=['Drivers/'+n+'/driver.c' for n in ['usb_host_v2','usb_hid','usb_hid_keyboard','usb_hid_text_input','usb_ui_navigation']]
paths+=['src/native/NativeNavigationInput.cpp','src/MappedInputManager.cpp','Drivers/usb_controller_esp32s3/driver_base.cpp','Apps/text_editor.c']
(o/'source-hashes.json').write_text(json.dumps({x:hashlib.sha256((r/x).read_bytes()).hexdigest() for x in paths},indent=2)+'\n')
flags=['-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation']
if a.sanitize:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
modules=[]
for path in paths[:5]:
    dest=o/(path.split('/')[1]+'.so');modules.append(str(dest))
    subprocess.run(['cc','-std=c11',*flags,'-fPIC','-fvisibility=hidden','-shared','-I'+str(r/'sdk/driver'),str(r/path),'-o',str(dest)],check=True)
text=(r/'src/MappedInputManager.cpp').read_text();start=text.index('void MappedInputManager::update() const {');end=text.index('\n}',start)+2
body=text[start:end]
fixture=Path(__file__).with_name('keyboard_retry_probe.cpp').read_text()
def method(text,signature):
    start=text.index(signature);end=text.index('{',start)+1;depth=1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[start:end]
controller=(r/'Drivers/usb_controller_esp32s3/driver_base.cpp').read_text()
fixture=fixture.replace('CONTROLLER_FUNCTIONS','\n'.join(method(controller,s) for s in ['void complete_transfer(', 'bool wait_completion(', 'bool idle_transfer(', 'int32_t control(']))
fixture=fixture.replace('EDITOR_COLLECT_FUNCTION',method((r/'Apps/text_editor.c').read_text(),'static void collect_keyboard('))
# Header-only shim changes no production statements.
fixture=fixture.replace('Gpio gpio;','mutable Gpio gpio;')+'\n'+body+'\n'
(o/'probe.cpp').write_text(fixture)
includes=['sdk/driver','src','src/native','test/streams/stubs','test/drivers/stubs']
subprocess.run(['c++','-std=c++17',*flags,'-Wno-missing-field-initializers',*['-I'+str(r/x) for x in includes],str(o/'probe.cpp'),'-ldl','-o',str(o/'probe')],check=True)
env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0:halt_on_error=1','UBSAN_OPTIONS':'halt_on_error=1'}
rows=[]
for s in ['absent','healthy','transient','persistent-immediate','persistent-slow-failure','two-failed','failed-plus-healthy','recovery','reconnect','undrained-timeout','text-editor-direct']:
    result=subprocess.run([str(o/'probe'),*modules,s],check=True,capture_output=True,text=True,env=env,timeout=30)
    print(result.stdout,end='');rows.extend(json.loads(x) for x in result.stdout.splitlines())
(o/'results.json').write_text(json.dumps(rows,indent=2)+'\n')
