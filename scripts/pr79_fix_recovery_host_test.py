#!/usr/bin/env python3
"""Keep recovery host-test events faithful to nested UI Back semantics."""
from pathlib import Path

path = Path('test/native_apps/driver_manager_test.c')
source = path.read_text(encoding='utf-8')
old = '''    retained_state = T5_DRIVER_RECOVERY_MAPPED;
    retained_retry = false; retained_discard = false; retained_count = 1;
    reset_queue(); enqueue(T5_UI_EVENT_CONFIRM); enqueue(T5_UI_EVENT_BACK);
    assert(recovery_screen(&manager, &recovery_ui));
'''
new = '''    retained_state = T5_DRIVER_RECOVERY_MAPPED;
    retained_retry = false; retained_discard = false; retained_count = 1;
    reset_queue();
    enqueue(T5_UI_EVENT_CONFIRM); // Open actions: only Cancel is available.
    enqueue(T5_UI_EVENT_BACK);    // Cancel the nested action menu.
    enqueue(T5_UI_EVENT_BACK);    // Then leave the recovery screen.
    assert(recovery_screen(&manager, &recovery_ui));
'''
if old in source:
    path.write_text(source.replace(old, new, 1), encoding='utf-8')
elif new in source:
    print('Recovery host test already updated')
else:
    raise SystemExit('Unexpected recovery test layout; refusing to patch')
