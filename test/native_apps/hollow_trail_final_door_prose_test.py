#!/usr/bin/env python3
"""Preserve the canonical final notebook passage and its honest alternate."""
import ast
import pathlib
import re

root = pathlib.Path(__file__).resolve().parents[2]
book = (root/'docs/HOLLOW_TRAIL_NOVELLA.md').read_text()
chapter = book[book.index('## XI.'):]
expected = chapter[chapter.index('I took out the notebook.'):chapter.index('The pencil point broke at the full stop.')].strip()
expected = expected.replace('*', '').replace('“', '"').replace('”', '"').replace('’', "'")
source = (root/'Apps/hollow_trail_journal.inc').read_text()
match = re.search(r'ht_door_notebook_book\[\] =\s*("(?:[^"\\]|\\.)*");', source)
assert match, 'Canonical notebook literal missing'
actual = ast.literal_eval(match.group(1))
assert actual == expected, 'Canonical notebook differs from Chapter XI'
assert len(actual.encode()) < 4096, 'Notebook must fit the existing bounded buffer'
assert 'The original was sealed behind its shutter.' in source
print('Final notebook: exact Chapter XI passage and bounded journal body PASS')
