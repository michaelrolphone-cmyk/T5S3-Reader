import ast,pathlib,re
r=pathlib.Path(__file__).resolve().parents[2];b=(r/'docs/HOLLOW_TRAIL_NOVELLA.md').read_text()
a=b.index('At the first house I stopped and listened.');z=b.index('\n\nThe mast stood on a terrace',a)
expected=b[a:z].strip().replace('’',"'").replace('“','"').replace('”','"')
entries=re.findall(r'^    ("(?:[^"\\]|\\.)*"),$',(r/'Apps/hollow_trail_lore.inc').read_text(),re.M)
body=ast.literal_eval(entries[27]);assert body.startswith(expected) and len(body.encode())<3800
assert 'I have left you a way to answer.' in body and 'first lamp' in body.lower()
print('First house: exact original passage, retained original lamp record and journal bounds PASS')
