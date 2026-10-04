#!/usr/bin/env python3
"""Exercise app startup through the production SD/FatFs/HAL and validators.

Pass the ArduinoJson include directory. Optional --baseline REF measures the
same fixture with the three traversal functions from REF, without fault tests. --require-probe-budget also enforces the new budget
on that baseline, so it must fail before the backup-probe repair.
Use --no-sanitize for an ordinary build; the default uses ASan/UBSan.
Sector counts and synthetic time are operation evidence, not hardware timing.
"""
from pathlib import Path
import argparse
import os
import platform
import subprocess
import tempfile
from mmio_boundary import header

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('arduino_json')
parser.add_argument('--baseline')
parser.add_argument('--require-probe-budget', action='store_true')
parser.add_argument('--no-sanitize', action='store_true')
args = parser.parse_args()
san = 'undefined' if platform.system() == 'Darwin' else 'address,undefined'
sanitize_flags = [] if args.no_sanitize else ['-fsanitize=' + san]
env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0', UBSAN_OPTIONS='halt_on_error=1')

def source(path):
    if args.baseline:
        return subprocess.check_output(['git', 'show', args.baseline + ':' + path], cwd=ROOT, text=True)
    return (ROOT / path).read_text()

with tempfile.TemporaryDirectory(prefix='app-inventory-') as temp:
    build = Path(temp)
    wire = header(ROOT).replace('extern unsigned card_reads, card_writes;',
        'extern unsigned card_reads, card_writes; extern uint64_t card_time; extern unsigned inventory_sector_ms;')
    wire = wire.replace('++card_reads;', '++card_reads; card_time += inventory_sector_ms;')
    (build / 'x4pro_mmio.h').write_text(wire)
    (build / 'esp_task_wdt.h').write_text('#pragma once\ninline void esp_task_wdt_reset() {}\n')
    (build / 'NativeAppLauncher.h').write_text('#pragma once\n#include <esp_err.h>\nextern "C" int native_app_register_sd_vfs();\nconst char* native_app_current_path();\n')
    # Inject concurrent HAL work only after its non-recursive mutex unlocks.
    # The real generation tracker, storage adapter and provider remain intact.
    (build / 'freertos').mkdir()
    mutex = (ROOT / 'test/storage_volume/stubs/freertos/semphr.h').read_text()
    mutex = mutex.replace('inline bool held = false;',
        'inline bool held = false; inline void (*afterUnlock)() = nullptr;')
    mutex = mutex.replace('FakeLock::held = false;\n  return 1;',
        'FakeLock::held = false;\n  if (FakeLock::afterUnlock) { auto callback = FakeLock::afterUnlock; FakeLock::afterUnlock = nullptr; callback(); }\n  return 1;')
    (build / 'freertos/semphr.h').write_text(mutex)
    # Pair the declaration with older traversal implementations too.
    (build / 'native').mkdir()
    (build / 'native/AppPackageInstaller.h').write_text(source('src/native/AppPackageInstaller.h'))
    (build / 'AppPackageInstaller.h').write_text('#include "native/AppPackageInstaller.h"\n')
    host = source('src/native/NativeAppHost.cpp')
    managed = host[host.index('constexpr RuntimePackages::PackageRuntimePolicy kCanonicalAppPolicy'):host.index('\n} // namespace', host.index('bool verifiedManagedApp('))]
    inventory = host[host.index('bool installedRefresh()'):host.index('\nuint32_t installedCount()')]
    (build / 'app_inventory_host.inc').write_text(managed + '\n' + inventory)
    for name in ('InstalledAppPath', 'AppPackageRecoveryInventory', 'AppManifest'):
        (build / (name + '.cpp')).write_text(source('src/native/' + name + '.cpp'))
    # Reuse the existing card setup, ELF fixture, and provider declarations.
    fixture = (ROOT / 'test/storage_volume/inventory_test.cpp').read_text()
    (build / 'app_inventory_card.inc').write_text(fixture[:fixture.index('int main()')])
    objects = []
    for path in ('Drivers/x4pro_sd/driver.c', 'Drivers/storage_fatfs/fatfs/ff.c',
                 'Drivers/storage_fatfs/fatfs/ffunicode.c', 'test/storage_volume/os_cpu_fake.c'):
        out = build / (Path(path).name + '.o')
        subprocess.run(['cc', '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
            '-Wno-overflow', *sanitize_flags, '-pthread', '-D_XOPEN_SOURCE=700',
            '-Isdk/driver', '-I' + temp, '-IDrivers/x4pro_board', '-c', path, '-o', str(out)], cwd=ROOT, check=True)
        objects.append(str(out))
    common = ['c++', '-std=c++17', '-O1', '-g', '-Wall', '-Wextra', '-Werror', *sanitize_flags,
        '-pthread', '-DBOARD_XTEINK_X4_PRO', '-DARDUINO_ARCH_ESP32', '-DCROSSPOINT_VERSION="1.3.102"',
        '-Wno-overloaded-virtual', '-Wno-unused-function', '-I' + temp, '-Itest/storage_volume',
        '-Itest/storage_volume/inventory_stubs', '-Itest/storage_volume/stubs', '-include', 'Arduino.h',
        '-Ilib/hal', '-IDrivers/storage_fatfs/fatfs', '-Isdk/driver', '-Isrc', '-Isrc/native', '-Ilib/NativeApps/include',
        '-isystem', args.arduino_json, '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections']
    compiler = subprocess.check_output(['c++', '--version'], text=True)
    parser_flags = [] if 'clang' in compiler.lower() else ['-Wno-error=maybe-uninitialized']
    app_parser = build / 'AppManifest.o'
    subprocess.run([*common, *parser_flags, '-c', str(build / 'AppManifest.cpp'), '-o', str(app_parser)], cwd=ROOT, check=True)
    binary = build / 'apps'
    subprocess.run([*common, *(['-DAPP_INVENTORY_BASELINE'] if args.baseline else []),
        *(['-DAPP_INVENTORY_REQUIRE_PROBE_BUDGET'] if args.require_probe_budget else []),
        'test/storage_volume/app_inventory_test.cpp', str(build / 'InstalledAppPath.cpp'),
        str(build / 'AppPackageRecoveryInventory.cpp'), 'src/native/AppPackageInstaller.cpp',
        'src/runtime/packages/InstalledCapabilityResolver.cpp', 'src/runtime/packages/PackageOrdinarySdAdapter.cpp',
        'src/runtime/packages/PackageCdcSdMigration.cpp', 'lib/hal/HalStorageVolume.cpp',
        str(app_parser), *objects, '-lcrypto', '-o', str(binary)], cwd=ROOT, check=True)
    scenarios = ((),) if args.baseline else ((), ('close-resolve',), ('close-recovery',), ('close-inventory',),
        ('uncertain-scan',), ('uncertain-end',), ('uncertain-probe',))
    for scenario in scenarios:
        subprocess.run([str(binary), *scenario], cwd=ROOT, env=env, check=True, timeout=120)
