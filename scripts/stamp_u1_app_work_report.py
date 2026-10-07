#!/usr/bin/env python3
"""Record provenance for a host comparison over this build's actual app pairs."""
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
ROOT = Path(__file__).resolve().parents[1]
report, directory, requested = Path(sys.argv[1]), Path(sys.argv[2]), sys.argv[3]
if not re.fullmatch(r'[0-9a-f]{40}', requested):
    raise ValueError('exact requested source SHA required')
data = json.loads(report.read_text())
pairs = []
total = 0
for elf in sorted(directory.glob('*.elf')):
    sidecar = elf.with_suffix('.json')
    if len(pairs) >= 128 or elf.is_symlink() or sidecar.is_symlink():
        raise ValueError('invalid app set')
    size = elf.stat().st_size
    if not 52 <= size <= 8*1024*1024 or not 0 < sidecar.stat().st_size <= 2048:
        raise ValueError('app input outside bound')
    total += size
    if total > 128*1024*1024:
        raise ValueError('app dataset outside bound')
    pairs.append({'elf': elf.name, 'bytes': size,
                  'elf_sha256': hashlib.sha256(elf.read_bytes()).hexdigest(),
                  'sidecar_sha256': hashlib.sha256(sidecar.read_bytes()).hexdigest()})
if len(pairs) != data['packages'] or total != data['elf_bytes']:
    raise ValueError('measurement dataset changed')
sources = ['src/native/AppManifest.cpp','src/native/AppPackageInstaller.cpp','lib/hal/HalStorage.cpp',
           'test/hal/storage_stubs/SdFat.h','test/resources/cdc_sd_stubs/mbedtls/sha256.h',
           'test/native_apps/pair_stubs/freertos/task.h','test/native_apps/app_pair_snapshot_test.cpp','test/run_app_manifest_test.sh']
data.update(source_sha256={name: hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in sources},
            host_compiler=subprocess.check_output(['c++','--version'],text=True).splitlines()[0],
            requested_head=requested,
            compiled_checkout=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
            dataset=pairs,
            method='Real AppManifest/AppPackageInstaller/HalStorage on in-memory fault-media adapters; fixture population excluded',
            limits='Host reference full-verifier versus current metadata inspection; not historical firmware or physical SD/launch timings')
report.write_text(json.dumps(data, indent=2)+'\n')
print(f"Measured {len(pairs)} built app pairs; full SHA retained, installed inspection hashes zero ELF bytes")
