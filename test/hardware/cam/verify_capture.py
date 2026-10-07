#!/usr/bin/env python3
"""Validate one saved CAM runtime batch; never opens a device or alters capture."""
import argparse
import json
import re
from pathlib import Path


def verify(raw: bytes):
    if len(raw) > 256 * 1024:
        raise ValueError('capture exceeds bound')
    text = raw.decode('utf-8', 'replace')
    if any(x in text for x in ('Guru Meditation', 'Backtrace:', 'result=failed')):
        raise ValueError('firmware reported failure')
    lines = []
    for line in text.splitlines():
        if len(line) > 4096:
            raise ValueError('line exceeds bound')
        # A dependency include collision in the first physical build used the
        # old logger's prefix. The initial SDK line can precede the boot label.
        if 'DIAG_RUNTIME ' in line:
            line = 'RUNTIME ' + line.split('DIAG_RUNTIME ', 1)[1]
        elif 'RUNTIME BOOT profile=' in line and line.startswith('['):
            line = line[line.index('RUNTIME BOOT profile='):]
        lines.append(line)
    def need(condition, description):
        if not condition:
            raise ValueError(description)
    need(sum('profile=cam-offline u1=480bf345' in x for x in lines) == 1, 'one exact boot required')
    need(any(x.startswith('RUNTIME BOOT mac=28:84:85:4b:57:98 app=0x10000 flash=16777216 ') for x in lines), 'CAM identity/layout missing')
    for state in ('Recover', 'Inventory', 'Prepare', 'Bind', 'Running'):
        need(lines.count('RUNTIME BOOT state=' + state) == 2, 'two normal boot phases required: '+state)
    need(lines.count('RUNTIME BOOT state=Cold') == 1 and lines.count('RUNTIME QUAL restart=1') == 1, 'clean stop/restart required')
    for package in ('platform-clock-v1', 'archive-zip'):
        need(lines.count('RUNTIME BOOT recover id='+package+' result=2') == 2, 'verified recovery required')
        need(sum(x.startswith('RUNTIME BOOT bind id='+package+' granted=1 ') for x in lines) == 2, 'normal lease grant required')
    for cycle in (1, 2):
        need(lines.count(f'RUNTIME QUAL cycle={cycle} result=0') == 1, 'consumer failed')
    need(sum(x.startswith('DIAG_STREAM cleanup=1 revoked=1 fresh_generation=1 failed=none') for x in lines) == 2, 'stream cleanup missing')
    files = [re.fullmatch(r'DIAG_INSTALLED path=(\S+) bytes=(\d+) sha256=([0-9a-f]{64}) exact=1', x) for x in lines if x.startswith('DIAG_INSTALLED ')]
    need(len(files) == 16 and all(files), 'all eight installed hashes required twice')
    rounds = [[m.groups() for m in files[i:i+8]] for i in (0,8)]
    need(rounds[0] == rounds[1] and len({x[0] for x in rounds[0]}) == 8, 'installed generations changed')
    marker = 'RUNTIME QUAL result=pass cycles=2 files=8 steady=Running'
    need(lines.count(marker) == 1, 'batch completion missing')
    beats = [x for x in lines[lines.index(marker)+1:] if x.startswith('RUNTIME BOOT heartbeat state=Running ')]
    need(len(beats) >= 3, 'three post-completion heartbeats required')
    need(all('handles=0 ' in x and ('grants=1' in x or 'has_grants=1' in x) for x in beats), 'steady resources missing')
    return {'result':'pass', 'normal_cycles':2, 'installed_files_exact':8, 'post_completion_heartbeats':len(beats), 'last_heartbeat':beats[-1], 'installed_files':[dict(zip(('path','bytes','sha256'), x)) for x in rounds[0]]}


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('capture', type=Path)
    args = parser.parse_args()
    print(json.dumps(verify(args.capture.read_bytes()), indent=2))
