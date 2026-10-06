#!/usr/bin/env python3
"""Add a read-only keyboard registration case to the published real-package fixture.

Inputs: checkout carrying c314652a's test/installed_provider_registration,
unmodified source checkout, installed ArduinoJson/src, isolated output directory.
No input checkout is changed. This proves registration before target activation;
it cannot run the Xtensa USB hardware drivers on the host.
"""
from pathlib import Path
import argparse,hashlib,json,os,shutil,subprocess
p=argparse.ArgumentParser();p.add_argument('evidence_checkout',type=Path);p.add_argument('source_checkout',type=Path);p.add_argument('arduino_json',type=Path);p.add_argument('output',type=Path);p.add_argument('--sanitize',action='store_true');a=p.parse_args()
source=a.evidence_checkout.resolve()/'test/installed_provider_registration'
out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
dst=out/'fixture'
shutil.copytree(source,dst,dirs_exist_ok=True)
hashes={str(x.relative_to(source)):hashlib.sha256(x.read_bytes()).hexdigest() for x in source.rglob('*') if x.is_file()}
(out/'fixture-source-hashes.json').write_text(json.dumps(hashes,indent=2)+'\n')
runner=dst/'run_test.py';s=runner.read_text();s=s.replace("choices=['navigation', 'touch-first', 'capacity', 'faults']","choices=['navigation', 'touch-first', 'capacity', 'faults', 'keyboard']");runner.write_text(s)
fixture=dst/'regression.cpp';s=fixture.read_text();insert=r'''
static void keyboardReachability() {
    for(bool touchFirst:{false,true}) {
        boot();prime();
        if(touchFirst)measured("touch-first-keyboard-witness",[&]{assert(registration("input.touch.raw"));});
        auto first=measured(touchFirst?"direct-keyboard-after-touch":"direct-keyboard-first",[&]{assert(registration("usb.hid.keyboard"));});
        assert(graph->moduleCount()==(touchFirst?15:14));
        assert(pinCount==graph->moduleCount() && first.work.failures==0 && first.work.peak<=7);
        assert(graph->hasProviderId("usb-hid-keyboard"));
        if(!touchFirst) {
            measured("touch-after-keyboard-witness",[&]{assert(registration("input.touch.raw"));});
            assert(graph->moduleCount()==15 && pinCount==15);
        }
        cleanup();
    }
}
'''
s=s.replace('int main(int argc, char** argv) {',insert+'\nint main(int argc, char** argv) {')
s=s.replace('else if (which == "faults") faults(original);','else if (which == "faults") faults(original);\n    else if (which == "keyboard") keyboardReachability();')
fixture.write_text(s)
env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0:halt_on_error=1','UBSAN_OPTIONS':'halt_on_error=1'}
subprocess.run(['python3',str(runner),str(a.arduino_json.resolve()),'--source-root',str(a.source_checkout.resolve()),'--expect-original','--case','keyboard',*(['--sanitize'] if a.sanitize else [])],check=True,env=env)
