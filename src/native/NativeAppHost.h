#pragma once
#include <esp_err.h>
class GfxRenderer;
class MappedInputManager;
// Main-task only. Exclusively owns the framebuffer while native code runs.
esp_err_t runNativeApp(const char* sdPath, GfxRenderer& renderer, MappedInputManager& input);

// Consumed by main.cpp to start a fresh inactivity period on browser return.
bool consumeNativeAppReturn();
