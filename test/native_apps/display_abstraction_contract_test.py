#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[2]

display_api = (root / "sdk/driver/RiscDisplayOutputV1.h").read_text()
surface = (root / "lib/DisplaySurface/DisplaySurface.h").read_text()
gfx = (root / "lib/GfxRenderer/GfxRenderer.h").read_text()
hal = (root / "lib/hal/HalDisplay.h").read_text()
ui = (root / "lib/NativeApps/include/T5UiApi.h").read_text()
bridge = (root / "src/native/NativeUiBridge.cpp").read_text()

assert '"display.output"' in display_api
assert '"display.primary"' in display_api
assert "RISC_DISPLAY_QUEUE_MAILBOX" in display_api
assert "RISC_DISPLAY_PRESENT_CLEAN" in display_api
assert "HalDisplay" not in gfx
assert "DisplaySurface& display" in gfx
assert "HalDisplay : public DisplaySurface" in hal
assert "getSurfaceInfo() const override" in hal
assert "DisplaySafeInsets" in surface
assert "t5_ui_viewport_t" in ui
assert "get_viewport" in ui
assert "getViewport" in bridge
print("display abstraction contract: ok")
