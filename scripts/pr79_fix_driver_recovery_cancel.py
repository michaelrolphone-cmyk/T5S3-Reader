#!/usr/bin/env python3
"""Fix a canceled menu matching a disabled -1 recovery action.

The menu returns -1 on Back/Exit; retry/discard indices are -1 when those
operations are unavailable. A canceled menu must never call a driver API.
"""
from pathlib import Path

path = Path('Apps/driver_manager.c')
source = path.read_text()
old = '''        const int32_t action = menu(ui, "Recovery actions", item.id, actions, choices);
        if (action == retry) {
'''
new = '''        const int32_t action = menu(ui, "Recovery actions", item.id, actions, choices);
        if (retry >= 0 && action == retry) {
'''
if old in source:
    assert source.count(old) == 1
    source = source.replace(old, new)
    path.write_text(source)
elif new not in source:
    raise SystemExit('Driver Manager recovery action anchor not found')
old = '''        } else if (action == discard && confirm(ui, item.id, "Discard retained files")) {
'''
new = '''        } else if (discard >= 0 && action == discard &&
                   confirm(ui, item.id, "Discard retained files")) {
'''
if old in source:
    assert source.count(old) == 1
    source = source.replace(old, new)
    path.write_text(source)
elif new not in source:
    raise SystemExit('Driver Manager discard action anchor not found')
print('Driver Manager canceled actions cannot match disabled recovery operations')
