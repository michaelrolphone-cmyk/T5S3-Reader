#!/usr/bin/env python3
"""Build hardware-owning camera ELF; requires U1 provider SDK at build time.

No downloads or firmware library links. ESP-IDF supplies register/CPU headers
only. Exact imports must already belong to the generic privileged CPU ABI.
"""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import re
from native_app_symbols import privileged_loader_public_libc_v1, validate_imports
from normalize_xtensa_relocations import normalize
ROOT=Path(__file__).resolve().parents[1]

def build(profile=False):
    source=ROOT/'Drivers'/('cam_ov3660_profile' if profile else 'camera_esp32s3')
    manifest=json.loads((source/'manifest.json').read_text())
    sdk_root=Path(os.environ.get('RISCRTE_PROVIDER_SDK_ROOT',ROOT))
    if not profile and not (sdk_root/'sdk/driver/RiscStreamProviderV1.h').is_file():
        raise RuntimeError('Camera requires U1 provider/stream SDK; set RISCRTE_PROVIDER_SDK_ROOT to pinned integrated checkout')
    core=Path(os.environ.get('PLATFORMIO_CORE_DIR',Path.home()/'.platformio'))
    cc=os.environ.get('NATIVE_DRIVER_CC',str(core/'packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-gcc'))
    sdk=Path(os.environ.get('ESP32S3_SDK',core/'packages/framework-arduinoespressif32/tools/sdk/esp32s3'))
    output=ROOT/'dist/experimental'/manifest['id'];output.mkdir(parents=True,exist_ok=True)
    inc=[source,source/'vendor',sdk_root/'sdk/driver',ROOT/'sdk/driver',ROOT/'lib/NativeApps/include',sdk/'qio_opi/include',sdk/'include/newlib/platform_include']
    inc += sorted(p for p in (sdk/'include').rglob('include') if p.is_dir())
    inc += [sdk/'include/soc/esp32s3',sdk/'include/xtensa/esp32s3/include',sdk/'include/freertos/port/xtensa/include',sdk/'include/freertos/include/esp_additions',sdk/'include/freertos/include/esp_additions/freertos',sdk/'include/esp_rom/include/esp32s3']
    sources=[source/'driver.c']
    if not profile:sources += [source/'hardware.c',source/'vendor/ov3660.c',source/'vendor/resolution.c']
    elf=output/'driver.elf'
    subprocess.run([cc,'-std=gnu11','-Os','-fPIC','-mtext-section-literals','-mlongcalls','-fvisibility=hidden',
        '-D_DEFAULT_SOURCE','-DCONFIG_IDF_TARGET_ESP32S3=1','-DLOG_LOCAL_LEVEL=0','-nostdlib','-nostartfiles','-shared',
        *['-I'+str(p) for p in inc],'-Wl,--hash-style=sysv','-Wl,--exclude-libs,ALL',
        *map(str,sources),'-lgcc','-o',str(elf)],check=True)
    if not profile: normalize(elf)
    readelf=cc.replace('gcc','readelf')
    symbols=subprocess.check_output([readelf,'--dyn-syms','--wide',str(elf)],text=True)
    cpu=(sdk_root/'lib/elf_loader/include/private/privileged_os_cpu_symbols_v1.def').read_text()
    allowed=set(re.findall(r'^RISC_OS_CPU_SYMBOL\((\w+)\)',cpu,re.M))|privileged_loader_public_libc_v1(sdk_root)
    imported=validate_imports(symbols,allowed)
    if any(n.startswith(('esp_camera','gpio_','i2c_','ledc_','cam_','SCCB_')) for n in imported):
        raise ValueError('Forbidden hardware firmware import')
    exports={f[7] for line in symbols.splitlines() if len(f:=line.split())>=8 and f[4]=='GLOBAL' and f[6]!='UND' and f[3]=='FUNC'}
    if exports!={'t5_driver_get'}:raise ValueError('Unexpected module exports: '+repr(exports))
    data=elf.read_bytes();manifest.update(size_bytes=len(data),sha256=hashlib.sha256(data).hexdigest())
    (output/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    (output/'import-audit.json').write_text(json.dumps({'imports':sorted(imported),'sha256':manifest['sha256']},indent=2)+'\n')
    print(manifest['id'],len(data),manifest['sha256'],sorted(imported))
    return elf
if __name__=='__main__':
    try: build()
    except subprocess.CalledProcessError as error: raise SystemExit(error.returncode)
