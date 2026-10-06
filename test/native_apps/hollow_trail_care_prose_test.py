"""Evening care follows the novella without new evidence or puzzle conditions."""
import pathlib
root = pathlib.Path(__file__).resolve().parents[2]
book = (root/'docs/HOLLOW_TRAIL_NOVELLA.md').read_text().upper()
study = (root/'Apps/hollow_trail_care_study.inc').read_text()
for phrase in ('THAT EVENING I MOVED A WATERLOGGED TRAY OUT OF A DRIP',
               'AND SHIMMED IT LEVEL WITH SCRAPS OF WOOD.',
               'I TIED A STEM WITH THREAD FROM MY CUFF',
               'THEN TURNED SEEDLINGS LEANING TOO FAR TOWARD THE LIGHT.',
               'THE TASKS QUIETED ME.',
               'I FOUND THE COUNT WHILE I WAS TURNING THE TRAYS.'):
    assert phrase in book and phrase in study
for name in ('hollow_trail_puzzles.inc', 'hollow_trail_evidence.inc', 'hollow_trail_endings.inc', 'hollow_trail_traversal.inc'):
    assert 'care_tended' not in (root/'Apps'/name).read_text()
assert 'ht_journal_open' not in study and 'ht_inspect' not in study
print('Evening care: actual novella actions, no remote count/evidence acquisition and no route/ending gate PASS')
