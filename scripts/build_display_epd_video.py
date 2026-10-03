#!/usr/bin/env python3
"""Build both existing T5 display engines into one independent ABI-2 provider."""
import hashlib,json,os,re,shlex,subprocess
from pathlib import Path
from native_app_symbols import privileged_os_cpu_exports,privileged_loader_public_libc_v1,validate_imports
from normalize_xtensa_relocations import normalize
from verify_provider_relocation_map import audit_loader_map
from probe_usb_controller_esp32s3 import compile_target,tool
from prepare_t5_i80_source import prepare as prepare_i80
from prepare_t5_quality_source import prepare as prepare_quality
ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/'Drivers/display_epd_video'
OUTPUT=ROOT/'dist/experimental/display-epd-video'
IDF_COMMIT='38eeba213aa695aabfd6d89aa9f5078dbe5a94c3'

def build(cc=None):
    manifest=json.loads((SOURCE/'manifest.json').read_text())
    if (manifest.get('id')!='display-epd-video' or manifest.get('driver_abi')!=2 or
        manifest.get('os_cpu_abi')!=3 or manifest.get('requires')!=[
          {'capability':'display.power','api':1},{'capability':'platform.clock','api':1}]):
        raise ValueError('Unexpected display provider manifest')
    OUTPUT.mkdir(parents=True,exist_ok=True)
    # Use the same pinned target configuration as the firmware, never a host ABI.
    pio=os.environ.get('PLATFORMIO_EXECUTABLE','pio')
    subprocess.run([pio,'run','-e','t5s3-pro','-t','compiledb'],cwd=ROOT,check=True)
    entries=json.loads((ROOT/'compile_commands.json').read_text())
    matches=[e for e in entries if e['file'].endswith('src/native/NativeUsbBridge.cpp')]
    if len(matches)!=1:raise ValueError('Ambiguous T5 target compilation command')
    entry=matches[0];args=entry.get('arguments') or shlex.split(entry['command'])
    cc=cc or os.environ.get('NATIVE_DRIVER_CC')
    if cc:args[0]=str(tool(Path(cc),'g++'))
    compiler=Path(args[0])
    cache=ROOT/'dist/idf-display-source/v4.4.7'
    if not cache.exists():
        subprocess.run(['git','clone','--depth=1','--branch','v4.4.7','--filter=blob:none','--sparse',
                        'https://github.com/espressif/esp-idf.git',str(cache)],check=True)
    actual=subprocess.check_output(['git','-C',str(cache),'rev-parse','HEAD'],text=True).strip()
    if actual!=IDF_COMMIT:raise ValueError('Display IDF source pin mismatch')
    subprocess.run(['git','-C',str(cache),'sparse-checkout','set','components/esp_lcd',
                    'components/driver','components/hal','components/soc','components/esp_hw_support','components/freertos'],check=True)
    idf=cache/'components'
    original=ROOT/'.pio/libdeps/t5s3-pro/M5GFX'
    if json.loads((original/'library.json').read_text()).get('version')!='0.2.20':
        raise ValueError('Display requires pinned M5GFX 0.2.20')
    staged=OUTPUT/'quality-source';prepare_quality(original/'src',staged,ROOT)
    # Replace the pre-existing include, not append a shadowed include directory.
    args=[a.replace('.pio/libdeps/t5s3-pro/M5GFX/src',str(staged))
          if a.startswith('-I') and '.pio/libdeps/t5s3-pro/M5GFX/src' in a else a for a in args]
    includes=['esp_lcd/src','esp_lcd/interface','esp_lcd/include','driver/include','hal/include','hal/esp32s3/include','soc/esp32s3/include']
    extra=tuple('-I'+str(idf/x) for x in includes)+('-I'+str(staged),'-I'+str(ROOT/'src/native'),'-ffunction-sections','-fdata-sections')
    objects=[];provenance={}
    def compile(source,c=False):
        output=OUTPUT/(source.stem+'.o');compile_target(args,entry,source,output,c_compiler=c,extra=extra)
        objects.append(output);provenance[str(source.relative_to(ROOT))]=hashlib.sha256(source.read_bytes()).hexdigest()
    for name in ['esp_lcd/src/esp_lcd_common.c','esp_lcd/src/esp_lcd_panel_io.c',
                 'esp_lcd/src/esp_lcd_panel_io_i80.c',
                 'hal/lcd_hal.c','soc/esp32s3/lcd_periph.c',
                 'soc/esp32s3/gpio_periph.c']:
        source=idf/name
        if source.name == 'esp_lcd_panel_io_i80.c':
            prepared=OUTPUT/source.name
            prepared.write_text(prepare_i80(source.read_text()));source=prepared
        compile(source,True)
    for name in ['lgfx/v1/platforms/esp32/Bus_EPD.cpp','lgfx/v1/platforms/esp32/Panel_EPD.cpp',
                 'lgfx/v1/panel/Panel_Device.cpp','lgfx/v1/panel/Panel_HasBuffer.cpp','lgfx/v1/misc/pixelcopy.cpp']:
        compile(staged/name)
    for name in ['driver.c','dma.c','lcd_clock.c','gpio.c','fast.cpp','quality.cpp','dispatch.cpp','runtime.cpp']:
        compile(SOURCE/name,name.endswith('.c'))
    values=dict(re.findall(r'PROVIDE\s*\(\s*(\w+)\s*=\s*(0x[0-9a-fA-F]+)\s*\)',(idf/'soc/esp32s3/ld/esp32s3.peripherals.ld').read_text()))
    symbols=('GPIO','LCD_CAM','GDMA','SYSTEM')
    if any(name not in values for name in symbols):raise ValueError('Missing pinned physical register address')
    mmio=[f'-Wl,--defsym={name}={values[name]}' for name in symbols]
    # SDK inline GPIO writes sometimes materialize an interior MMIO pointer.
    # Bind only these exact register addresses from the pinned SoC header.
    registers=(idf/'soc/esp32s3/include/soc/gpio_reg.h').read_text()
    offsets=dict(re.findall(r'#define (GPIO_OUT(?:1)?_W1T[SC]_REG)\s+\(DR_REG_GPIO_BASE \+ (0x[0-9A-Fa-f]+)\)',registers))
    if len(offsets)!=4:raise ValueError('Pinned GPIO register map changed')
    mmio += [f'-Wl,--defsym={name}={int(values["GPIO"],16)+int(offset,16)}' for name,offset in offsets.items()]
    elf=OUTPUT/'driver.elf'
    subprocess.run([str(compiler),'-shared','-nostdlib','-nostartfiles','-Wl,--hash-style=sysv',
        '-Wl,--exclude-libs,ALL','-Wl,--no-relax','-Wl,-T,'+str(SOURCE/'loader_sections.ld'),'-Wl,-Bsymbolic','-Wl,--version-script,'+str(SOURCE/'exports.map'),
        *mmio,*map(str,objects),'-lm','-lgcc','-o',str(elf)],check=True)
    subprocess.run([str(tool(compiler,"strip")),"--strip-debug",str(elf)],check=True)
    normalize(elf,display_gcc14_noops=True)
    mapping=audit_loader_map(elf)
    if any(mapping[key] for key in ('unmapped_relocations','unmapped_relative_values','unmapped_executable_sections')):
        raise ValueError('External display has unmapped loader sections/pointers: '+repr(mapping))
    (OUTPUT/'loader-map.json').write_text(json.dumps(mapping,indent=2)+'\n')
    readelf=tool(compiler,'readelf')
    listing=subprocess.check_output([str(readelf),'--dyn-syms','--wide',str(elf)],text=True)
    validate_imports(listing,privileged_os_cpu_exports(ROOT,3)|privileged_loader_public_libc_v1(ROOT))
    undefined={p[7] for line in listing.splitlines() if len(p:=line.split())>=8 and p[6]=='UND'}
    if not {'risc_cpu_dma_reserve_tx_v3','risc_cpu_dma_release_v3'}<=undefined:
        raise ValueError('Display must share the resident CPU DMA reservation authority')
    all_symbols=subprocess.check_output([str(tool(compiler,'nm')),str(elf)],text=True)
    if any(name in all_symbols for name in ('gdma_acquire_group_handle','gdma_release_group_handle',
       'gdma_new_tx_channel','gdma_new_rx_channel','gdma_register_tx_event_callbacks')):
        raise ValueError('Copied shared GDMA allocator/IRQ authority in display provider')
    exports={p[7] for line in listing.splitlines() if len(p:=line.split())>=8 and p[4]=='GLOBAL' and p[6]!='UND'}
    if exports!={'t5_driver_get'}:raise ValueError('Unexpected display exports: '+repr(exports))
    sections=subprocess.check_output([str(readelf),'-S','--wide',str(elf)],text=True)
    if re.search(r'\.(?:init|fini)_array\s',sections):raise ValueError('Provider must explicitly initialize all state')
    manifest.update(size_bytes=elf.stat().st_size,sha256=hashlib.sha256(elf.read_bytes()).hexdigest())
    (OUTPUT/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    (OUTPUT/'source-provenance.json').write_text(json.dumps({'idf_commit':actual,'m5gfx':'0.2.20','sources':provenance},indent=2)+'\n')
    print('Independent T5 Reader/fast display ELF linked; scoped ABI3 imports and sole entry point verified')
    return elf
if __name__=='__main__':build()
