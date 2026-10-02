#!/usr/bin/env python3
"""Actual declarative builder/ZIP/record/index/runtime pipeline, with no ELF."""
from pathlib import Path
import json
import os
import subprocess
import sys
import tempfile
from unittest.mock import patch
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'scripts'))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import build_installed_usb_stack as builder
import build_release_candidates as selective
import export_canonical_driver_release as exporter
from build_release_record import build_record
from publish_updated_packages import discover_candidates, release_assets
from update_release_index import update_index, serialize_index
from verify_release_plan import verify_plan
from package_resource_source import resource_source
from package_catalog_roundtrip_test import CPP

with tempfile.TemporaryDirectory(prefix='rte-resource-only-') as temporary:
    root = Path(temporary)
    source = root/'Services/reference_pack'; source.mkdir(parents=True)
    (root/'Drivers').mkdir();(root/'Apps').mkdir()
    (root/'platformio.ini').write_text('[riscrte]\nversion=1.0.0\n')
    (source/'help').mkdir();(source/'help/guide.txt').write_bytes(b'package-owned reference text')
    metadata = {'type':'service','id':'reference-pack','version':'1.0.0','payload':'resources',
                'architecture':'xtensa-esp32s3','min_runtime_api':2,'resources':['help/guide.txt']}
    path=source/'manifest.json';path.write_text(json.dumps(metadata))
    plan=[{'product':'services','id':'reference-pack','version':'1.0.0'}]
    index={'schema':1,'firmware':{'version':'1.0.0'},'apps':[],'drivers':[]}
    assert discover_candidates(root,index)==plan
    assert selective.discover_module_builders(root,'service')=={'reference-pack':[]}
    def no_elf(*args,**kwargs):raise AssertionError('data-only source invoked ELF machinery')
    with patch.object(builder,'DRIVER_SOURCES',root/'Drivers'),patch.object(builder,'DESTINATION',root/'dist/packages'), \
         patch.object(builder,'linked_or_build',no_elf),patch.object(builder,'audit_loader_map',no_elf), \
         patch.object(builder,'extract_imports',no_elf),patch.object(builder,'provider_inputs',no_elf):
        rows=builder.build({'reference-pack'})
    assert rows[0]['artifact'] is None and rows[0]['payload']=='resources'
    output=root/'dist/release-service-packages'
    with patch.object(exporter,'SOURCE',root/'dist/packages'):
        exporter.export({'reference-pack'},output,'service')
    record=build_record('services','reference-pack','1.0.0',root)
    assert record['manifest']['payload']=='resources'
    assert not any(item['executable'] for item in record['manifest']['entries'])
    assert len(release_assets(root,'services','reference-pack','1.0.0'))==1
    assert len(verify_plan(root,plan))==1
    index=update_index(index,'services',record)
    cpp=root/'consumer.cpp';binary=root/'consumer';data=root/'index.json'
    cpp.write_text(CPP);data.write_text(serialize_index(index))
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-Wall','-Wextra','-Werror','-I'+str(ROOT/'src'),str(cpp),'-o',str(binary)],check=True)
    result=subprocess.run([str(binary),str(data)],check=True,capture_output=True,text=True)
    assert result.stdout.strip()==record['url']
    # Source bytes, mode and path ownership remain strict at publication intake.
    (source/'help/guide.txt').write_bytes(b'changed without rebuilding')
    try:build_record('services','reference-pack','1.0.0',root)
    except ValueError:pass
    else:raise AssertionError('stale source resource accepted')
    for name in ('../outside.txt','concealed.elf'):
        changed=dict(metadata,resources=[name]);path.write_text(json.dumps(changed))
        try:resource_source(path)
        except ValueError:pass
        else:raise AssertionError('unsafe resource source accepted')
    changed=dict(metadata,driver_abi=2);path.write_text(json.dumps(changed))
    try:resource_source(path)
    except ValueError:pass
    else:raise AssertionError('resource-only source advertised executable ABI')
print('Data-only service: real source discovery/build/export/record/index/runtime URL, no ELF/fake capability, source integrity and path refusal PASS')
