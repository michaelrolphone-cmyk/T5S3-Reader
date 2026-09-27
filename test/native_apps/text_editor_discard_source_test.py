#!/usr/bin/env python3
from pathlib import Path

repo = Path(__file__).resolve().parents[2]
source = (repo / "Apps/text_editor.c").read_text(encoding="utf-8")

discard_start = source.index("static bool discard_changes(void)")
discard_end = source.index("\nstatic bool load_file", discard_start)
discard = source[discard_start:discard_end]

assert "if (!path[0] || !storage->exists(path))" in discard
assert "te_reset(&document);" in discard
assert "path[0] = 0;" in discard
assert "filename[0] = 0;" in discard
assert "storage->read_file(path, NULL, 0, &size)" in discard
assert "storage->read_file(path, scratch, TE_CAPACITY, &count)" in discard
assert "count != size" in discard
assert "!te_import(&document, scratch, count)" in discard
assert 'report("Discard FAILED. Edits remain in memory.");' in discard

unsaved_start = source.index("if (mode == UNSAVED)")
unsaved_end = source.index("\n    if (mode == NEW_NAME", unsaved_start)
unsaved = source[unsaved_start:unsaved_end]
assert "if (discard_changes()) continue_after();" in unsaved
assert "document.dirty = false" not in unsaved

def discard_model(path_present, exists, reload_ok):
    dirty = True
    cleared_identity = False
    if not path_present or not exists:
        dirty = False
        cleared_identity = True
        return True, dirty, cleared_identity
    if not reload_ok:
        return False, dirty, cleared_identity
    dirty = False
    return True, dirty, cleared_identity

assert discard_model(True, True, True) == (True, False, False)
assert discard_model(True, True, False) == (False, True, False)
assert discard_model(True, False, False) == (True, False, True)
assert discard_model(False, False, False) == (True, False, True)

print("Text Editor discard restore contract PASS")
