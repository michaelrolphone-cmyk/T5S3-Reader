#!/usr/bin/env python3
"""Build the actual provider/FatFs/adapter against a native SD card wire model."""
from pathlib import Path
import subprocess
import tempfile
import platform
import sys
import os
from bootstrap_handoff import header as bootstrap_header
from mmio_boundary import header as mmio_header
SANITIZER = "undefined" if platform.system() == "Darwin" else "address,undefined"
ENV = dict(os.environ, ASAN_OPTIONS="detect_leaks=0", UBSAN_OPTIONS="halt_on_error=1")
ROOT = Path(__file__).resolve().parents[2]
SPI = os.environ.get('STORAGE_TRANSPORT') == 'spi'
with tempfile.TemporaryDirectory() as temp:
    build = Path(temp)
    objects=[]
    if not SPI:
        (build/'bootstrap_handoff.inc').write_text(bootstrap_header(ROOT))
        (build/'x4pro_mmio.h').write_text(mmio_header(ROOT))
    for source in ('Drivers/t5s3_sd/driver.c' if SPI else 'Drivers/x4pro_sd/driver.c',
                   'Drivers/storage_fatfs/fatfs/ff.c','Drivers/storage_fatfs/fatfs/ffunicode.c',
                   'test/storage_volume/os_cpu_fake.c'):
        out=build/(Path(source).name+'.o')
        subprocess.run(['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-overflow','-fsanitize='+SANITIZER,
                        '-pthread','-D_XOPEN_SOURCE=700','-Isdk/driver','-I'+str(build),'-Itest/storage_volume/fake','-IDrivers/x4pro_board','-c',source,'-o',str(out)],cwd=ROOT,check=True)
        objects.append(str(out))
    common = ['c++', '-std=c++17', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
              '-fsanitize='+SANITIZER, '-pthread', '-DBOARD_T5S3_PRO' if SPI else '-DBOARD_XTEINK_X4_PRO', '-Wno-overloaded-virtual',
              '-Itest/storage_volume/stubs', '-Ilib/hal', '-Isdk/driver', '-I'+str(build)]
    extra = []
    if SPI: common.append('-DTEST_SPI_TRANSPORT')
    if len(sys.argv) > 1:
        extra = ['-DTEST_APP_PARSER', '-DCROSSPOINT_VERSION="1.3.74"',
                 '-isystem', sys.argv[1], '-Isrc', '-Ilib/NativeApps/include']
        # GCC emits maybe-uninitialized for ArduinoJson's empty iterator after
        # inlining, even with -isystem. Keep that diagnostic visible but not
        # fatal in the parser TU only. Provider/HAL/test retain full -Werror;
        # every TU retains the runtime sanitizers.
        compiler_version = subprocess.check_output(['c++', '--version'], text=True)
        parser_flags = [] if 'clang' in compiler_version.lower() else ['-Wno-error=maybe-uninitialized']
        parser = build/'AppManifest.o'
        subprocess.run([*common, *extra, *parser_flags, '-c', 'src/native/AppManifest.cpp',
                        '-o', str(parser)], cwd=ROOT, check=True)
        objects.append(str(parser))
    binary=build/'storage'
    subprocess.run([*common, *extra, 'test/storage_volume/runtime_test.cpp',
                    'lib/hal/HalStorageVolume.cpp', *objects, '-o', str(binary)], cwd=ROOT, check=True)
    if not SPI:
        for edge in ('clock-registers', 'clock-stuck-counter', 'clock-stuck-readback'):
            subprocess.run([str(binary),edge],cwd=ROOT,check=True,timeout=120,env=ENV)
        subprocess.run([str(binary),'bootstrap-hold'],cwd=ROOT,check=True,timeout=120,env=ENV)
        # SD0.2.1's init sequence never released the bootstrap hold. The exact
        # historical provider was reproduced during investigation; this bounded
        # negative control removes only that transition from the current driver.
        # Keep all real transport/FatFs/mutex code and avoid a frozen driver fork.
        original=(ROOT/'Drivers/x4pro_sd/driver.c').read_text()
        release='    x4pro_pin_hold(X4PRO_PIN_SD_PWR, false);'
        assert original.count(release)==1
        legacy=original.replace(release,'    /* SD0.2.1 init: no hold release. */')
        legacy=legacy.replace('#include "../x4pro_i2c/os_cpu_v1.h"',
                              '#include "'+str(ROOT/'Drivers/x4pro_i2c/os_cpu_v1.h')+'"')
        legacy=legacy.replace('#include "../storage_fatfs/volume.c"',
                              '#include "'+str(ROOT/'Drivers/storage_fatfs/volume.c')+'"')
        legacy_source=build/'legacy-hold.c'; legacy_source.write_text(legacy)
        legacy_object=build/'legacy-hold.o'
        subprocess.run(['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-overflow','-fsanitize='+SANITIZER,
                        '-pthread','-D_XOPEN_SOURCE=700','-Isdk/driver','-I'+str(build),'-Itest/storage_volume/fake','-IDrivers/x4pro_board',
                        '-c',str(legacy_source),'-o',str(legacy_object)],cwd=ROOT,check=True)
        legacy_binary=build/'legacy-hold'
        subprocess.run([*common,*extra,'test/storage_volume/runtime_test.cpp','lib/hal/HalStorageVolume.cpp',
                        str(legacy_object),*objects[1:],'-o',str(legacy_binary)],cwd=ROOT,check=True)
        negative=subprocess.run([str(legacy_binary),'bootstrap-hold'],cwd=ROOT,timeout=120,env=ENV,
                                text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        assert negative.returncode != 0 and 'bootstrap SD release required' in negative.stderr, negative.stderr
        assert 'hold=1 power_off=1 mounted=0 reason=CMD8 response invalid sectors=0' in negative.stderr, negative.stderr
        print('legacy SD0.2.1 hold transition independently fails the bootstrap regression')
        old_tick = "static bool tick(void) {\n    (void)selected_phase;\n    x4pro_pin_output(X4PRO_PIN_SD_CLK, true);\n    x4pro_pin_output(X4PRO_PIN_SD_CLK, false);\n    return true;\n}\n"
        begin = original.index('static bool tick(void) {')
        end = original.index('static bool wait_dat0(', begin)
        slow = original[:begin] + old_tick + original[end:]
        for include in ('../x4pro_i2c/os_cpu_v1.h', '../storage_fatfs/volume.c'):
            slow = slow.replace('"' + include + '"', '"' + str((ROOT/'Drivers/x4pro_sd'/include).resolve()) + '"')
        slow_source = build/'old-clock.c'; slow_source.write_text(slow)
        slow_object = build/'old-clock.o'
        subprocess.run(['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-overflow','-fsanitize='+SANITIZER,
                        '-pthread','-D_XOPEN_SOURCE=700','-Isdk/driver','-I'+str(build),'-IDrivers/x4pro_board',
                        '-c',str(slow_source),'-o',str(slow_object)],cwd=ROOT,check=True)
        slow_binary = build/'old-clock'
        subprocess.run([*common,*extra,'test/storage_volume/runtime_test.cpp','lib/hal/HalStorageVolume.cpp',
                        str(slow_object),*objects[1:],'-o',str(slow_binary)],cwd=ROOT,check=True)
        refused = subprocess.run([str(slow_binary),'clock-registers'],cwd=ROOT,timeout=120,env=ENV,
                                 text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        assert refused.returncode != 0 and 'selected-card clock edges must not reconfigure CLK' in refused.stderr, refused.stderr
        print('pre-fix GPIO clock path independently fails register-traffic regression')

    for layout in ('superfloppy','mbr'):
        for failure in ([], ['busy-timeout']):
            subprocess.run([str(binary),layout,*failure],cwd=ROOT,check=True,timeout=120,env=ENV)

    for scenario in ("directory-metadata", "directory-metadata-errors", "directory-direct-read", "directory-metadata-close", "directory-basic", "directory-races", "directory-io", "directory-unavailable", "directory-exhaustion",
                     "directory-close-file", "directory-close-dir", "sleep", "mutex-open-give", "mutex-read-give", "mutex-write-give",
                     "mutex-close-give", "mutex-dir-give", "mutex-stat-give",
                     "mutex-error-give", "mutex-info-give"):
        subprocess.run([str(binary),scenario],cwd=ROOT,check=True,timeout=120,env=ENV)
