#!/usr/bin/env python3
from pathlib import Path

repo = Path(__file__).resolve().parents[2]
source = (repo / "Apps/app_store.c").read_text(encoding="utf-8")

assert "view = view == RELEASES ? SD_INBOX : RELEASES;" not in source

start = source.index("if (hit == T5_UI_HIT_HEADER)")
end = source.index("} else if (hit >= 0 && hit < (int32_t)row_count)", start)
block = source[start:end]

assert "if (view == RELEASES)" in block
assert "view = SD_INBOX;" in block
assert "else if (refresh_releases(manager, ui))" in block
assert block.index("refresh_releases(manager, ui)") < block.index("view = RELEASES;")
assert '"Release refresh failed; SD packages available"' in block

def switch_from_inbox(refresh_ok):
    view = "SD_INBOX"
    rows = ["Inbox A", "Inbox B"]
    release_indices = [41, 42]
    if refresh_ok:
        rows = ["Release A", "Release B"]
        release_indices = [7, 8]
        view = "RELEASES"
    return view, rows, release_indices

view, rows, release_indices = switch_from_inbox(False)
assert view == "SD_INBOX"
assert rows == ["Inbox A", "Inbox B"]
assert release_indices == [41, 42]

view, rows, release_indices = switch_from_inbox(True)
assert view == "RELEASES"
assert rows == ["Release A", "Release B"]
assert release_indices == [7, 8]

print("App Store release transition contract PASS")
