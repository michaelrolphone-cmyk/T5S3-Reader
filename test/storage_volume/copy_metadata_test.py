#!/usr/bin/env python3
"""Copied package metadata regression using real SD/FatFs/HAL and inspection.

The default 40-package fixture is reproducible and checks real SHA-256s.
Pass --archive PATH for the delivered X4 SD ZIP without rewriting its bytes.
--expect-rejection records the original defect on an unpatched checkout.
Modeled milliseconds per sector are bounds checks, not device benchmarks.
"""
from pathlib import Path
import subprocess
import tempfile
import platform
import sys
import os
import argparse
import hashlib
import json
import zipfile
SANITIZER = "undefined" if platform.system() == "Darwin" else "address,undefined"
ENV = dict(os.environ, ASAN_OPTIONS="detect_leaks=0", UBSAN_OPTIONS="halt_on_error=1")
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'test/storage_volume'))
from mmio_boundary import header
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--archive',type=Path)
parser.add_argument('--expect-rejection',action='store_true')
parser.add_argument('--providers',type=int,default=40,choices=(9,40))
parser.add_argument('--sector-ms',type=int,default=3,choices=(2,3))
args=parser.parse_args()
with tempfile.TemporaryDirectory(prefix='copy-metadata-regression-') as temp:
    build=Path(temp)
    fixture=build/'fixture'; fixture.mkdir()
    paths=[]
    if args.archive:
        archive=args.archive.read_bytes()
        print(f'Archive: {args.archive.name} bytes={len(archive)} sha256={hashlib.sha256(archive).hexdigest()}',flush=True)
        with zipfile.ZipFile(args.archive) as bundle:
            for info in bundle.infolist():
                if not info.filename.startswith('sdcard/Drivers/') or info.is_dir():
                    continue
                relative=Path(info.filename).relative_to('sdcard')
                assert len(relative.parts)==3 and all(part not in ('','..','.') for part in relative.parts)
                target=fixture/relative;target.parent.mkdir(parents=True,exist_ok=True)
                target.write_bytes(bundle.read(info));paths.append('/'+relative.as_posix())
    else:
        packages=[
            ('platform-clock-v1','platform.clock',1,[]),
            ('x4pro-battery','board.battery',1,[('i2c.bus',1)]),
            ('x4pro-buttons','input.navigation',1,[]),
            ('x4pro-frontlight','display.frontlight',1,[]),
            ('x4pro-gt911','input.touch.raw',1,[('i2c.bus',1),('platform.clock',1)]),
            ('x4pro-i2c','i2c.bus',1,[('platform.clock',1)]),
            ('x4pro-panel','display.output',1,[('platform.clock',1)]),
            ('x4pro-rtc','rtc.clock',2,[('i2c.bus',1)]),
            ('x4pro-sd','storage.volume',1,[('platform.clock',1)])]
        packages += [(f'test-extra-{index}',f'test.extra{index}',1,[]) for index in range(args.providers-9)]
        for identity,capability,api,requirements in packages:
            elf=bytearray(64);elf[:6]=b'\x7fELF\x01\x01';elf[6]=1;elf[16]=3;elf[18]=94;elf[20]=1
            files={'driver.elf':bytes(elf),'privileged-imports.v1':b'\n',
                   'manifest.json':b'{"os_cpu_abi":1}',
                   'provider-abi.v1':f'os-cpu-abi=1\nprovides={capability}\napi={api}\n'.encode()}
            manifest={'schema':1,'kind':'driver','id':identity,'version':'1.0.0','artifact':'driver.elf',
                      'architecture':'xtensa-esp32s3','min_runtime_api':2,
                      'entries':[{'name':name,'size_bytes':len(data),'sha256':hashlib.sha256(data).hexdigest(),
                                  'executable':name=='driver.elf'} for name,data in files.items()],
                      'requires':[{'capability':name,'min_api':version} for name,version in requirements]}
            files['.package.json']=json.dumps(manifest,separators=(',',':')).encode()
            directory=fixture/'Drivers'/identity;directory.mkdir(parents=True)
            for name,data in files.items():
                (directory/name).write_bytes(data);paths.append(f'/Drivers/{identity}/{name}')
    expected=9 if args.archive else args.providers
    assert len(paths)==expected*5 and len(set(Path(path).parent for path in paths))==expected
    listing=build/'files.txt';listing.write_text('\n'.join(sorted(paths))+'\n')
    wire=header(ROOT)
    wire=wire.replace('extern unsigned card_reads, card_writes;', 'extern unsigned card_reads, card_writes; extern uint64_t card_time; extern unsigned inventory_sector_ms;')
    wire=wire.replace('++card_reads;', '++card_reads; card_time += inventory_sector_ms;')
    (build/'x4pro_mmio.h').write_text(wire)
    objects=[]
    for source in ('Drivers/x4pro_sd/driver.c',
                   'Drivers/storage_fatfs/fatfs/ff.c','Drivers/storage_fatfs/fatfs/ffunicode.c','test/storage_volume/os_cpu_fake.c'):
        out=build/(Path(source).name+'.o')
        subprocess.run(['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-overflow','-fsanitize='+SANITIZER,
                        '-pthread','-D_XOPEN_SOURCE=700','-Isdk/driver','-I'+str(build),'-IDrivers/x4pro_board','-c',source,'-o',str(out)],cwd=ROOT,check=True)
        objects.append(str(out))
    common = ['c++', '-std=c++17', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
              '-fsanitize='+SANITIZER, '-pthread', '-DBOARD_XTEINK_X4_PRO', '-Wno-overloaded-virtual',
              '-DARDUINO_ARCH_ESP32','-Itest/storage_volume','-Itest/storage_volume/inventory_stubs','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-Itest/storage_volume/stubs',  '-include', 'Arduino.h', '-Ilib/hal', '-Isdk/driver']
    extra = ['-Isrc', '-Ilib/NativeApps/include']
    binary=build/'storage'
    subprocess.run([*common, *extra, 'test/storage_volume/copy_metadata_test.cpp', 'src/runtime/packages/InstalledCapabilityResolver.cpp', 'src/runtime/packages/PackageOrdinarySdAdapter.cpp', 'src/runtime/packages/PackageCdcSdMigration.cpp',
                    'lib/hal/HalStorageVolume.cpp', *objects, '-lcrypto', '-o', str(binary)], cwd=ROOT, check=True)
    subprocess.run([str(binary),str(fixture),str(listing),str(args.sector_ms),*(['--expect-rejection'] if args.expect_rejection else [])],cwd=ROOT,check=True,timeout=120,env=ENV)
