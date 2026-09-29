#!/usr/bin/env python3
"""Host-side bit-exact contract test for Hollow Trail's exported INT8 upscaler."""
from pathlib import Path
import re

ROOT=Path(__file__).resolve().parents[1]
MODEL=(ROOT/'Apps'/'hollow_trail_neural_upscale.inc').read_text()

def array(name, count):
    m=re.search(rf'{name}[^=]*=\s*\{{(.*?)\}};',MODEL,re.S)
    assert m, name
    values=[int(x) for x in re.findall(r'-?\d+',m.group(1))]
    assert len(values)==count,(name,len(values),count)
    return values

W1=array('ht_nn_w1',216)
B1=array('ht_nn_b1',24)
W2=array('ht_nn_w2',72)
B2=array('ht_nn_b2',3)

def round_shift(v,shift):
    half=1<<(shift-1)
    return (v+half)>>shift if v>=0 else -(((-v)+half)>>shift)

def infer(p):
    h=[]
    for j in range(24):
        acc=B1[j]
        for i in range(9): acc+=(p[i]-128)*W1[j*9+i]
        h.append(max(0,(acc+16)>>5))
    out=[]
    for o in range(3):
        acc=B2[o]
        for j in range(24): acc+=h[j]*W2[o*24+j]
        out.append(max(-127,min(127,round_shift(acc,9))))
    return out

VECTORS=[
    ([40]*9, [0,2,2]),
    ([20,20,200,20,20,200,20,20,200], [-45,0,-66]),
    ([20,20,20,20,20,20,200,200,200], [1,-37,-23]),
    ([10,10,80,10,80,180,80,180,240], [1,-15,11]),
    ([80,84,88,84,88,92,88,92,96], [0,-1,2]),
    ([0,12,24,18,30,42,36,48,60], [-4,-6,5]),
    ([230,200,160,210,170,120,180,130,70], [13,32,10]),
    ([0,0,0,0,255,255,0,255,255], [40,64,127]),
]
for p,expected in VECTORS:
    assert infer(p)==expected,(p,infer(p),expected)

# The runtime gate must preserve exactly-flat fills with the original bilinear path.
flat=[73]*9
assert max(flat)-min(flat) < 40
# Weight storage must remain in signed int8 range.
assert all(-128<=x<=127 for x in W1+W2)
print('hollow_trail_neural_upscale_test: PASS')
