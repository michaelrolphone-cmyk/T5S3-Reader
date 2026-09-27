#!/usr/bin/env python3
"""Contracts for responsive keyboard input independent of render cadence."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
CPP = (ROOT / "src/activities/util/KeyboardEntryActivity.cpp").read_text(encoding="utf-8")
HDR = (ROOT / "src/activities/util/KeyboardEntryActivity.h").read_text(encoding="utf-8")

assert "struct RenderState" in HDR
assert "stateMutex" in HDR
assert "requestKeyboardUpdate" in HDR
assert "measureInputHeightForTouch" in HDR
assert "class KeyboardStateLock" in CPP

capture = CPP.split(
    "KeyboardEntryActivity::RenderState KeyboardEntryActivity::captureRenderState()", 1
)[1].split("void KeyboardEntryActivity::requestKeyboardUpdate()", 1)[0]
assert "KeyboardStateLock lock(stateMutex);" in capture
for field in (
    "text", "cursorPos", "passwordVisible", "selectedRow", "selectedCol",
    "shiftState", "symMode", "cursorMode", "togglePos", "urlMode", "hintVisible",
):
    assert f"snapshot.{field} = {field};" in capture

request = CPP.split("void KeyboardEntryActivity::requestKeyboardUpdate()", 1)[1].split(
    "void KeyboardEntryActivity::onEnter()", 1
)[0]
assert "Activity::requestUpdate();" in request
assert "snapshot" not in request
assert "text =" not in request

loop = CPP.split("void KeyboardEntryActivity::loop()", 1)[1].split(
    "bool KeyboardEntryActivity::onTouchTap", 1
)[0]
touch = CPP.split("bool KeyboardEntryActivity::onTouchTap", 1)[1].split(
    "int KeyboardEntryActivity::measureInputHeightForTouch", 1
)[0]
assert "KeyboardStateLock stateLock(stateMutex);" in loop
assert "KeyboardStateLock stateLock(stateMutex);" in touch

render = CPP.split("void KeyboardEntryActivity::render(RenderLock&&)", 1)[1].split(
    "void KeyboardEntryActivity::onComplete", 1
)[0]
assert "const RenderState state = captureRenderState();" in render

# The render task must only consume the one local snapshot, never the mutable
# owner/input state directly after capture.
mutable = (
    "text", "cursorPos", "passwordVisible", "selectedRow", "selectedCol",
    "shiftState", "symMode", "cursorMode", "togglePos", "urlMode", "hintVisible",
)
for field in mutable:
    without_snapshot_reads = render.replace(f"state.{field}", "")
    assert re.search(rf"(?<![A-Za-z0-9_.]){field}\b", without_snapshot_reads) is None, field

key_loop = touch.index("for (int row = 0; row < contentRows; row++)")
bottom_loop = touch.index("for (int i = 0; i < BOTTOM_KEY_COUNT; i++)")
late_layout = touch.index("if (metrics.keyboardBottomAligned)", bottom_loop)
assert key_loop < bottom_loop < late_layout

# Bottom-aligned typing (the normal RiscRTE keyboard) must not measure/wrap
# entered text before trying key hitboxes.
prefix = touch[:key_loop]
assert prefix.count("measureInputHeightForTouch") == 1
assert "if (!metrics.keyboardBottomAligned)" in prefix

print("Keyboard input/render decoupling contracts passed")
