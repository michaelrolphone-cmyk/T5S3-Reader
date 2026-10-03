#!/usr/bin/env python3
"""Build the actual provider/FatFs/adapter against a native SD card wire model."""
from pathlib import Path
import subprocess
import tempfile
import platform
import sys
import os
SANITIZER = "undefined" if platform.system() == "Darwin" else "address,undefined"
ENV = dict(os.environ, ASAN_OPTIONS="detect_leaks=0", UBSAN_OPTIONS="halt_on_error=1")
ROOT = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory() as temp:
    build = Path(temp)
    objects=[]
    for source in ('Drivers/x4pro_sd/driver.c','Drivers/x4pro_sd/fatfs/ff.c','Drivers/x4pro_sd/fatfs/ffunicode.c'):
        out=build/(Path(source).name+'.o')
        subprocess.run(['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-overflow','-fsanitize='+SANITIZER,
                        '-Isdk/driver','-Itest/storage_volume/fake','-IDrivers/x4pro_board','-c',source,'-o',str(out)],cwd=ROOT,check=True)
        objects.append(str(out))
    extra = []
    if len(sys.argv) > 1:
        # Keep -Werror on our sources; upstream ArduinoJson is a system header.
        extra = ['-DTEST_APP_PARSER', '-DCROSSPOINT_VERSION="1.3.74"',
                 '-isystem', sys.argv[1], '-Isrc', '-Ilib/NativeApps/include', 'src/native/AppManifest.cpp']
    binary=build/'storage'
    subprocess.run(['c++','-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize='+SANITIZER,'-DBOARD_XTEINK_X4_PRO','-Wno-overloaded-virtual',
                    '-Itest/storage_volume/stubs','-Ilib/hal','-Isdk/driver',
                    'test/storage_volume/runtime_test.cpp','lib/hal/HalStorageVolume.cpp',*extra,*objects,'-o',str(binary)],cwd=ROOT,check=True)
    for layout in ('superfloppy','mbr'):
        for failure in ([], ['busy-timeout']):
            subprocess.run([str(binary),layout,*failure],cwd=ROOT,check=True,timeout=120,env=ENV)
