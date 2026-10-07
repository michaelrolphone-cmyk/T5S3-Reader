import ast,pathlib,re
r=pathlib.Path(__file__).resolve().parents[2];b=(r/'docs/HOLLOW_TRAIL_NOVELLA.md').read_text()
a=b.index('On the other side, a child’s pouch lay beneath a slab.');z=b.index('\n\nThe path rose through ledges',a)
expected=b[a:z].strip().replace('’',"'").replace('“','"').replace('”','"')
entries=re.findall(r'^    ("(?:[^"\\]|\\.)*"),$',(r/'Apps/hollow_trail_lore.inc').read_text(),re.M)
body=ast.literal_eval(entries[15]);assert body.startswith(expected) and len(body.encode())<3800
assert 'Keep these little stones out of the gears.' in body and 'Six equal loads' in body
print('Quarry pouch: exact original passage, retained hoist guidance and journal bounds PASS')
