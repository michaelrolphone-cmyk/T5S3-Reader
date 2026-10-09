#!/usr/bin/env python3
"""Build ordinary PIC device-MSC ELF from shared Reader + pinned TinyUSB.

No firmware USB API, OS task, ISR, DMA, allocation, or privileged imports.
The native port supplies only its typed PHY ownership lease.
"""
import argparse,hashlib,json,os,subprocess,sys,shutil
from pathlib import Path
from prepare_usb_device_stack import prepare,SOURCES,PIN
ROOT=Path(__file__).resolve().parents[1]
def run():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--tinyusb',type=Path,required=True)
 p.add_argument('--framework',type=Path,required=True)
 p.add_argument('--cc',default=os.environ.get('NATIVE_DRIVER_CC'),required=not os.environ.get('NATIVE_DRIVER_CC'))
 p.add_argument('--output',type=Path,default=ROOT/'dist/experimental/usb-device-msc-esp32s3')
 a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
 stack=prepare(a.tinyusb,out/'tinyusb');src=ROOT/'Drivers/usb_device_msc_esp32s3'
 sdk=a.framework/'tools/sdk/esp32s3'
 include=[ROOT/'sdk/driver',src,stack,sdk/'dio_opi/include',sdk/'include/config',sdk/'include/newlib/platform_include',sdk/'include/freertos/include',sdk/'include/freertos/include/esp_additions/freertos',sdk/'include/freertos/port/xtensa/include',sdk/'include/xtensa/include',sdk/'include/xtensa/esp32s3/include',sdk/'include/soc/esp32s3/include',sdk/'include/hal/esp32s3/include']
 include += [d for d in (sdk/'include').glob('*/include') if d not in include]
 flags=['-std=gnu11','-O2','-fno-ivopts','-fPIC','-mtext-section-literals','-mlongcalls','-fvisibility=hidden','-fno-builtin','-ffunction-sections','-fdata-sections','-D__ESP32S3__','-DCONFIG_IDF_TARGET_ESP32S3=1','-DNDEBUG']+['-I'+str(d) for d in include]
 import re
 regs=dict(re.findall(r'PROVIDE\s*\(\s*(\w+)\s*=\s*(0x[0-9a-fA-F]+)\s*\)',(sdk/'ld/esp32s3.peripherals.ld').read_text()))
 mmio=out/'mmio.h'
 # The ordinary loader must never relocate a physical register as an ELF RAM
 # pointer. Compile numeric MMIO expressions from the pinned SoC map instead.
 kinds={'GPIO':('gpio','gpio_dev_t'),'RTCCNTL':('rtc_cntl','rtc_cntl_dev_t'),'SYSTEM':('system','system_dev_t'),'USB_WRAP':('usb_wrap','usb_wrap_dev_t')}
 mmio.write_text(''.join('#include <soc/'+v[0]+'_struct.h>\n' for v in kinds.values())+''.join('#define '+n+' (*('+v[1]+' *)(uintptr_t)'+regs[n]+')\n' for n,v in kinds.items()))
 flags+=['-include',str(mmio)]
 sources=[src/'driver.c',src/'transport.c',ROOT/'Drivers/usb_controller_esp32s3/phy_gpio.c',src/'phy_pads.c',src/'StackDefaults.c']+[stack/f for f in SOURCES]
 objects=[]
 for i,f in enumerate(sources):
  obj=out/f'{i}.o';objects.append(obj)
  subprocess.run([a.cc,*flags,*(['-Wall','-Wextra','-Werror',*(['-Wno-unused-parameter'] if i==2 else [])] if i<5 else []),'-c',str(f),'-o',str(obj)],check=True)
 elf=out/'driver.elf'
 subprocess.run([a.cc,'-shared','-nostdlib','-nostartfiles','-Wl,--hash-style=sysv','-Wl,--exclude-libs,ALL','-Wl,-Bsymbolic','-Wl,--no-relax','-Wl,--gc-sections','-Wl,--version-script,'+str(ROOT/'Drivers/usb_controller_esp32s3/exports.map'),*map(str,objects),'-lgcc','-o',str(elf)],check=True)
 subprocess.run([sys.executable,str(ROOT/'scripts/normalize_xtensa_relocations.py'),str(elf)],check=True)
 subprocess.run([sys.executable,str(ROOT/'scripts/validate_xtensa_relative_targets.py'),str(elf)],check=True)
 readelf=a.cc.removesuffix('gcc')+'readelf';objdump=a.cc.removesuffix('gcc')+'objdump'
 symbols=subprocess.check_output([readelf,'--dyn-syms','--wide',str(elf)],text=True);(out/'symbols.txt').write_text(symbols)
 rows=[line.split() for line in symbols.splitlines()]
 imports={r[7] for r in rows if len(r)>=8 and r[4] in ('GLOBAL','WEAK') and r[6]=='UND'}
 exports={r[7] for r in rows if len(r)>=8 and r[3]=='FUNC' and r[4]=='GLOBAL' and r[6]!='UND'}
 allowed={'memcpy','memmove','memset','memcmp','strlen','strcmp'}
 if imports-allowed:raise ValueError('Forbidden USB/OS imports: '+repr(imports-allowed))
 if exports!={'t5_driver_get'}:raise ValueError('Unexpected exports: '+repr(exports))
 disasm=subprocess.check_output([objdump,'-d',str(elf)],text=True);(out/'disassembly.txt').write_text(disasm)
 if 's32c1i' in disasm.lower():raise ValueError('PSRAM unsafe atomic instruction')
 record={'reader_commit':subprocess.check_output(['git','-C',str(ROOT),'rev-parse','HEAD'],text=True).strip(),'source_dirty':bool(subprocess.check_output(['git','-C',str(ROOT),'status','--porcelain'],text=True).strip()),'tinyusb_commit':PIN,'compiler':subprocess.check_output([a.cc,'--version'],text=True).splitlines()[0],'elf_bytes':elf.stat().st_size,'elf_sha256':hashlib.sha256(elf.read_bytes()).hexdigest(),'imports':sorted(imports),'target_structure_passed':True,'device_tested':False}
 record['source_sha256']={str(f.relative_to(ROOT)):hashlib.sha256(f.read_bytes()).hexdigest() for f in [*sorted(src.glob('*')),*[ROOT/'sdk/driver'/n for n in ('RiscUsbDeviceMscV1.h','RiscUsbPhyResourceV1.h','RiscStorageExportV1.h')],Path(__file__),ROOT/'scripts/prepare_usb_device_stack.py'] if f.is_file()}
 record['framework_version']=json.loads((a.framework/'package.json').read_text())['version']
 shutil.copy2(src/'manifest.json',out/'manifest.json')
 (out/'target-proof.json').write_text(json.dumps(record,indent=2)+'\n')
 print(json.dumps(record,indent=2))
if __name__=='__main__':
 try:run()
 except subprocess.CalledProcessError as error:sys.exit(error.returncode)
