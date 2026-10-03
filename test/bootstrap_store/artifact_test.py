#!/usr/bin/env python3
"""Required hidden ordinary manifests survive a dotfile-excluding uploader."""
from pathlib import Path
import hashlib,sys,tempfile,zipfile
root=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(root/'scripts'))
from build_x4_module_store import pack_sd_tree
with tempfile.TemporaryDirectory() as temporary:
 folder=Path(temporary);tree=folder/'tree'
 (tree/'Drivers/example').mkdir(parents=True)
 (tree/'Drivers/example/.package.json').write_bytes(b'{"schema":1}\n')
 (tree/'Drivers/example/driver.elf').write_bytes(b'fixture')
 archive=folder/'sdcard.zip'
 record=pack_sd_tree(tree,archive)
 assert record==dict(bytes=archive.stat().st_size,sha256=hashlib.sha256(archive.read_bytes()).hexdigest())
 with zipfile.ZipFile(archive) as z:
  assert z.read('sdcard/Drivers/example/.package.json')==b'{"schema":1}\n'
  assert len(z.namelist())==2
 # Outer upload sees only a visible ZIP. Hidden members remain opaque bytes.
 upload=folder/'upload.zip'
 with zipfile.ZipFile(upload,'w') as z:z.write(archive,archive.name)
 with zipfile.ZipFile(upload) as z:assert z.read('sdcard.zip')==archive.read_bytes()
print('SD deployment ZIP: hidden package manifests and complete round-trip PASS')
