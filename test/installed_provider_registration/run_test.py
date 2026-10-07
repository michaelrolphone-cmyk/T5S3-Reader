#!/usr/bin/env python3
"""Production T5 provider-admission regression; no network, hardware, or ELF execution.

Pass an installed ArduinoJson include directory (containing ArduinoJson.h).
--source-root compiles another complete checkout without changing that checkout.
--expect-original asserts the original exhaustion/capacity failures instead of
candidate behavior; it is not a candidate-pass mode. --sanitize enables ASan and
UBSan (UBSan only on Darwin, like the existing storage-volume harness).
"""
import argparse
import base64
import gzip
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import platform
import subprocess
import sys
import tarfile
import tempfile

# Import the shared transport helper without writing caches into --source-root.
sys.dont_write_bytecode = True

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def unpack_fixture(destination):
    metadata = json.loads((HERE / 'fixtures/manifest.json').read_text())
    archive = base64.b64decode((HERE / 'fixtures/t5-expanded.tar.gz.b64').read_bytes())
    assert len(archive) == metadata['archive']['bytes']
    assert hashlib.sha256(archive).hexdigest() == metadata['archive']['sha256']
    # The manifest is an exact allowlist. Never extract links, arbitrary paths,
    # unbounded payloads, or undeclared members from even this checked-in input.
    seen = set()
    with tarfile.open(fileobj=io.BytesIO(gzip.decompress(archive)), mode='r:') as tar:
        for member in tar:
            assert member.isfile() and member.name in metadata['files']
            assert member.name not in seen and not member.name.startswith('/')
            assert '..' not in Path(member.name).parts
            expected = metadata['files'][member.name]
            assert member.size == expected['size'] <= 1024 * 1024
            data = tar.extractfile(member).read()
            assert hashlib.sha256(data).hexdigest() == expected['sha256']
            target = destination / member.name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
            seen.add(member.name)
    assert seen == set(metadata['files'])
    for package in metadata['packages']:
        directory = destination / 'Drivers' / package['id']
        declared = json.loads((directory / '.package.json').read_text())
        assert (declared['id'], declared['version']) == (package['id'], package['version'])
        for entry in declared['entries']:
            data = (directory / entry['name']).read_bytes()
            assert len(data) == entry['size_bytes']
            assert hashlib.sha256(data).hexdigest() == entry['sha256']
    print(f"Verified {len(seen)} fixture files and {len(metadata['packages'])} actual packages", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('arduino_json', type=Path)
    parser.add_argument('--source-root', type=Path, default=ROOT)
    parser.add_argument('--expect-original', action='store_true')
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--case', action='append', choices=['navigation', 'touch-first', 'capacity', 'faults'])
    args = parser.parse_args()
    root = args.source_root.resolve()
    arduino = args.arduino_json.resolve()
    if not (arduino / 'ArduinoJson.h').is_file():
        parser.error('arduino_json must contain ArduinoJson.h; this runner never downloads dependencies')
    sanitizer = 'undefined' if platform.system() == 'Darwin' else 'address,undefined'
    flags = ['-fsanitize=' + sanitizer, '-fno-omit-frame-pointer'] if args.sanitize else []
    cc, cxx = os.environ.get('CC', 'cc'), os.environ.get('CXX', 'c++')
    compiler = subprocess.check_output([cxx, '--version'], text=True)
    gcc = [] if 'clang' in compiler.lower() else ['-Wno-error=maybe-uninitialized']
    spec = importlib.util.spec_from_file_location('mmio_boundary', root / 'test/storage_volume/mmio_boundary.py')
    mmio = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mmio)
    with tempfile.TemporaryDirectory(prefix='provider-admission-') as temporary:
        build = Path(temporary)
        fixture = build / 'fixture'
        unpack_fixture(fixture)
        (build / 'x4pro_mmio.h').write_text(mmio.header(root))
        arduino_stub = (root / 'test/storage_volume/stubs/Arduino.h').read_text()
        delay = 'inline void delay(unsigned long n) { card_time += n; }'
        assert arduino_stub.count(delay) == 1
        (build / 'Arduino.h').write_text(arduino_stub.replace(delay,
            'extern unsigned long delayCalls; inline void delay(unsigned long n) { ++delayCalls; card_time += n; }'))
        (build / 'esp_task_wdt.h').write_text('#pragma once\ninline void esp_task_wdt_reset() {}\n')
        (build / 'NativeAppLauncher.h').write_text('#pragma once\n#include <esp_err.h>\nextern "C" int native_app_register_sd_vfs();\nconst char* native_app_current_path();\n')
        (build / 'freertos').mkdir()
        mutex = (root / 'test/storage_volume/stubs/freertos/semphr.h').read_text()
        mutex = mutex.replace('inline bool held = false;', 'inline bool held = false; inline void (*afterUnlock)() = nullptr;')
        mutex = mutex.replace('FakeLock::held = false;\n  return 1;',
            'FakeLock::held = false;\n  if (FakeLock::afterUnlock) { auto callback = FakeLock::afterUnlock; FakeLock::afterUnlock = nullptr; callback(); }\n  return 1;')
        (build / 'freertos/semphr.h').write_text(mutex)
        # Compile the complete production translation unit in the harness to
        # reach registration before activation. Only the target stack warning
        # is omitted: host pointers and sanitizer frames are not Xtensa frames.
        source = (root / 'src/runtime/drivers/InstalledProviderGraph.cpp').read_text()
        source = source.replace('#pragma GCC diagnostic error "-Wframe-larger-than=384"', '')
        (build / 'InstalledProviderGraph.inc').write_text(source)
        objects = []
        for path in ['Drivers/x4pro_sd/driver.c', 'Drivers/storage_fatfs/fatfs/ff.c',
                     'Drivers/storage_fatfs/fatfs/ffunicode.c', 'test/storage_volume/os_cpu_fake.c']:
            obj = build / (Path(path).name + '.o')
            subprocess.run([cc, '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                '-Wno-overflow', *flags, '-pthread', '-D_XOPEN_SOURCE=700',
                '-Isdk/driver', '-I' + str(build), '-IDrivers/x4pro_board', '-c', path, '-o', str(obj)],
                cwd=root, check=True, timeout=120)
            objects.append(str(obj))
        includes = [str(build), str(HERE / 'stubs'), 'test/storage_volume',
                    'test/storage_volume/inventory_stubs', 'test/storage_volume/stubs',
                    'lib/hal', 'Drivers/storage_fatfs/fatfs', 'sdk/driver', 'src',
                    'src/native', 'src/runtime/drivers', 'lib/NativeApps/include']
        command = [cxx, '-std=c++17', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                   '-Wno-overloaded-virtual', '-Wno-unused-function', '-Wno-unused-variable', *gcc,
                   *(['-DPROVIDER_HAS_MATCH_LIST'] if 'struct ProviderMatches {' in source else []),
                   *flags, '-pthread', '-DBOARD_XTEINK_X4_PRO', '-DARDUINO_ARCH_ESP32',
                   '-DCROSSPOINT_VERSION="1.3.102"', '-include', 'Arduino.h',
                   *['-I' + p for p in includes], '-idirafter', 'test/drivers/stubs',
                   '-isystem', str(arduino), '-ffunction-sections', '-fdata-sections',
                   '-Wl,-dead_strip' if platform.system() == 'Darwin' else '-Wl,--gc-sections']
        sources = ['src/native/InstalledAppPath.cpp', 'src/native/AppPackageRecoveryInventory.cpp',
                   'src/native/AppPackageInstaller.cpp', 'src/native/AppManifest.cpp',
                   'src/runtime/packages/PackageExecutableAdmission.cpp', 'src/runtime/drivers/ProviderGraphV2.cpp',
                   'src/runtime/drivers/ProviderModuleV2.cpp', 'src/runtime/drivers/DeviceProviderExecutorV2.cpp',
                   'src/runtime/drivers/BootstrapModuleStore.cpp', 'src/runtime/packages/InstalledCapabilityResolver.cpp',
                   'src/runtime/packages/PackageOrdinarySdAdapter.cpp', 'src/runtime/packages/PackageCdcSdMigration.cpp',
                   'lib/hal/HalStorageVolume.cpp']
        binary = build / 'registration'
        subprocess.run([*command, str(HERE / 'regression.cpp'), *sources, *objects,
                        '-lcrypto', '-ldl', '-o', str(binary)], cwd=root, check=True, timeout=240)
        environment = dict(os.environ)
        environment.setdefault('UBSAN_OPTIONS', 'halt_on_error=1')
        for case in args.case or ['navigation', 'touch-first', 'capacity', 'faults']:
            subprocess.run([str(binary), str(fixture), case,
                            'original' if args.expect_original else 'candidate'],
                           cwd=root, env=environment, check=True, timeout=180)


if __name__ == '__main__':
    main()
