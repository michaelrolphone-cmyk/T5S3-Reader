import ast, pathlib, re
r = pathlib.Path(__file__).resolve().parents[2]
book = (r / 'docs/HOLLOW_TRAIL_NOVELLA.md').read_text()
a = book.index('The first shelter post had a strip of scarf')
z = book.index('\n\nThe path steepened.', a)
expected = book[a:z].replace('’', "'").replace('“', '"').replace('”', '"')
entries = re.findall(r'^    ("(?:[^"\\]|\\.)*"),$', (r / 'Apps/hollow_trail_lore.inc').read_text(), re.M)
body = ast.literal_eval(entries[24])
assert body.startswith(expected) and len(body.encode()) < 3800
for phrase in ['Start reading the shelter posts at the scarf', 'notches record heights, not directions',
               'I left more than one way to find me', 'dated before the first lock',
               'could have been made later', 'the little backward stitch',
               'I thought I was following an order from elsewhere']:
    assert phrase in body
assert 'Match each post height to a chime, reading from the cloth toward the ridge' in ast.literal_eval(entries[25])
for name in ['hollow_trail_puzzles.inc', 'hollow_trail_maps.inc', 'hollow_trail_traversal.inc',
             'hollow_trail_isolator_study.inc', 'hollow_trail_endings.inc']:
    assert 'ht_scarf_' not in (r / 'Apps' / name).read_text()
assert 'THE OTHER HALF OF THE SCARF' in (r / 'Apps/hollow_trail_evidence.inc').read_text()
print('Ridge scarf: exact source passage, unchanged page24 direction and page25 chime clues, no puzzle/isolation/ending coupling PASS')
