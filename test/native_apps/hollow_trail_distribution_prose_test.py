import ast,pathlib,re
r=pathlib.Path(__file__).resolve().parents[2];b=(r/'docs/HOLLOW_TRAIL_NOVELLA.md').read_text()
a=b.index('Farther west, the ground dropped toward a distribution station.');z=b.index('\n\n## IV.',a)
expected=b[a:z].replace('’',"'").replace('“','"').replace('”','"')
entries=re.findall(r'^    ("(?:[^"\\]|\\.)*"),$',(r/'Apps/hollow_trail_lore.inc').read_text(),re.M)
body=ast.literal_eval(entries[8]);assert body==expected and len(body.encode())<3800
assert 'eight, five and three' in body and 'four measures stood in each' in body
print('Distribution: exact original lists/date/measure passage and journal bounds PASS')
