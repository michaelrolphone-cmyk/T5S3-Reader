import ast,pathlib,re
r=pathlib.Path(__file__).resolve().parents[2];b=(r/'docs/HOLLOW_TRAIL_NOVELLA.md').read_text()
a=b.index('In the morning, light lay across the beds');z=b.index('\n\nAt the back of the house, a glass partition',a)
expected=b[a:z].strip().replace('’',"'").replace('“','"').replace('”','"').replace('*','')
entries=re.findall(r'^    ("(?:[^"\\]|\\.)*"),$',(r/'Apps/hollow_trail_lore.inc').read_text(),re.M)
body=ast.literal_eval(entries[20]);assert expected in body and len(body.encode())<3800
assert 'high path must stay straight' in body and 'low path turns toward the floor' in body
print('Glasshouse drawings: exact book passage, unchanged mirror instructions and bounded journal PASS')
