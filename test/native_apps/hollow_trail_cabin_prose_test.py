import ast,pathlib,re
r=pathlib.Path(__file__).resolve().parents[2];b=(r/'docs/HOLLOW_TRAIL_NOVELLA.md').read_text()
a=b.index('At the signal cabin, a map');z=b.index('\n\nThe letter under a carriage seat',a)
expected=b[a:z].strip().replace('’',"'").replace('“','"').replace('”','"')
entries=re.findall(r'^    ("(?:[^"\\]|\\.)*"),$',(r/'Apps/hollow_trail_lore.inc').read_text(),re.M)
body=ast.literal_eval(entries[10]);assert body.startswith(expected) and len(body.encode())<3800
assert 'lever' in body and 'wagon' in body
print('Signal cabin: exact corrected-map/paper-dog passage and retained shunting guide PASS')
