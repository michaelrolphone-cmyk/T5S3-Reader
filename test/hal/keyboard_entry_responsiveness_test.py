#!/usr/bin/env python3
"""Contracts for responsive keyboard input independent of render cadence."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
CPP = (ROOT / "src/activities/util/KeyboardEntryActivity.cpp").read_text(encoding="utf-8")
HDR = (ROOT / "src/activities/util/KeyboardEntryActivity.h").read_text(encoding="utf-8")

assert "struct RenderState" in HDR
assert "renderStateMutex" in HDR
assert "requestKeyboardUpdate" in HDR
assert "measureInputHeightForTouch" in HDR

publish = CPP.split("void KeyboardEntryActivity::publishRenderState()", 1)[1].split(
    "KeyboardEntryActivity::RenderState KeyboardEntryActivity::captureRenderState", 1
)[0]
for field in (
    "text", "cursorPos", "passwordVisible", "selectedRow", "selectedCol",
    "shiftState", "symMode", "cursorMode", "togglePos", "urlMode", "hintVisible",
):
    assert f"renderState.{field}" in publish

request = CPP.split("void KeyboardEntryActivity::requestKeyboardUpdate()", 1)[1].split(
    "void KeyboardEntryActivity::onEnter()", 1
)[0]
assert request.index("publishRenderState();") < request.index("Activity::requestUpdate();")

render = CPP.split("void KeyboardEntryActivity::render(RenderLock&&)", 1)[1].split(
    "void KeyboardEntryActivity::onComplete", 1
)[0]
assert "const RenderState state = captureRenderState();" in render

# The render task must only consume the published snapshot, never the mutable
# owner/input state directly.
mutable = (
    "text", "cursorPos", "passwordVisible", "selectedRow", "selectedCol",
    "shiftState", "symMode", "cursorMode", "togglePos", "urlMode", "hintVisible",
)
for field in mutable:
    without_snapshot_reads = render.replace(f"state.{field}", "")
    assert re.search(rf"(?<![A-Za-z0-9_.]){field}\b", without_snapshot_reads) is None, field

touch = CPP.split("bool KeyboardEntryActivity::onTouchTap", 1)[1].split(
    "int KeyboardEntryActivity::measureInputHeightForTouch", 1
)[0]
key_loop = touch.index("for (int row = 0; row < contentRows; row++)")
bottom_loop = touch.index("for (int i = 0; i < BOTTOM_KEY_COUNT; i++)")
late_layout = touch.index("if (metrics.keyboardBottomAligned)", bottom_loop)
assert key_loop < bottom_loop < late_layout

# Bottom-aligned typing (the normal RiscRTE keyboard) must not measure/wrap
# the text before trying key hitboxes. The only earlier measurement is for a
# non-bottom-aligned theme.
prefix = touch[:key_loop]
assert prefix.count("measureInputHeightForTouch") == 1
assert "if (!metrics.keyboardBottomAligned)" in prefix

print("Keyboard input/render decoupling contracts passed")
