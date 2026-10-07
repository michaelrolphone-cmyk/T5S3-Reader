import ast,pathlib,re
r=pathlib.Path(__file__).resolve().parents[2];b=(r/'docs/HOLLOW_TRAIL_NOVELLA.md').read_text()
a=b.index('I was afraid of the cage.');z=b.index('\n\nThe weights were stacked',a)
expected=b[a:z].replace('’',"'").replace('“','"').replace('”','"').replace('—','--')
entries=re.findall(r'^    ("(?:[^"\\]|\\.)*"),$',(r/'Apps/hollow_trail_lore.inc').read_text(),re.M)
body=ast.literal_eval(entries[17]);assert body.startswith(expected) and len(body.encode())<3800
for phrase in ['The left arm reaches three paces','the right reaches two','Seven, including you.','When I am no longer with them--','struck my shoulder']:
 assert phrase in body
for name in ['hollow_trail_puzzles.inc','hollow_trail_maps.inc','hollow_trail_traversal.inc','hollow_trail_hoist_scene.inc']:
 assert 'ht_floorboard_' not in (r/'Apps'/name).read_text()
print('Floorboard note: exact Chapter VI passage, pre-solution arm guidance, original page 17, bounded journal and unchanged puzzle/descent source PASS')
