#!/usr/bin/env python3
"""Contracts for firmware home shortcuts while a normal ELF owns the foreground."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HOST = (ROOT / "src/native/NativeAppHost.cpp").read_text(encoding="utf-8")
MENU = (ROOT / "src/activities/GlobalMenuActivity.cpp").read_text(encoding="utf-8")
MENU_H = (ROOT / "src/activities/GlobalMenuActivity.h").read_text(encoding="utf-8")
TAKEOVER = (ROOT / "src/native/NativeHardwareTakeover.cpp").read_text(encoding="utf-8")
HAL_H = (ROOT / "lib/hal/HalGPIO.h").read_text(encoding="utf-8")
HAL_CPP = (ROOT / "lib/hal/HalGPIO.cpp").read_text(encoding="utf-8")
MAPPED_H = (ROOT / "src/MappedInputManager.h").read_text(encoding="utf-8")
MAPPED_CPP = (ROOT / "src/MappedInputManager.cpp").read_text(encoding="utf-8")

poll_start = HOST.index("bool poll(t5_app_input_t* out, uint32_t waitMs)")
poll_end = HOST.index("\nuint32_t clockMs()", poll_start)
poll = HOST[poll_start:poll_end]

assert "takeTouchHomeButtonPress(homeEventMs)" in poll
assert poll.count("takeTouchHomeButtonPress(homeEventMs)") == 1
assert "wasTouchHomeButtonPressed()" not in poll
assert "SETTINGS.doubleClickHomeMenu && !nativeHardwareTakeoverDisplayActive()" in poll
assert "GlobalMenuActivity::runFirmwareModal(s->renderer, s->input)" in poll
assert "elapsed <= kNativeHomeDoubleClickWindowMs" in poll
assert "millis() - s->lastHomeEventMs" in poll
assert "s->exiting = true;" in poll
assert "homeRequested = true;" in poll
assert "ModalResult::ShutdownRequested" in poll

assert "bool takeTouchHomeButtonPress(unsigned long& eventMs) const;" in HAL_H
assert "sizeof(unsigned long)" in HAL_CPP
assert "xQueueSend(touchHomeQueue, &eventMs, 0)" in HAL_CPP
assert "bool MappedInputManager::takeTouchHomeButtonPress(unsigned long& eventMs) const" in MAPPED_CPP
assert "bool takeTouchHomeButtonPress(unsigned long& eventMs) const;" in MAPPED_H

assert "static ModalResult runFirmwareModal" in MENU_H
assert "RuntimeMemory::PsramBuffer snapshot(renderer.getBufferSize(), false)" in MENU
assert "std::memcpy(snapshot.data(), renderer.getFrameBuffer(), renderer.getBufferSize())" in MENU
assert "std::memcpy(renderer.getFrameBuffer(), snapshot.data(), renderer.getBufferSize())" in MENU
assert "modalShutdownConfirmed(renderer, mappedInput)" in MENU
assert "requestShutdown();" in MENU

assert "bool nativeHardwareTakeoverDisplayActive()" in TAKEOVER
assert "return s_display_borrowed;" in TAKEOVER

print("Firmware home shortcut overlay contracts passed")
