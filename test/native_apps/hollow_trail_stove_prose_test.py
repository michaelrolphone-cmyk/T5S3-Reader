"""Preserve Chapter III's actual stove passage without removing vessel guidance."""
import ast,pathlib,re
r=pathlib.Path(__file__).resolve().parents[2]
b=(r/'docs/HOLLOW_TRAIL_NOVELLA.md').read_text()
a=b.index('There was a stove in the next shed.');z=b.index('\n\nFarther west,',a)
expected=b[a:z].replace('’',"'").replace('“','"').replace('”','"')
entries=re.findall(r'^    ("(?:[^"\\]|\\.)*"),$',(r/'Apps/hollow_trail_lore.inc').read_text(),re.M)
actual=ast.literal_eval(entries[7])
assert expected in actual and len(actual.encode())<3800
assert 'a transfer stops only when a vessel is full or the other is empty' in actual
print('Stove prose: exact book passage, independent retained vessel guidance and journal bounds PASS')
