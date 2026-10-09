#!/usr/bin/env python3
"""Build original and current default entry points at identical source paths."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[2]
BASE='bf997f3d777e644e98c2f885a9c9f4c81e1a0e48'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def run_old(script,args,env):
    source=subprocess.check_output(['git','show',BASE+':scripts/'+script],cwd=ROOT,text=True)
    runner='import sys; sys.path.insert(0,'+repr(str(ROOT/'scripts'))+'); sys.argv='+repr([str(ROOT/'scripts'/script),*args])+'; exec(compile('+repr(source)+','+repr(str(ROOT/'scripts'/script))+',"exec"), {"__name__":"__main__","__file__":'+repr(str(ROOT/'scripts'/script))+'})'
    subprocess.run([sys.executable,'-c',runner],cwd=ROOT,env=env,check=True)
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--cc',required=True);p.add_argument('--compile-database',required=True);p.add_argument('--idf-source',required=True);p.add_argument('--receipt',type=Path,required=True);a=p.parse_args()
    env=dict(os.environ,NATIVE_DRIVER_CC=a.cc);results=[]
    # Verify every tracked original driver file and SDK input is unchanged.
    paths=subprocess.check_output(['git','ls-tree','-r','--name-only',BASE,'Drivers','sdk/driver'],cwd=ROOT,text=True).splitlines()
    for path in paths:
        original=subprocess.check_output(['git','show',BASE+':'+path],cwd=ROOT)
        if (ROOT/path).read_bytes()!=original:raise ValueError('Default source changed: '+path)
    for source,identity in [('usb_host_v2','usb-host-v2'),('usb_hid','usb-hid'),('usb_hid_keyboard','usb-hid-keyboard'),('usb_hid_mouse','usb-hid-mouse'),('usb_hid_gamepad','usb-hid-gamepad'),('usb_xinput_gamepad','usb-xinput-gamepad'),('usb_hid_text_input','usb-hid-text-input')]:
        script='build_'+source+'.py';elf=ROOT/'dist/experimental'/identity/'driver.elf'
        run_old(script,[],env);before=elf.read_bytes()
        subprocess.run([sys.executable,str(ROOT/'scripts'/script)],env=env,cwd=ROOT,check=True)
        if elf.read_bytes()!=before:raise ValueError('Flag-off ELF bytes differ: '+identity)
        results.append({'profile':identity,'sha256':sha(elf),'bytes':len(before),'full_elf_identical':True})
    for native in (False,True):
        args=['--link-experiment','--compile-database',a.compile_database,'--idf-source',a.idf_source]+(['--native-phy-lease'] if native else [])
        folder='usb-controller-esp32s3'+('-native-phy' if native else '')
        elf=ROOT/'dist/experimental'/folder/'controller-link-experiment.elf'
        run_old('probe_usb_controller_esp32s3.py',args,env);before=elf.read_bytes()
        subprocess.run([sys.executable,str(ROOT/'scripts/probe_usb_controller_esp32s3.py'),*args],cwd=ROOT,env=env,check=True)
        if elf.read_bytes()!=before:raise ValueError('Flag-off controller bytes differ: '+folder)
        results.append({'profile':folder,'sha256':sha(elf),'bytes':len(before),'full_elf_identical':True})
    a.receipt.write_text(json.dumps({'baseline':BASE,'checked_original_files':len(paths),'profiles':results},indent=2)+'\n')
    print('Full flag-off ELF byte identity: all nine original profiles PASS')
if __name__=='__main__':main()
