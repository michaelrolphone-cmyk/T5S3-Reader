import ast
import pathlib
import re

r = pathlib.Path(__file__).resolve().parents[2]
book = (r / 'docs/HOLLOW_TRAIL_NOVELLA.md').read_text()
a = book.index('Inside, a flat stone held papers')
z = book.index('\n\nA light flashed there.', a)
expected = book[a:z].replace('’', "'").replace('“', '"').replace('”', '"')
entries = re.findall(r'^    ("(?:[^"\\]|\\.)*"),$', (r / 'Apps/hollow_trail_lore.inc').read_text(), re.M)
body = ast.literal_eval(entries[25])
assert body.startswith(expected) and len(body.encode()) < 3800
for phrase in ['Match each post height to a chime, reading from the cloth toward the ridge',
               'Neither page explains the correction', 'The stone shelters all three readings equally']:
    assert phrase in body
for name in ['hollow_trail_puzzles.inc', 'hollow_trail_maps.inc', 'hollow_trail_traversal.inc',
             'hollow_trail_isolator_study.inc', 'hollow_trail_endings.inc']:
    assert 'ht_papers_' not in (r / 'Apps' / name).read_text()
assert 'THE WIND SHELTER' in (r / 'Apps/hollow_trail_evidence.inc').read_text()
print('Ridge papers: exact shelter text, original page25 direction/chimes/ambiguity and no route/ending coupling PASS')
