#!/usr/bin/env python3
"""The marked-tree archive is the authoritative book passage, not its old adaptation."""
import ast
import pathlib
import re

root = pathlib.Path(__file__).resolve().parents[2]
book = (root/'docs/HOLLOW_TRAIL_NOVELLA.md').read_text()
expected = book[book.index('A hooked branch carried'):book.index('There might be someone')].strip()
# The existing fallback glyph path is ASCII; preserve words and paragraph breaks.
expected = expected.replace('*', '').replace('“', '"').replace('”', '"').replace('’', "'")
source = (root/'Apps/hollow_trail_lore.inc').read_text().split('};', 1)[0]
entries = re.findall(r'^    ("(?:[^"\\]|\\.)*"),$', source, re.M)
actual = ast.literal_eval(entries[0])
assert actual == expected, 'Marked tree prose diverges from the authoritative novella'
assert len(actual.encode()) < 4096, 'Full passage must fit the journal body without truncation'
print('Marked tree: exact novella passage, preserved paragraphs and bounded journal body PASS')
