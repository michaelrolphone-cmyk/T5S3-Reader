import ast,pathlib,re
r=pathlib.Path(__file__).resolve().parents[2];b=(r/'docs/HOLLOW_TRAIL_NOVELLA.md').read_text()
a=b.index('In the next house, seven sleeping places');z=b.index('\n\nThat evening I moved',a)
expected=b[a:z].strip().replace('’',"'").replace('“','"').replace('”','"')
entries=re.findall(r'^    ("(?:[^"\\]|\\.)*"),$',(r/'Apps/hollow_trail_lore.inc').read_text(),re.M)
body=ast.literal_eval(entries[18]);assert body.startswith(expected) and len(body.encode())<3800
assert 'The low receiver is sealed behind condensation.' in body
print('Sleeping house: exact original passage, retained glasshouse guidance and journal bounds PASS')
