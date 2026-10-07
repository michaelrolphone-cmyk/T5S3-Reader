import ast,pathlib,re
r=pathlib.Path(__file__).resolve().parents[2];b=(r/'docs/HOLLOW_TRAIL_NOVELLA.md').read_text()
a=b.index('Then I followed the wire to its isolator.');z=b.index('\n\n## X.',a)
expected=b[a:z].strip().replace('’',"'").replace('“','"').replace('”','"')
entries=re.findall(r'^    ("(?:[^"\\]|\\.)*"),$',(r/'Apps/hollow_trail_lore.inc').read_text(),re.M)
body=ast.literal_eval(entries[26]);assert body.startswith(expected) and len(body.encode())<3800
assert 'The last post is the same height as the first.' in body and 'outgoing wire intact' in body
print('Ridge isolator: exact original passage, retained original tin-and-wire record and journal bounds PASS')
