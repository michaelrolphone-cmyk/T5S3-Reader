import ast, pathlib, re
r = pathlib.Path(__file__).resolve().parents[2]
book = (r / 'docs/HOLLOW_TRAIL_NOVELLA.md').read_text()
a = book.index('At the turbine shed, the ground trembled.')
z = book.index('\n\nOutside, the service route climbed', a)
expected = book[a:z].replace('’', "'").replace('“', '"').replace('”', '"').replace('—', '--')
entries = re.findall(r'^    ("(?:[^"\\]|\\.)*"),$', (r / 'Apps/hollow_trail_lore.inc').read_text(), re.M)
body = ast.literal_eval(entries[21])
assert body.startswith(expected) and len(body.encode()) < 3800
for phrase in ['three measures for a cold start', 'round running mark is one scratch lower',
               'Restoring the lamp also restored the writing arm', 'not why she powered them',
               'sheet of clear mica', 'Scratches doubled the gauge needle until I shifted my head',
               'one branch *lamps*', 'labelled *recorder*']:
    assert phrase in body
for name in ['hollow_trail_puzzles.inc', 'hollow_trail_maps.inc', 'hollow_trail_traversal.inc',
             'hollow_trail_warming_study.inc', 'hollow_trail_endings.inc']:
    assert 'ht_gauge_' not in (r / 'Apps' / name).read_text()
assert 'THE TURBINE GAUGE' in (r / 'Apps/hollow_trail_evidence.inc').read_text()
print('Turbine gauge: exact Chapter VIII passage, preserved cold-start/running clues, original page 21, no motive verdict or puzzle/ending coupling PASS')
