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


# Blank-screen regression guards: display abstraction failure must fail visibly
# and must not erase the retained e-paper image before metadata validation.
gfx_cpp = (root / "lib/GfxRenderer/GfxRenderer.cpp").read_text()
gfx_h = (root / "lib/GfxRenderer/GfxRenderer.h").read_text()
main_cpp = (root / "src/main.cpp").read_text()
epd47_cpp = (root / "lib/hal/HalDisplayEPD47.cpp").read_text()

assert "bool begin();" in gfx_h
begin_body = gfx_cpp[gfx_cpp.index("bool GfxRenderer::begin()"):gfx_cpp.index("void GfxRenderer::insertFont")]
assert "assert(false)" not in begin_body
assert "display.isReady()" in begin_body
assert "validateDisplaySurfaceInfo(candidateInfo)" in begin_body
assert "candidateFrameBuffer" in begin_body
assert "Commit only after all validation succeeds" in begin_body

assert "display.begin(false);" in main_cpp
assert "if (!display.isReady())" in main_cpp
assert "if (!renderer.begin())" in main_cpp
assert "showEmergencyFailurePattern(0xD1)" in main_cpp
assert "g_displayBootFailed" in main_cpp
assert "if (g_displayBootFailed)" in main_cpp
assert "Do not touch ActivityManager/renderer after failed display bootstrap" in main_cpp

# EPD47 may preserve the physical image, but its RAM surface must never remain
# uninitialized merely because clearPanel=false.
assert "(void)clearPanel;" in epd47_cpp
assert "clearScreen(0xFF);" in epd47_cpp

print("display blank-screen fail-safe contract: ok")


# Public framebuffer/presentation entry points must fail closed before a
# successful transactional begin(). This prevents an accidental early clear or
# present from destroying the retained diagnostic image.
assert "if (!initialized || !frameBuffer) return;" in gfx_cpp
assert 'Refusing displayBuffer before a validated, ready display surface' in gfx_cpp
assert "if (!initialized || !frameBuffer || !display.isReady())" in gfx_cpp
print("display pre-init mutation guards: ok")
