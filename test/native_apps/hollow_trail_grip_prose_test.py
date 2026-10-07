import ast,pathlib,re
r=pathlib.Path(__file__).resolve().parents[2];b=(r/'docs/HOLLOW_TRAIL_NOVELLA.md').read_text()
a=b.index('The hoist stood at the edge of the upper works.');z=b.index('\n\nI was afraid of the cage.',a)
expected=b[a:z].replace('’',"'").replace('“','"').replace('”','"').replace('—','--')
entries=re.findall(r'^    ("(?:[^"\\]|\\.)*"),$',(r/'Apps/hollow_trail_lore.inc').read_text(),re.M)
body=ast.literal_eval(entries[16]);assert body.startswith(expected) and len(body.encode())<3800
for phrase in ['Both arms and the brake must bear weight.','none can be added or discarded.','Four screw holes','There was no plate.']:assert phrase in body
for name in ['hollow_trail_puzzles.inc','hollow_trail_maps.inc','hollow_trail_traversal.inc','hollow_trail_hoist_scene.inc']:assert 'ht_grip_' not in (r/'Apps'/name).read_text()
print('Hoist grip: exact novella passage, preserved load guidance, journal bound and no puzzle/descent mutation PASS')
