#!/usr/bin/env python3
"""Avoid truncating SD directory identities in the actual Driver Manager app."""
from pathlib import Path

source = Path(__file__).resolve().parents[1] / 'Apps/driver_manager.c'
original = source.read_text(encoding='utf-8')
old = '''        packages[row_count] = info;
        snprintf(folders[row_count], sizeof(folders[row_count]), "%s", entry.name);
        snprintf(names[row_count], sizeof(names[row_count]), "%s", info.id);'''
new = '''        size_t folder_length = 0;
        while (folder_length < sizeof(entry.name) && entry.name[folder_length])
            ++folder_length;
        // A truncated folder would identify a different package. Skip it.
        if (!folder_length || folder_length >= sizeof(entry.name) ||
            folder_length >= sizeof(folders[0])) continue;
        packages[row_count] = info;
        memcpy(folders[row_count], entry.name, folder_length + 1u);
        snprintf(names[row_count], sizeof(names[row_count]), "%s", info.id);'''
if old in original:
    source.write_text(original.replace(old, new, 1), encoding='utf-8')
    print('Replaced truncating package identity copy with bounded exact copy')
elif new in original:
    print('Bounded package identity copy already applied')
else:
    raise SystemExit('Expected Driver Manager inbox copy missing; refusing unrelated mutation')
