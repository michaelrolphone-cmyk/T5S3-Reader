#!/usr/bin/env python3
"""Build only explicit USB health profiles, without changing normal outputs."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
from generate_provider_package_inputs_v1 import prepare
from native_app_symbols import firmware_exports, validate_imports
from verify_provider_relocation_map import audit_loader_map
from audit_provider_health_elf import audit
from pack_rte_zip import pack_directory, catalog_row
ROOT = Path(__file__).resolve().parents[1]

def build(names, cc, output):
    results = []
    manifests = {json.loads(p.read_text())['id']: p for p in (ROOT/'Drivers').glob('*/manifest.health.json')}
    for identity in names:
        if identity not in manifests: raise ValueError('Unknown health profile: ' + identity)
        source = manifests[identity]
        manifest = json.loads(source.read_text())
        target = output/'packages'/identity
        target.mkdir(parents=True, exist_ok=True)
        elf = target/'driver.elf'
        if identity == 'usb-controller-esp32s3':
            linked = output/identity/'driver.elf'
            if not linked.is_file(): raise ValueError('Build the controller first with --native-phy-lease --provider-health')
            shutil.copyfile(linked, elf)
        else:
            subprocess.run([cc, '-std=c11', '-Os', '-fPIC', '-mtext-section-literals', '-mlongcalls',
                '-fvisibility=hidden', '-nostdlib', '-nostartfiles', '-shared',
                '-I'+str(ROOT/'sdk/driver'), '-Wl,--hash-style=sysv', '-Wl,--exclude-libs,ALL',
                '-Wl,--version-script,'+str(ROOT/'Drivers/usb_controller_esp32s3/exports.health.map'),
                str(source.parent/'driver.health.c'), '-lgcc', '-o', str(elf)], check=True)
        readelf = str(Path(cc).with_name(Path(cc).name.replace('gcc','readelf')))
        symbols = subprocess.check_output([readelf,'--dyn-syms','--wide',str(elf)],text=True)
        if identity != 'usb-controller-esp32s3':
            validate_imports(symbols,{s for s in firmware_exports(ROOT) if not s.startswith('t5_')})
        validation = audit(elf)
        if identity == 'usb-controller-esp32s3':
            from audit_usb_controller_elf import audit as audit_controller
            controller_audit = audit_controller(elf, provider_health=True)
            if not controller_audit['privileged_os_cpu_v1_import_compatible']:
                raise ValueError('Controller scoped import/relocation audit failed')
            validation['controller'] = controller_audit
        mapping = audit_loader_map(elf)
        if any(mapping[k] for k in ('unmapped_relocations','unmapped_relative_values','unmapped_executable_sections')):
            raise ValueError('Unmapped provider ELF: '+identity)
        prepare(elf, source, target)
        (target/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
        entries = [{'name':name,'size_bytes':(target/name).stat().st_size,
                    'sha256':hashlib.sha256((target/name).read_bytes()).hexdigest(),'executable':name=='driver.elf'}
                   for name in ('driver.elf','provider-abi.v1','privileged-imports.v1','manifest.json')]
        package={'schema':1,'kind':'driver','id':identity,'version':manifest['version'],'artifact':'driver.elf',
                 'architecture':manifest['architecture'],'min_runtime_api':2,'entries':entries,
                 'requires':[{'capability':d['capability'],'min_api':d['api']} for d in manifest['requires']]}
        (target/'.package.json').write_text(json.dumps(package,separators=(',',':'))+'\n')
        # Audits stay outside the installed directory's exact package contents.
        (output/(identity+'-audit.json')).write_text(json.dumps({'health':validation,'mapping':mapping},indent=2)+'\n')
        asset=f"driver-{identity}-{manifest['version']}-{manifest['architecture']}.rte.zip"
        archive=pack_directory(target);(output/asset).write_bytes(archive)
        results.append(catalog_row(target,asset,archive))
        print(identity+'@'+manifest['version']+' health profile PASS')
    (output/'package-catalog.json').write_text(json.dumps({'schema':1,'release':'unpublished-provider-health','packages':results},indent=2)+'\n')
    return results
if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ids',nargs='+',required=True)
    parser.add_argument('--cc',default=os.environ.get('NATIVE_DRIVER_CC') or shutil.which('xtensa-esp32s3-elf-gcc'))
    parser.add_argument('--output-dir',type=Path,default=ROOT/'dist/provider-health')
    args=parser.parse_args()
    if not args.cc: parser.error('--cc or NATIVE_DRIVER_CC is required')
    build(args.ids,args.cc,args.output_dir)
