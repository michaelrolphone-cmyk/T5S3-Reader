"""Chapter VII's cupboard wording and original evidence identities stay intact."""
import pathlib
import re

root = pathlib.Path(__file__).resolve().parents[2]
book = (root / 'docs/HOLLOW_TRAIL_NOVELLA.md').read_text()
study = (root / 'Apps/hollow_trail_food_study.inc').read_text()
draw = (root / 'Apps/hollow_trail_food_draw.inc').read_text()
assert '*Keep enough to begin again.*' in book
assert '"KEEP ENOUGH TO"' in study and '"BEGIN AGAIN."' in study
for phrase in ('I DRANK.', 'WAITED.', 'DRANK AGAIN.', 'I SHUT THE DOOR GENTLY.'):
    assert phrase in study
assert 'sealed jar of dried beans' in book and 'SEALED BEANS' in study
assert 'roots laid in sand' in book and 'ROOTS IN SAND' in study
assert '"PORTIONS  DAYS  PEOPLE"' in study
assert 'THE FIRST BITE HURT MY DRY MOUTH.' in study
assert 'I ATE TOO QUICKLY ANYWAY.' in study
assert 'I STOOD STILL UNTIL THE CRAMP EASED.' in study
assert not re.search(r'\b(?:19|20)\d{2}\b', study + draw)
assert 'ht_journal_open' not in study and 'evidence |=' not in study
assert '#define HT_FOOD_X 373' in draw
print('Food/drink: book cupboard note, beans/biscuits/sand roots, deliberate waiting and recovery, no invented date or added evidence PASS')
