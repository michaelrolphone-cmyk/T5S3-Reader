#!/usr/bin/env python3
"""Contracts for interrupt-backed GT911 capture independent of the UI loop."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HAL_H = (ROOT / "lib/hal/HalGPIO.h").read_text(encoding="utf-8")
HAL_CPP = (ROOT / "lib/hal/HalGPIO.cpp").read_text(encoding="utf-8")
MAIN = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
T5_H = (ROOT / "lib/Board_T5S3/BoardT5S3.h").read_text(encoding="utf-8")
T5_CPP = (ROOT / "lib/Board_T5S3/BoardT5S3.cpp").read_text(encoding="utf-8")
EPD_H = (ROOT / "lib/Board_EPD47/BoardEPD47.h").read_text(encoding="utf-8")
EPD_CPP = (ROOT / "lib/Board_EPD47/BoardEPD47.cpp").read_text(encoding="utf-8")
THEME = (ROOT / "src/components/themes/BaseTheme.cpp").read_text(encoding="utf-8")
T5_BOARD = (ROOT / "lib/Board_T5S3/BoardT5S3.cpp").read_text(encoding="utf-8")

assert "attachInterruptArg(BoardPins::TouchInterrupt" in HAL_CPP
assert "touchInterruptThunk, this, CHANGE" in HAL_CPP
assert 'xTaskCreate(touchTaskTrampoline, "touch-input"' in HAL_CPP

# Early boot only probes the controller. The worker/ISR are armed after
# SD/settings/RTC/display initialization, so boot-critical setup stays
# single-threaded.
begin_start = HAL_CPP.index("void HalGPIO::begin()")
capture_start = HAL_CPP.index("void HalGPIO::startTouchCapture()")
begin = HAL_CPP[begin_start:capture_start]
capture = HAL_CPP[capture_start:HAL_CPP.index("void IRAM_ATTR HalGPIO::touchInterruptThunk")]
assert "xTaskCreate" not in begin
assert "attachInterruptArg" not in begin
assert "xTaskCreate" in capture
assert "attachInterruptArg" in capture
display_setup = MAIN.index("setupDisplayAndFonts();")
touch_start = MAIN.index("gpio.startTouchCapture();")
assert display_setup < touch_start
assert "vTaskNotifyGiveFromISR" in HAL_CPP
assert "pdMS_TO_TICKS(20)" in HAL_CPP
assert "touch.begin()" in HAL_CPP[HAL_CPP.index("void HalGPIO::startTouchCapture()"):
                                  HAL_CPP.index("void IRAM_ATTR HalGPIO::touchInterruptThunk")]
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



# Keep slow board telemetry out of the per-frame input hot path.
assert "static constexpr unsigned long USB_STATE_POLL_MS = 250;" in HAL_H
update_start = HAL_CPP.index("void HalGPIO::update()")
update_end = HAL_CPP.index("bool HalGPIO::wasUsbStateChanged", update_start)
update = HAL_CPP[update_start:update_end]
assert "currentTime - lastUsbPollTime" in update
assert update.count("isUsbConnected()") == 1

begin_start = T5_BOARD.index("void begin()")
begin_end = T5_BOARD.index("\nvoid deinitForSleep()", begin_start)
board_begin = T5_BOARD[begin_start:begin_end]
button_start = T5_BOARD.index("bool readButton()")
button_end = T5_BOARD.index("\nbool readBQ27220Reg16", button_start)
button_read = T5_BOARD[button_start:button_end]
assert "setPca9535PinMode(PCA9535_IO12_BUTTON, INPUT)" in board_begin
assert "setPca9535PinMode" not in button_read

print("Interrupt-backed touch capture contracts passed")
