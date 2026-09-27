#!/usr/bin/env python3
from pathlib import Path

repo = Path(__file__).resolve().parents[2]
source = (repo / "Apps/rom_manager.c").read_text(encoding="utf-8")

anchor = 't5_ui_list_row_t actions[3]={{"Rename","Change filename","",0},{"Delete","Remove this ROM","",0},{"Cancel","Return to list","",0}};int32_t a=0;'
start = source.index(anchor)
end = source.index("\n    }\n  }\n  release_workspaces();", start)
modal = source[start:end]

assert "x.type==T5_UI_EVENT_TAP" in modal
assert "ui->hit_test(x.touch_x,x.touch_y)" in modal
assert "hit>=0&&hit<3" in modal
assert "if(hit==a)x.type=T5_UI_EVENT_CONFIRM;else a=hit;" in modal

selected = 0
hit = 1
confirm = hit == selected
if not confirm:
    selected = hit
assert (selected, confirm) == (1, False)
hit = 1
confirm = hit == selected
assert (selected, confirm) == (1, True)

print("Rom Manager actions touch contract PASS")
