#pragma once
#include <esp_err.h>
class GfxRenderer;
class MappedInputManager;
// Main-task only. Exclusively owns the framebuffer while native code runs.
esp_err_t runNativeApp(const char* sdPath, GfxRenderer& renderer, MappedInputManager& input);

// Consumed by main.cpp to start a fresh inactivity period on browser return.
bool consumeNativeAppReturn();

// Home's Apps entry: the actual grid lives in /sd/Apps/springboard.elf.
// Returns true when a firmware settings dialog must finish before resuming.
bool runNativeSpringboard(GfxRenderer& renderer, MappedInputManager& input, bool resume = false);
