#pragma once

#include <T5SystemUiApi.h>

// Implementation unit for firmware-owned reusable UI services exposed to ELF
// applications. The exported symbol is declared in T5SystemUiApi.h.

// Keyboard is the existing generic "system activity pending" handoff used by
// runNativeApp. Wi-Fi selection uses the same handoff path.
enum class NativeSystemUiNavigation { None, Keyboard, Home };
void nativeSystemUiBegin();
NativeSystemUiNavigation nativeSystemUiTakeNavigation();
