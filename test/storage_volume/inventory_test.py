#!/usr/bin/env python3
"""Real SD/FatFs/HAL/package inspector regression with 40 bounded providers.

Three synthetic milliseconds per sector test the unchanged inventory deadline;
they are not a device benchmark. Payloads exercise metadata/header inspection.
"""
from pathlib import Path
import subprocess
import tempfile
import platform
import sys
import os
SANITIZER = "undefined" if platform.system() == "Darwin" else "address,undefined"
ENV = dict(os.environ, ASAN_OPTIONS="detect_leaks=0", UBSAN_OPTIONS="halt_on_error=1")
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'test/storage_volume'))
from mmio_boundary import header
with tempfile.TemporaryDirectory(prefix='inventory-regression-') as temp:
    build=Path(temp)
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
    subprocess.run([*common, *extra, 'test/storage_volume/inventory_test.cpp', 'src/runtime/packages/InstalledCapabilityResolver.cpp', 'src/runtime/packages/PackageOrdinarySdAdapter.cpp', 'src/runtime/packages/PackageCdcSdMigration.cpp',
                    'lib/hal/HalStorageVolume.cpp', *objects, '-lcrypto', '-o', str(binary)], cwd=ROOT, check=True)
    subprocess.run([str(binary)],cwd=ROOT,check=True,timeout=120,env=ENV)
