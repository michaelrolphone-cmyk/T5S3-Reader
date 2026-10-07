#!/usr/bin/env python3
"""Verify the opt-in helper leaves both legacy transport objects byte-identical."""
from pathlib import Path
import hashlib
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
BASE = os.environ.get('STORAGE_SLEEP_BASE', 'cccfa3fd9b606998c27adb03665722ea44d5358f')
CC = os.environ.get('CC', 'cc')

with tempfile.TemporaryDirectory() as temporary:
    folder = Path(temporary)
    volume = folder/'volume.c'
    driver = folder/'driver.c'
    baseline = subprocess.check_output(['git', 'show', BASE+':Drivers/storage_fatfs/volume.c'], cwd=ROOT)
    current = (ROOT/'Drivers/storage_fatfs/volume.c').read_bytes()
    for transport in ('x4pro_sd', 't5s3_sd'):
        source = (ROOT/'Drivers'/transport/'driver.c').read_text()
        source = source.replace('"../x4pro_i2c/os_cpu_v1.h"', '"'+str(ROOT/'Drivers/x4pro_i2c/os_cpu_v1.h')+'"')
        source = source.replace('"../storage_fatfs/volume.c"', '"'+str(volume)+'"')
        source = source.replace('"../storage_fatfs/sd_protocol.h"', '"'+str(ROOT/'Drivers/storage_fatfs/sd_protocol.h')+'"')
        driver.write_text(source)
        objects = []
        for label, content in (('base', baseline), ('current', current)):
            volume.write_bytes(content)
            output = folder/(label+'.o')
            subprocess.run([CC, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                            '-I'+str(ROOT/'test/storage_volume/gate_fake'),
                            '-I'+str(ROOT/'sdk/driver'), '-I'+str(ROOT/'Drivers/x4pro_board'),
                            '-I'+str(ROOT/'Drivers/storage_fatfs'), '-c', str(driver), '-o', str(output)], check=True)
            objects.append(output.read_bytes())
        assert objects[0] == objects[1], transport+' changed without a resume hook'
        print(transport+' no-hook object byte parity PASS: '+hashlib.sha256(objects[1]).hexdigest())
