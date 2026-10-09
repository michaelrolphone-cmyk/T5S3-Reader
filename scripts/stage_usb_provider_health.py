#!/usr/bin/env python3
"""Stage exact production controller with only the outer export renamed.

No production/default source is rewritten. The selected wrapper supplies the
public getter; its owned dependency tables observe uncertain cleanup results.
"""
import argparse
import hashlib
import json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def stage(destination):
    source=ROOT/'Drivers/usb_controller_esp32s3/driver.cpp'
    text=source.read_text()
    old='const risc_driver_v2 *t5_driver_get(uint32_t abi) {\n    return abi == RISC_PROVIDER_DRIVER_ABI_V2 ? &hid_driver.base : nullptr;\n}'
    new=old.replace('t5_driver_get','health_controller_original_get',1)
    if text.count(old)!=1:raise ValueError('Expected exactly one final production controller export')
    destination.mkdir(parents=True,exist_ok=True)
    output=destination/'driver.health.body.inc'
    output.write_text(text.replace(old,new))
    (destination/'controller-health-stage.json').write_text(json.dumps({
        'source':'Drivers/usb_controller_esp32s3/driver.cpp','source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),
        'staged_sha256':hashlib.sha256(output.read_bytes()).hexdigest(),
        'sole_change':'outer t5_driver_get renamed health_controller_original_get'},indent=2)+'\n')
    return output
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('destination',type=Path)
    stage(p.parse_args().destination)
