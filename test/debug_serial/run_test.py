#!/usr/bin/env python3
"""Production console dispatch/parser versus pinned pre-repair Stream behavior."""
from pathlib import Path
import argparse, hashlib, json, os, subprocess, tempfile
p=argparse.ArgumentParser();p.add_argument('--sanitize',action='store_true');p.add_argument('--negative-control',action='store_true');args=p.parse_args()
here=Path(__file__).resolve().parent;root=here.parents[1]
manifest=json.loads((here/'reference.json').read_text())
for path,want in manifest['fixtures'].items():
    assert hashlib.sha256((here/path).read_bytes()).hexdigest()==want,path
main=(root/'src/main.cpp').read_text()
start=main.index('  // Keep fragmented console input')
block=main[start:main.index('  // Check for any user activity',start)]
assert 'readStringUntil' not in block
assert main.index('serialCommand.poll') < main.index('(void)serviceIdleSleep(userActivity)') < main.index('activityManager.loop();',main.index('serialCommand.poll'))
assert 'yield();' in main and 'delay(10);' in main
with tempfile.TemporaryDirectory(prefix='debug-serial-') as tmp:
    tmp=Path(tmp);(tmp/'current_dispatch.inc').write_text((here/'original_dispatch.inc').read_text() if args.negative_control else block)
    exe=tmp/'regression'
    flags=['-std=c++17','-Wall','-Wextra','-Werror','-O1','-g']
    if args.negative_control:flags+=['-DNEGATIVE_CONTROL=1']
    if args.sanitize:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie']
    subprocess.run([os.environ.get('CXX','c++'),*flags,'-I'+str(root/'src'),'-I'+str(here),'-I'+str(tmp),str(here/'regression.cpp'),'-o',str(exe)],check=True)
    result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=60)
    if args.negative_control:
        assert result.returncode == -6 and 'reads-before<=' in result.stderr, result
        print('PASS: unchanged original independently fails the per-poll read bound')
    else:
        print(result.stdout,end='')
        if result.returncode:print(result.stderr)
        result.check_returncode()
