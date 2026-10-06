#!/usr/bin/env python3
"""Keep remembered claims grounded in the Chapter VII passage."""
import pathlib
import re
root = pathlib.Path(__file__).resolve().parents[2]
book = (root/'docs/HOLLOW_TRAIL_NOVELLA.md').read_text().split('## VII. Things Still Growing',1)[1].split('## VIII.',1)[0]
source = (root/'Apps/hollow_trail_garden_memory.inc').read_text()
normalize = lambda s: re.sub(r'[^a-z0-9]+', ' ', s.lower()).strip()
for caption in re.findall(r'return "([A-Z][A-Z .]+)";', source):
    assert normalize(caption) in normalize(book), caption
print('Garden sister memory: every remembered caption is a Chapter VII excerpt PASS')
