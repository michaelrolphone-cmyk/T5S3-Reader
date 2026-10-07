#!/usr/bin/env python3
from pathlib import Path
import runpy

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
hal_cpp = (root / "lib/hal/HalDisplay.cpp").read_text()

assert "bool begin();" in gfx_h
begin_body = gfx_cpp[gfx_cpp.index("bool GfxRenderer::begin()"):gfx_cpp.index("void GfxRenderer::insertFont")]
assert "assert(false)" not in begin_body
assert "display.isReady()" in begin_body
assert "validateDisplaySurfaceInfo(candidateInfo)" in begin_body
assert "candidateFrameBuffer" in begin_body
assert "Commit only after all validation succeeds" in begin_body

assert "bool preflightSurface() const;" in gfx_h
assert "bool GfxRenderer::preflightSurface() const" in gfx_cpp
setup_start = main_cpp.index("bool setupDisplayAndFonts()")
setup_end = main_cpp.index("void ensureSdFontLoaded()", setup_start)
setup_body = main_cpp[setup_start:setup_end]
assert setup_body.index("renderer.preflightSurface()") < setup_body.index("display.begin(false)")
assert setup_body.index("display.begin(false)") < setup_body.index("renderer.begin()")
assert "panel backend was not touched" in setup_body

assert "validateDisplaySurfaceInfo(SURFACE_INFO)" in hal
assert "VISIBLE_WIDTH == BoardPins::LogicalWidth" in hal
assert "DISPLAY_WIDTH == ((BoardPins::DisplayWidth + 15) / 16) * 16" in hal
assert 'LOG_ERR("DSP", "Present rejected:' in hal_cpp
assert 'LOG_ERR("DSP", "Present rejected:' in epd47_cpp

resume_start = hal_cpp.index("bool HalDisplay::resumeFromExternalOwner()")
resume_end = hal_cpp.index("void HalDisplay::begin(", resume_start)
resume_body = hal_cpp[resume_start:resume_end]
assert "begin(false);" in resume_body
assert "begin();" not in resume_body
assert "retained panel image preserved" in resume_body

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


# Metadata preflight runs before hardware init and therefore must remain
# side-effect free: no readiness check, framebuffer access, clear, present, or
# other backend operation is allowed in this function.
preflight_start = gfx_cpp.index("bool GfxRenderer::preflightSurface() const")
preflight_end = gfx_cpp.index("bool GfxRenderer::begin()", preflight_start)
preflight_body = gfx_cpp[preflight_start:preflight_end]
assert "display.getSurfaceInfo()" in preflight_body
for forbidden in (
    "display.isReady()",
    "display.getFrameBuffer()",
    "display.clearScreen(",
    "display.displayBuffer(",
    "display.drawImage(",
    "display.requestNext",
    "display.copyGrayscale",
):
    assert forbidden not in preflight_body, forbidden

# Raster operations require validated initialization and valid target memory.
# Backend delegation/presentation additionally requires display.isReady().
# RAM clearing must remain available during UI-video takeover, when the
# physical backend is suspended but the renderer's allocation remains valid.
for function_name in (
    "drawImage", "drawIcon", "clearScreen", "displayBuffer",
    "copyGrayscaleLsbBuffers", "copyGrayscaleMsbBuffers",
    "captureGrayscaleBaseBuffer", "displayGrayBuffer",
    "cleanupGrayscaleWithFrameBuffer",
):
    marker = f"GfxRenderer::{function_name}"
    start = gfx_cpp.index(marker)
    next_fn = gfx_cpp.find("\nvoid GfxRenderer::", start + len(marker))
    next_bool = gfx_cpp.find("\nbool GfxRenderer::", start + len(marker))
    candidates = [p for p in (next_fn, next_bool) if p != -1]
    end = min(candidates) if candidates else len(gfx_cpp)
    body = gfx_cpp[start:end]
    assert "initialized" in body, function_name

assert "if (!initialized || !display.isReady()) return;" in gfx_cpp
assert "Display preflight accepted:" in gfx_cpp
print("display preflight side-effect and delegation guards: ok")


# The retained-image boot path is a hard safety boundary. A future refactor
# must not turn begin(false) into a physical clear/present before renderer
# validation has completed.
t5_begin_start = hal_cpp.index("void HalDisplay::begin(const bool clearPanel)")
t5_begin_end = hal_cpp.index("void HalDisplay::clearScreen", t5_begin_start)
t5_begin_body = hal_cpp[t5_begin_start:t5_begin_end]
assert "clearPanel ? gfx->init() : gfx->initPreservingPanel()" in t5_begin_body
assert "displayBuffer(" not in t5_begin_body
assert "pushSprite(" not in t5_begin_body

epd_begin_start = epd47_cpp.index("void HalDisplay::begin(const bool clearPanel)")
epd_begin_end = epd47_cpp.index("void HalDisplay::clearScreen", epd_begin_start)
epd_begin_body = epd47_cpp[epd_begin_start:epd_begin_end]
assert "displayReady = false;" in epd_begin_body
assert "epd_clear(" not in epd_begin_body
assert "epd_clear_area" not in epd_begin_body
assert "epd_draw_" not in epd_begin_body
assert "displayBuffer(" not in epd_begin_body

assert "display.begin(false);" in setup_body
assert "display.begin();" not in setup_body
print("display retained-image initialization contract: ok")


# Current compiled-in HalDisplay is qualified only for the shipped 4.7-inch
# e-paper boards. Changing this geometry under the compatibility backend must
# be a compile break; new panels belong behind display.output.
for invariant in (
    "static_assert(DISPLAY_WIDTH == 960u && DISPLAY_HEIGHT == 540u",
    "static_assert(VISIBLE_WIDTH == 540u && VISIBLE_HEIGHT == 960u",
    "static_assert(DISPLAY_WIDTH_BYTES == 120u",
    "static_assert(BUFFER_SIZE == 64800u",
    "SAFE_INSETS.top == 9u",
):
    assert invariant in hal, invariant

assert "They are NOT the provider-ABI presentation intent ordinals" in surface
print("display board-profile and ordinal tripwires: ok")

# Compile and execute the production methods: declarations alone cannot catch
# a software clear accidentally gated on physical display ownership.
runpy.run_path(str(root / "test/native_apps/springboard_takeover_clear_test.py"), run_name="__main__")
