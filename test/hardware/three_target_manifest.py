"""Freeze only target firmware bytes and exact CI provenance, never host code."""
import hashlib
import json
from pathlib import Path
import re
import sys

ENVIRONMENTS={'cam-sd':'cam-camera-app-experiment','cam-nosd':'cam-nosd-ci','x4':'xteink-x4-pro'}

def main():
    target,folder,sha,run,attempt=sys.argv[1:]
    if target not in ENVIRONMENTS or not re.fullmatch('[0-9a-f]{40}',sha):
        raise ValueError('Unknown target or nonexact source SHA')
    root=Path(__file__).resolve().parents[2]
    dest=Path(folder); dest.mkdir(parents=True,exist_ok=False)
    result={'schema':1,'target':target,'source_sha':sha,'run_id':int(run),'run_attempt':int(attempt),'images':{}}
    for name,path in [('candidate',root/'.pio/build'/ENVIRONMENTS[target]/'firmware.bin'),('heartbeat',root/'test/hardware/heartbeat/.pio/build'/target/'firmware.bin')]:
        data=path.read_bytes()
        if not 0<len(data)<=0x640000: raise ValueError('Image size out of bounds')
        filename=name+'.bin'; (dest/filename).write_bytes(data)
        result['images'][name]={'file':filename,'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest(),'offset':0x10000}
    (dest/'manifest.json').write_text(json.dumps(result,sort_keys=True)+'\n')

if __name__=='__main__': main()
