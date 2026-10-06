"""The winter memory preserves the exact book passage and operational log clue."""
import ast,pathlib,re
root=pathlib.Path(__file__).resolve().parents[2]
book=(root/'docs/HOLLOW_TRAIL_NOVELLA.md').read_text();a=book.index('I sat in the empty chair.');b=book.index('\n\nWhen the rain weakened',a)
expected=book[a:b].replace('’',"'").replace('“','"').replace('”','"')
entries=re.findall(r'^    ("(?:[^"\\]|\\.)*"),$',(root/'Apps/hollow_trail_lore.inc').read_text(),re.M)
actual=ast.literal_eval(entries[5]);assert expected in actual and actual.count("I sat in the empty chair.")==1
assert len(actual.encode())<4096 and 'central return fed the heaters below' in actual
assert 'THE CENTRE CONTACT IS THE HEATER RETURN.' in (root/'Apps/hollow_trail_evidence.inc').read_text()
print('Winter watch: exact book paragraph/return, retained relay clue and bounded journal body PASS')
