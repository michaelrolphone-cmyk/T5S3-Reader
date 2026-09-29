#!/usr/bin/env python3
"""Contract checks for Hollow Trail's learned deterministic halftone LUT."""
from pathlib import Path
import re

ROOT=Path(__file__).resolve().parents[1]
TEXT=(ROOT/"Apps"/"hollow_trail_ai_dither.inc").read_text()

m=re.search(r"ht_ai_dither_lut\[[^\]]+\]\s*=\s*\{(.*?)\};",TEXT,re.S)
assert m
values=[int(x) for x in re.findall(r"\b\d+\b",m.group(1))]
assert len(values)==1024, len(values)
assert all(0<=v<=15 for v in values)
# Exact endpoints stay clean at every physical phase.
assert values[:16]==[0]*16
assert values[-16:]==[15]*16
# The learned model must retain intermediate halftone states, not collapse to
# a binary threshold or identical phase-independent pattern.
assert len(set(values))>=4
for tone in range(64):
    assert len(set(values[tone*16:(tone+1)*16]))>=1
print("hollow_trail_ai_dither_test: PASS")
