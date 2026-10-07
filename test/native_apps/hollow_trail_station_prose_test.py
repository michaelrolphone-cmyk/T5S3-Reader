"""HOME belongs in the station; chronological order is relative, not invented dates."""
import ast,pathlib,re
r=pathlib.Path(__file__).resolve().parents[2]
b=(r/'docs/HOLLOW_TRAIL_NOVELLA.md').read_text()
a=b.index('Inside the station, a ticket marked HOME');z=b.index('\n\nWest of the station,',a)
expected=b[a:z].replace('’',"'").replace('“','"').replace('”','"')
entries=re.findall(r'^    ("(?:[^"\\]|\\.)*"),$',(r/'Apps/hollow_trail_lore.inc').read_text(),re.M)
body=ast.literal_eval(entries[9]);assert expected in body and len(body.encode())<3800
assert 'people travel between the heavy brake and the fuel' in body
for name in ['hollow_trail_story.inc','hollow_trail_lore.inc']:
 assert 'TICKET IS PINNED INSIDE A CARRIAGE' not in (r/'Apps'/name).read_text().upper()
assert 'THE WEATHER WARNING IS EARLIER STILL.' in (r/'Apps/hollow_trail_evidence.inc').read_text()
print('Station prose: exact book setting/passage, relative date order, retained shunting guidance and journal bounds PASS')
