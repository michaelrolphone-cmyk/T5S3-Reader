"""Instrument a separate clean corrected SD-bootstrap checkout; never touch a production checkout."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import x4_serial_staging as s
import x4_deployment_plan as plan


def prepare(root, packages, artifact):
    # Local bytes only: never fetch, open a device, or rely on raw uploaded trees.
    s.require(artifact.is_file() and not artifact.is_symlink() and artifact.stat().st_size <= plan.MAX_ARCHIVE,
              'Deployment artifact path/size invalid')
    raw=artifact.read_bytes()
    validated=plan.validate(raw,s.SOURCE,s.DEPLOYMENT_ARCHIVE)
    s.require(validated['sd_archive']['sha256']==s.SD_ARCHIVE and validated['sd_file_count']==s.SD_FILES,
              'Corrected SD inventory differs')
    head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip()
    s.require(head==s.SOURCE,'Fixture base differs')
    s.require(not subprocess.check_output(['git','status','--porcelain','--untracked-files=no'],cwd=root,text=True).strip(),'Fixture has tracked changes')
    source=Path(__file__).parent/'reader'
    s.require(root.resolve()!=Path(__file__).resolve().parents[2],'Refusing to instrument the CI development checkout')
    archives={name:(packages/f'application-{name}-1.0.8-xtensa-esp32s3.rte.zip').read_bytes() for name in s.IDS}
    s.validate_archives(archives)
    profile=root/'platformio.local.ini'
    s.require(not profile.exists() or profile.read_text().strip()=='; local overrides (empty in CI)','Existing nonempty local profile')
    target=root/'test/hardware/reader';s.require(not target.exists(),'Instrumentation already exists')
    shutil.copytree(source,target)
    utility=s.sha(b''.join(p.read_bytes() for p in sorted(source.iterdir()) if p.is_file())+
        Path(__file__).read_bytes()+Path(s.__file__).read_bytes()+Path(plan.__file__).read_bytes()+
        s.SOURCE.encode()+s.SD_ARCHIVE.encode()+s.DEPLOYMENT_ARCHIVE.encode()+
        json.dumps(validated['sd_files'],sort_keys=True).encode()+b''.join(archives[name] for name in s.IDS))
    declarations=['#pragma once','#include <stddef.h>',f'static constexpr char stageBase[]="{s.SOURCE}";',
        f'static constexpr char stageSdArchive[]="{s.SD_ARCHIVE}";',f'static constexpr char stageUtility[]="{utility}";',
        'struct StageFile { const char* name; size_t bytes; const char* sha; };',
        'struct StageApp { const char* id; size_t bytes; const char* sha; StageFile files[3]; };',
        'static constexpr StageApp stageApps[]={']
    for name,data in archives.items():
        files=s.package_inventory(data,name)
        ordered=['.package.json',name+'.elf',name+'.json']
        rows=','.join('{'+json.dumps(k)+','+str(files[k]['bytes'])+','+json.dumps(files[k]['sha256'])+'}' for k in ordered)
        declarations.append('{'+json.dumps(name)+','+str(len(data))+','+json.dumps(s.sha(data))+',{'+rows+'}},')
    declarations.append('};')
    declarations.append('static constexpr StageFile stageSdFiles[]={')
    for name,info in sorted(validated['sd_files'].items()):
        declarations.append('{'+json.dumps('/'+name)+','+str(info['bytes'])+','+json.dumps(info['sha256'])+'},')
    declarations.append('};')
    (target/'StageIdentity.h').write_text('\n'.join(declarations)+'\n')
    main=root/'src/main.cpp';text=main.read_text()
    begin=text.index('  // Handle incoming serial commands,')
    end=text.index('  // Check for any user activity',begin)
    old=text[begin:end]
    screenshot=old[old.index('        const uint32_t bufferSize'):old.index('      }\n    }\n  }')]
    text=text[:begin]+'#ifdef RISCRTE_CI_SERIAL_STAGING\n  if (readerStageTick()) {\n'+screenshot+'  }\n#else\n'+old+'#endif\n\n'+text[end:]
    text='#ifdef RISCRTE_CI_SERIAL_STAGING\n#include "SerialStaging.h"\n#endif\n'+text
    anchor='  static unsigned long lastActivityTime = millis();'
    s.require(text.count(anchor)==1,'Sleep guard anchor mismatch')
    text=text.replace(anchor,anchor+'\n#ifdef RISCRTE_CI_SERIAL_STAGING\n  if (readerStageActive()) lastActivityTime = millis();\n#endif')
    main.write_text(text)
    profile.write_text('[env:x4-ci-staging]\nextends = env:xteink-x4-pro\nbuild_flags =\n  ${env:xteink-x4-pro.build_flags}\n  -DRISCRTE_CI_SERIAL_STAGING=1\n  -Itest/hardware/reader\nextra_scripts =\n  ${base.extra_scripts}\n  post:test/hardware/reader/build.py\n')
    record={'production_base':s.SOURCE,'utility_id':utility,'expected_sd_archive_sha256':s.SD_ARCHIVE,'deployment_artifact_sha256':s.DEPLOYMENT_ARCHIVE,'expected_sd_files':validated['sd_files'],
      'instrumented_main_sha256':s.sha(main.read_bytes()),'firmware_sha256':None,'hardware_performed':False,
      'source_files':{p.name:s.sha(p.read_bytes()) for p in target.iterdir() if p.is_file()}}
    (root/'ci-staging-build.json').write_text(json.dumps(record,indent=2)+'\n')
    return record


def refresh(root,packages,artifact):
    record=json.loads((root/'ci-staging-build.json').read_text())
    s.require(s.sha((root/'src/main.cpp').read_bytes())==record['instrumented_main_sha256'],'Fixture main changed outside preparation')
    for name,digest in record['source_files'].items():
        s.require(s.sha((root/'test/hardware/reader'/name).read_bytes())==digest,'Fixture utility source changed')
    for path in ('src/main.cpp','platformio.local.ini'):
        (root/path).write_bytes(subprocess.check_output(['git','show','HEAD:'+path],cwd=root))
    shutil.rmtree(root/'test/hardware/reader')
    return prepare(root,packages,artifact)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('fixture',type=Path);p.add_argument('packages',type=Path)
    p.add_argument('--artifact',type=Path,required=True,help='Exact corrected schema-2 deployment ZIP')
    p.add_argument('--refresh',action='store_true')
    a=p.parse_args();print(json.dumps((refresh if a.refresh else prepare)(a.fixture,a.packages,a.artifact),indent=2))
