import ast,pathlib,re
r=pathlib.Path(__file__).resolve().parents[2];b=(r/'docs/HOLLOW_TRAIL_NOVELLA.md').read_text()
a=b.index('An awning had been fastened between two posts.');z=b.index('At home, during our mother',a)
expected=b[a:z].strip().replace('’',"'").replace('“','"').replace('”','"')
entries=re.findall(r'^    ("(?:[^"\\]|\\.)*"),$',(r/'Apps/hollow_trail_lore.inc').read_text(),re.M)
body=ast.literal_eval(entries[13]);assert body.startswith(expected) and len(body.encode())<3800
assert 'Wait until everyone can stand.' in body and 'four' in body.lower()
print('Waiting awning: exact original passage, retained lock guidance and journal bounds PASS')
