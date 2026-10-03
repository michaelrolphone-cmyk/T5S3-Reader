#!/usr/bin/env python3
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory() as temp:
 objects=[]
 for name in ('SdBootFatFs','SdBootFatUnicode'):
  obj=Path(temp)/(name+'.o');objects.append(str(obj))
  subprocess.run(['cc','-std=c11','-DBOARD_XTEINK_X4_PRO','-Isrc','-c',str(root/'src/platform'/(name+'.c')),'-o',str(obj)],cwd=root,check=True)
 binary=Path(temp)/'test'
 subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-Isrc',str(root/'test/bootstrap_store/read_only_fat_test.cpp'),*objects,'-o',str(binary)],cwd=root,check=True)
 subprocess.run([str(binary)],check=True,timeout=5)
 symbols=subprocess.check_output(['nm',str(binary)],text=True)
 assert 'risc_boot_f_open' in symbols
 for forbidden in ('f_write','f_sync','f_mkdir','f_rename','f_unlink','disk_write'):
  assert not any(line.split() and line.split()[-1].lstrip('_')==forbidden for line in symbols.splitlines())
