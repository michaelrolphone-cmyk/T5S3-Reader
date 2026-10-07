import ast,pathlib,re
r=pathlib.Path(__file__).resolve().parents[2];b=(r/'docs/HOLLOW_TRAIL_NOVELLA.md').read_text()
a=b.index('On the ledge I found a recorder manual');z=b.index('\n\nRain drained from the gallery roof.',a)
expected=b[a:z].replace('’',"'").replace('“','"').replace('”','"').replace('—','--')
entries=re.findall(r'^    ("(?:[^"\\]|\\.)*"),$',(r/'Apps/hollow_trail_lore.inc').read_text(),re.M)
body=ast.literal_eval(entries[23]);assert body.startswith(expected) and len(body.encode())<3800
for phrase in ['the return is being primed', 'the return closes the loop', 'half the six-unit supply', 'A signature proves a pattern, not a person.', 'A clean thumbprint', 'tip of my damaged knife']:
 assert phrase in body
for name in ['hollow_trail_puzzles.inc','hollow_trail_maps.inc','hollow_trail_traversal.inc','hollow_trail_warming_study.inc']:
 assert 'ht_recorder_' not in (r/'Apps'/name).read_text()
print('Recorder manual: exact Chapter VIII passage, retained priming/bypass guidance, bounded original page 23 and unchanged puzzle/warming source PASS')
