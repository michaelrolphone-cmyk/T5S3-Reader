#!/usr/bin/env python3
"""Build the independently installable T5 SPI bus transition provider."""
import hashlib
import json
import shlex
import subprocess
from pathlib import Path
from probe_usb_controller_esp32s3 import ROOT, compile_target, tool
from normalize_xtensa_relocations import normalize
from validate_xtensa_relative_targets import validate


def build():
    source=ROOT/'Drivers/spi_esp32s3'
    manifest=json.loads((source/'manifest.json').read_text())
    output=ROOT/'dist/experimental'/manifest['id']
    output.mkdir(parents=True,exist_ok=True)
    subprocess.run(['pio','run','-e','t5s3-pro','-t','compiledb'],cwd=ROOT,check=True,timeout=120)
    entries=json.loads((ROOT/'compile_commands.json').read_text())
    entries=[e for e in entries if Path(e['file']).as_posix().endswith('src/native/NativeUsbBridge.cpp')]
    if len(entries)!=1: raise ValueError('Missing unique target compilation configuration')
    entry=entries[0]
    args=entry.get('arguments') or shlex.split(entry['command'])
    cc=Path(args[0]); obj=output/'driver.o'; elf=output/'driver.elf'
    compile_target(args,entry,source/'driver.c',obj,c_compiler=True,
                   extra=('-Wall','-Wextra','-Werror','-O2','-fno-builtin','-mtext-section-literals','-mlongcalls'))
    subprocess.run([str(cc),'-shared','-nostdlib','-nostartfiles','-Wl,--hash-style=sysv',
                    '-Wl,--exclude-libs,ALL','-Wl,--no-relax',str(obj),'-lgcc','-o',str(elf)],check=True)
    normalize(elf);validate(elf)
    symbols=subprocess.check_output([str(tool(cc,'readelf')),'--dyn-syms','--wide',str(elf)],text=True)
    exported=set();imports=set()
    for line in symbols.splitlines():
        parts=line.split()
        if len(parts)<8: continue
        if parts[6]=='UND': imports.add(parts[7])
        elif parts[4]=='GLOBAL' and parts[3]=='FUNC': exported.add(parts[7])
    expected={'risc_fw_spi_begin_v1','risc_fw_spi_select_v1','risc_fw_spi_transfer_v1','risc_fw_spi_end_v1',
              'xQueueCreateMutex','xQueueSemaphoreTake','xQueueGenericSend','vQueueDelete',
              'xTaskGetTickCount','xTaskGetCurrentTaskHandle'}
    if imports!=expected or exported!={'t5_driver_get'}:
        raise ValueError(f'SPI provider symbols differ: imports={imports}, exports={exported}')
    payload=elf.read_bytes()
    manifest.update(size_bytes=len(payload),sha256=hashlib.sha256(payload).hexdigest())
    (output/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print(f"{manifest['id']} {manifest['version']} {len(payload)} bytes: exact private imports and relocations PASS")


if __name__=='__main__': build()
