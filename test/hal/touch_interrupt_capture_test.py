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
T5_DISPLAY = (ROOT / "lib/hal/HalDisplay.cpp").read_text(encoding="utf-8")

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

# The render task and input owner loop share the physical I2C controller on
# T5S3. Panel power sequencing must never monopolize that bus across its waits:
# each transaction locks independently, while PCA9535 bit updates retain an
# atomic read-modify-write critical section.
# The independent chip owner now serializes RMW and grants pins. Board
# consumers must not bypass it through a second raw PCA transaction path.
assert "updatePca9535Bit" not in T5_BOARD
assert "Wire.beginTransmission(T5S3_PCA9535_ADDR)" not in T5_BOARD
assert "expander->write(expander->context, grant, mask" in T5_BOARD
assert "expander->read(expander->context, grant" in T5_BOARD

prepare_power = T5_DISPLAY.split("bool preparePowerPins()", 1)[1].split(
    "bool powerOnSequence()", 1)[0]
power_on = T5_DISPLAY.split("bool powerOnSequence()", 1)[1].split(
    "void powerOffSequence()", 1)[0]
power_off = T5_DISPLAY.split("void powerOffSequence()", 1)[1].split(
    "uint8_t grayscaleValueForBit", 1)[0]
for sequence in (prepare_power, power_on, power_off):
    assert "Board::ScopedI2CLock busLock;" not in sequence

tps_write = T5_DISPLAY.split("bool writeTpsRegister(", 1)[1].split(
    "bool writeTpsRegister8", 1)[0]
tps_read = T5_DISPLAY.split("bool readTpsRegister(", 1)[1].split(
    "bool waitForPcaPinHigh", 1)[0]
assert "Board::ScopedI2CLock lock;" in tps_write
assert "Board::ScopedI2CLock lock;" in tps_read
assert "delay(1);" in power_on
assert "delay(1);" in power_off

# Firmware acquires input.touch.raw and continuously services that opaque
# provider on a dedicated capture task. UI/render cadence must never be the
# sampler: nativeTouchTick only observes queued activity, while the worker
# polls/drains the provider at a bounded cadence and snapshot-recovers gaps.
assert 'acquireCapability("input.touch.raw", RISC_TOUCH_API_V1' in TOUCH
assert "candidateApi->subscribe" in TOUCH
assert "candidateApi->poll" in TOUCH
assert "candidateApi->next" in TOUCH
assert "candidateApi->snapshot" in TOUCH
assert 'xTaskCreate(touchWorker, "touch-provider"' in TOUCH
assert "kCaptureIntervalMs = 5" in TOUCH
assert "serviceProvider();" in TOUCH
assert "ulTaskNotifyTake(pdTRUE," in TOUCH
assert "pdMS_TO_TICKS(kCaptureIntervalMs) : 1" in TOUCH
assert "if (result < 0)" in TOUCH
assert "resync(false)" in TOUCH
assert "gestureEligible = false;" in TOUCH
tick_body = TOUCH.split("void nativeTouchTick()", 1)[1].split(
    "bool nativeTouchSuspend()", 1)[0]
assert "api->poll" not in tick_body
assert "api->next" not in tick_body
suspend_body = TOUCH.split("bool nativeTouchSuspend()", 1)[1].split(
    "bool nativeTouchResume()", 1)[0]
assert suspend_body.index("stopWorker()") < suspend_body.index("api->unsubscribe")
assert "nativeTouchGetTap" in TOUCH_H
assert "nativeTouchGetHold" in TOUCH_H
assert "nativeTouchGetSwipe" in TOUCH_H

# MappedInputManager derives UI gestures from the provider consumer rather
# than HalGPIO. Provider activation is deferred to the normal input loop so
# navigation gets first access to the provider graph when touch is absent.
assert "nativeTouchTick();" in MAPPED
assert "nativeTouchGetTap" in MAPPED
assert "nativeTouchGetHold" in MAPPED
assert "nativeTouchGetSwipe" in MAPPED
assert "nativeTouchTakeHomePress" in MAPPED
for old in ("gpio.getTouchTap", "gpio.getTouchHold", "gpio.getTouchSwipe",
            "gpio.wasTouchHomeButtonPressed"):
    assert old not in MAPPED
# This assertion constrains graphical boot; do not accidentally inspect the
# separate headless setup and weaken the no-touch-startup contract.
graphical_main = MAIN.split("#else\n", 1)[1] if MAIN.startswith("#if defined(RISCRTE_PROFILE_HEADLESS)\n") else MAIN
setup_start = graphical_main.index("void setup()")
loop_start = graphical_main.index("void loop()", setup_start)
setup = graphical_main[setup_start:loop_start]
assert "(void)nativeTouchResume();" not in setup
update_start = MAPPED.index("void MappedInputManager::update() const")
update_end = MAPPED.index("bool MappedInputManager::wasAnyPressed()", update_start)
update = MAPPED[update_start:update_end]
assert update.index("nativeNavigationTick();") < update.index("nativeTouchTick();")
assert "gpio.startTouchCapture()" not in MAIN
assert "gpio.isTouchAvailable()" not in MAIN
assert 'versionInInstalledSnapshot(snapshot, "i2c.bus")' in MAIN
assert 'versionInInstalledSnapshot(snapshot, "platform.clock")' in MAIN
assert 'versionInInstalledSnapshot(snapshot, "input.touch.raw")' in MAIN
assert 'versionInInstalledSnapshot(snapshot, "input.navigation")' in MAIN
assert "Foundational platform drivers incomplete" in MAIN

# Auto-sleep must quiesce touch before navigation attempts graph-wide shutdown.
sleep_helper = MAIN.split("bool suspendInputProvidersForSleep()", 1)[1].split(
    "void resumeInputProvidersAfterSleep()", 1)[0]
assert sleep_helper.index("nativeTouchSuspend()") < sleep_helper.index("nativeNavigationSuspend()")
resume_helper = MAIN.split("void resumeInputProvidersAfterSleep()", 1)[1].split(
    "// Enter deep sleep mode", 1)[0]
assert resume_helper.index("nativeNavigationResume()") < resume_helper.index("nativeTouchResume()")
assert "if (!suspendInputProvidersForSleep()) return;" in MAIN

# Pending UI gesture delivery must not keep resetting the inactivity timer.
activity = TOUCH.split("bool nativeTouchHadActivity()", 1)[1].split("\n}", 1)[0]
assert "touchActive || activityThisTick" in activity
for sticky in ("tapCount", "swipeCount", "homeCount"):
    assert sticky not in activity
tick = TOUCH.split("void nativeTouchTick()", 1)[1].split(
    "bool nativeTouchSuspend()", 1)[0]
assert "activityThisTick = false;" in tick
assert "activitySerial != observedActivitySerial" in tick
assert "activityThisTick = true;" not in tick

# Snapshot recovery after a transient poll/I2C failure must invalidate only the
# in-flight gesture; already completed tap/swipe/home queues remain deliverable.
resync_body = TOUCH.split("bool resync(bool clearQueues)", 1)[1].split(
    "void process(", 1)[0]
assert "clearTransient(clearQueues)" in resync_body
service_body = TOUCH.split("void serviceProvider()", 1)[1].split(
    "bool workerShouldRun()", 1)[0]
assert "kPollFailureTimeoutMs" in TOUCH
assert "bool pollOk = api->poll(api->context, 1u);" in service_body
assert service_body.index("api->next") < service_body.index("if (pollOk)")
assert "clearTransient(false)" in service_body
assert "resync(false)" in service_body

# Display takeover releases only the firmware consumer lease. This leaves no
# active provider for legacy direct-GT911 apps, while a migrated app can acquire
# input.touch.raw itself after takeover begins.
assert "nativeTouchSuspend()" in TAKEOVER
assert "nativeTouchResume()" in TAKEOVER
assert "gpio.suspendTouchCapture()" not in TAKEOVER
assert "gpio.resumeTouchCapture()" not in TAKEOVER

print("Provider-owned firmware touch migration contracts passed")
