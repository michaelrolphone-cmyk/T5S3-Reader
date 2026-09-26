#pragma once
#include <esp_err.h>
#include <string>
class GfxRenderer;
class MappedInputManager;
// Main-task only. Exclusively owns the framebuffer while native code runs.
esp_err_t runNativeApp(const char* sdPath, GfxRenderer& renderer, MappedInputManager& input);

// Consumed by main.cpp to start a fresh inactivity period on browser return.
bool consumeNativeAppReturn();

// Present a native UI bridge frame without blocking the app owner task during
// long e-paper pixel transfers.
bool presentNativeAppUiFrame();

// Install one exact application artifact from the authoritative online catalog.
// Intended for firmware-owned workflows that require a child app. If already
// installed, returns true without network I/O. On success displayName is the
// installed/catalog display name when available. Failure detail is suitable for
// a bounded system-UI status line.
bool installRequiredNativeApp(const char* artifact, std::string& displayName,
                              std::string& failureDetail);

// Home's Apps entry: the actual grid lives in /sd/Apps/springboard.elf.
// Returns true when a firmware settings dialog must finish before resuming.
bool runNativeSpringboard(GfxRenderer& renderer, MappedInputManager& input, bool resume = false);
