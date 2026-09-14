#pragma once
#include <esp_err.h>
class GfxRenderer;
class MappedInputManager;
// Main-task only. Exclusively owns the framebuffer while native code runs.
esp_err_t runNativeApp(const char* sdPath, GfxRenderer& renderer, MappedInputManager& input);

// Returns the absolute /sd/... path for the ELF that currently owns the native
// session, or nullptr outside that owning task. Used by reusable system-UI
// services to relaunch the same app after firmware activities such as keyboard.
const char* currentNativeAppPath();

// Consumed by main.cpp to start a fresh inactivity period on browser return.
bool consumeNativeAppReturn();
