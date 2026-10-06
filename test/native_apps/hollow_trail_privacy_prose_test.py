"""Privacy carries the novella's concrete gesture without a progress gate."""
import pathlib
root = pathlib.Path(__file__).resolve().parents[2]
book = (root/'docs/HOLLOW_TRAIL_NOVELLA.md').read_text().upper()
study = (root/'Apps/hollow_trail_privacy_study.inc').read_text()
for phrase in ('SOMEONE HAD CUT A CURTAIN FROM OLD SACKING',
               'BEHIND IT A PERSON WOULD BE VISIBLE ONLY BY THEIR FEET.',
               'I PRESSED A CLEAN FOLD OF THE TOWEL TO MY CHEEK',
               'AND LEFT A PRINT OF QUARRY DUST.',
               'I LINGERED IN THAT IMPROVISED PRIVACY.',
               'ONE CORNER SAGGED BECAUSE THE HOOK HAD COME AWAY',
               'MY SISTER USED TO CATCH IT BACK WITH A PIN WHENEVER SHE PASSED.',
               'HERE SHE HAD FOUND A WALL WHERE THERE WAS NO WALL',
               'A ROOM INSIDE A RUINED BUILDING'):
    assert phrase in book and phrase in study
for name in ('hollow_trail_puzzles.inc', 'hollow_trail_evidence.inc', 'hollow_trail_endings.inc', 'hollow_trail_traversal.inc'):
    assert 'ht_privacy' not in (root/'Apps'/name).read_text()
assert 'ht_journal_open' not in study and 'ht_inspect' not in study
print('Privacy: novella wording and concrete towel/curtain gesture; no new evidence, route or ending condition PASS')
