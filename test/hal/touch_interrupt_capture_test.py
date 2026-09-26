#!/usr/bin/env python3
"""Contracts for provider-owned touch and firmware consumer migration."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HAL_H = (ROOT / "lib/hal/HalGPIO.h").read_text(encoding="utf-8")
HAL_CPP = (ROOT / "lib/hal/HalGPIO.cpp").read_text(encoding="utf-8")
MAPPED = (ROOT / "src/MappedInputManager.cpp").read_text(encoding="utf-8")
MAIN = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
TAKEOVER = (ROOT / "src/native/NativeHardwareTakeover.cpp").read_text(encoding="utf-8")
TOUCH = (ROOT / "src/native/NativeTouchInput.cpp").read_text(encoding="utf-8")
TOUCH_H = (ROOT / "src/native/NativeTouchInput.h").read_text(encoding="utf-8")
PROVIDER = (ROOT / "Drivers/gt911_touch/driver.c").read_text(encoding="utf-8")
T5_BOARD = (ROOT / "lib/Board_T5S3/BoardT5S3.cpp").read_text(encoding="utf-8")
T5_HEADER = (ROOT / "lib/Board_T5S3/BoardT5S3.h").read_text(encoding="utf-8")
EPD_BOARD = (ROOT / "lib/Board_EPD47/BoardEPD47.cpp").read_text(encoding="utf-8")
EPD_HEADER = (ROOT / "lib/Board_EPD47/BoardEPD47.h").read_text(encoding="utf-8")

# HalGPIO must no longer own, probe, poll, acknowledge, or spawn a worker for
# GT911. It retains only physical buttons, USB state and deep-sleep wake pins.
for forbidden in (
    "Board::GT911Touch", "touch.begin()", "touch.readEvent",
    "startTouchCapture", "suspendTouchCapture", "resumeTouchCapture",
    "touchTaskTrampoline", "touchInterruptThunk", "touchTapQueue",
):
    assert forbidden not in HAL_H
    assert forbidden not in HAL_CPP

# The installed provider is the only production component that knows GT911
# status/data registers and clears READY.
assert "GT911_STATUS_REG" in PROVIDER
assert "GT911_FIRST_POINT_REG" in PROVIDER
assert "write_reg8(GT911_STATUS_REG, 0u)" in PROVIDER

# Board code may establish electrical reset/wake state, but it must contain no
# alternate GT911 register reader or READY acknowledgement implementation.
for board, header in ((T5_BOARD, T5_HEADER), (EPD_BOARD, EPD_HEADER)):
    assert "GT911Touch" not in board
    assert "GT911Touch" not in header
    assert "GT911_STATUS_REG" not in board
    assert "GT911_POINT1_REG" not in board
assert "prepareTouchControllerForProvider();" in T5_BOARD
assert "digitalWrite(T5S3_TOUCH_RST, LOW);" in T5_BOARD
assert "digitalWrite(T5S3_TOUCH_INT, LOW);" in T5_BOARD
assert "digitalWrite(EPD47_TOUCH_INT, HIGH);" in EPD_BOARD

# Firmware acquires input.touch.raw and always has snapshot recovery when a
# bounded event cursor gaps or polling fails.
assert 'acquireCapability("input.touch.raw", RISC_TOUCH_API_V1' in TOUCH
assert "candidateApi->subscribe" in TOUCH
assert "candidateApi->poll" in TOUCH
assert "candidateApi->next" in TOUCH
assert "candidateApi->snapshot" in TOUCH
assert "if (result < 0)" in TOUCH
assert "resync();" in TOUCH
assert "gestureEligible = false;" in TOUCH
assert "nativeTouchGetTap" in TOUCH_H
assert "nativeTouchGetHold" in TOUCH_H
assert "nativeTouchGetSwipe" in TOUCH_H

# MappedInputManager now derives UI gestures from the provider consumer rather
# than HalGPIO. Startup activates it only after SD/provider storage is ready.
assert "nativeTouchTick();" in MAPPED
assert "nativeTouchGetTap" in MAPPED
assert "nativeTouchGetHold" in MAPPED
assert "nativeTouchGetSwipe" in MAPPED
assert "nativeTouchTakeHomePress" in MAPPED
for old in ("gpio.getTouchTap", "gpio.getTouchHold", "gpio.getTouchSwipe",
            "gpio.wasTouchHomeButtonPressed"):
    assert old not in MAPPED
assert "(void)nativeTouchResume();" in MAIN
assert "gpio.startTouchCapture()" not in MAIN
assert "gpio.isTouchAvailable()" not in MAIN

# Display takeover releases only the firmware consumer lease. This leaves no
# active provider for legacy direct-GT911 apps, while a migrated app can acquire
# input.touch.raw itself after takeover begins.
assert "nativeTouchSuspend()" in TAKEOVER
assert "nativeTouchResume()" in TAKEOVER
assert "gpio.suspendTouchCapture()" not in TAKEOVER
assert "gpio.resumeTouchCapture()" not in TAKEOVER

print("Provider-owned firmware touch migration contracts passed")
