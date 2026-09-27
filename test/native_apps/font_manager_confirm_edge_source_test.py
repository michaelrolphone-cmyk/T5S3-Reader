#!/usr/bin/env python3
from pathlib import Path

repo = Path(__file__).resolve().parents[2]
source = (repo / "Apps/font_manager.c").read_text(encoding="utf-8")

assert "static bool confirm_rising_edge(uint32_t buttons, bool *was_pressed)" in source
assert "const bool pressed = (buttons & T5_APP_BUTTON_CONFIRM) != 0;" in source
assert "const bool rising = pressed && !*was_pressed;" in source
assert "*was_pressed = pressed;" in source
assert "bool confirm_was_pressed = false;" in source
assert "if (confirm_rising_edge(input.buttons, &confirm_was_pressed)) activate_selected();" in source
assert "if (input.buttons & T5_APP_BUTTON_CONFIRM) activate_selected();" not in source

sequence = [True, True, True, False, True]
was_pressed = False
edges = []
for pressed in sequence:
    rising = pressed and not was_pressed
    was_pressed = pressed
    edges.append(rising)

assert edges == [True, False, False, False, True], edges
print("Font Manager confirm rising-edge contract PASS")
