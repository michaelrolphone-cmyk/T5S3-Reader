import ast,pathlib,re
r=pathlib.Path(__file__).resolve().parents[2];b=(r/'docs/HOLLOW_TRAIL_NOVELLA.md').read_text()
a=b.index('At the distributor, the cold return');z=b.index('\n\nAt the end of the gallery',a)
expected=b[a:z].strip().replace('’',"'").replace('“','"').replace('”','"')
entries=re.findall(r'^    ("(?:[^"\\]|\\.)*"),$',(r/'Apps/hollow_trail_lore.inc').read_text(),re.M)
body=ast.literal_eval(entries[22]);assert body.startswith(expected) and len(body.encode())<3800
assert 'The cold return swallows three measures' in body and 'one measure keeps the gardens warm' in body
print('Warming return: exact original passage, retained original return-pipe record and journal bounds PASS')
