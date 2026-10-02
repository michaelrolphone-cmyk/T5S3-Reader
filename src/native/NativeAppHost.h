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
// Intended for firmware-owned workflows that require a child app. Normally an
// already-installed verified app returns true without network I/O. Callers may
// force a catalog install when a workflow requires newer manifest semantics.
// On success displayName is the installed/catalog display name when available.
// Failure detail is suitable for a bounded system-UI status line.
bool installRequiredNativeApp(const char* artifact, std::string& displayName,
                              std::string& failureDetail,
                              bool forceCatalogInstall = false);

// Home's Apps entry: resolve Springboard from the verified managed package
// inventory first, then fall back to the legacy loose /sd/Apps/springboard.elf pair.
// Returns true when a firmware settings dialog must finish before resuming.
bool runNativeSpringboard(GfxRenderer& renderer, MappedInputManager& input, bool resume = false);
