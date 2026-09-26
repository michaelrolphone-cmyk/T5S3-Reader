#!/usr/bin/env python3
"""Contracts for interrupt-backed GT911 capture independent of the UI loop."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HAL_H = (ROOT / "lib/hal/HalGPIO.h").read_text(encoding="utf-8")
HAL_CPP = (ROOT / "lib/hal/HalGPIO.cpp").read_text(encoding="utf-8")
T5_H = (ROOT / "lib/Board_T5S3/BoardT5S3.h").read_text(encoding="utf-8")
T5_CPP = (ROOT / "lib/Board_T5S3/BoardT5S3.cpp").read_text(encoding="utf-8")
EPD_H = (ROOT / "lib/Board_EPD47/BoardEPD47.h").read_text(encoding="utf-8")
EPD_CPP = (ROOT / "lib/Board_EPD47/BoardEPD47.cpp").read_text(encoding="utf-8")
THEME = (ROOT / "src/components/themes/BaseTheme.cpp").read_text(encoding="utf-8")

assert "attachInterruptArg(BoardPins::TouchInterrupt" in HAL_CPP
assert 'xTaskCreate(touchTaskTrampoline, "touch-input"' in HAL_CPP
assert "vTaskNotifyGiveFromISR" in HAL_CPP
assert "touch.readEvent(&point, &homeButtonPressed, &contactActive)" in HAL_CPP
assert "xQueueSend(touchTapQueue" in HAL_CPP
assert "xQueueReceive(touchTapQueue" in HAL_CPP
assert "xQueueSend(touchSwipeQueue" in HAL_CPP
assert "xQueueReceive(touchSwipeQueue" in HAL_CPP
assert "xQueueSend(touchHomeQueue" in HAL_CPP
assert "xQueueReceive(touchHomeQueue" in HAL_CPP

# The UI loop must not poll or clear GT911 state. It only consumes queued events.
get_state = HAL_CPP[HAL_CPP.index("uint8_t HalGPIO::getState()"):
                    HAL_CPP.index("void HalGPIO::update()")]
update = HAL_CPP[HAL_CPP.index("void HalGPIO::update()"):
                 HAL_CPP.index("bool HalGPIO::wasUsbStateChanged")]
assert "touch." not in get_state
assert "touch." not in update
for stale in ("touchTapEvent", "touchSwipeEvent", "touchHomeButtonEvent",
              "TOUCH_RELEASE_GRACE_MS", "readTouchState"):
    assert stale not in HAL_CPP

# Both board drivers must expose an acknowledged READY event even when contact
# count is zero, so release is captured by the interrupt worker rather than
# inferred later by UI-loop polling.
for header in (T5_H, EPD_H):
    assert "bool readEvent(TouchPoint* point, bool* homeButtonPressed, bool* contactActive);" in header
for source in (T5_CPP, EPD_CPP):
    start = source.index("bool GT911Touch::readEvent(")
    end = source.index("bool GT911Touch::readPoint(", start)
    event = source[start:end]
    assert "GT911_STATUS_READY" in event
    assert "touchCount == 0" in event or "GT911_TOUCH_COUNT_MASK) == 0" in event
    assert "writeReg8(GT911_STATUS_REG, 0);" in event
    assert "return true;" in event

# Preserve existing gesture/hit-target semantics; this change is capture
# architecture, not a swipe threshold or shared-nav geometry tweak.
assert "constexpr uint16_t TOUCH_SWIPE_THRESHOLD = 25;" in HAL_CPP
assert "constexpr int buttonHeight = BaseMetrics::values.buttonHintsHeight;" in THEME
assert "touchPadTop" not in THEME

print("Interrupt-backed touch capture contracts passed")
