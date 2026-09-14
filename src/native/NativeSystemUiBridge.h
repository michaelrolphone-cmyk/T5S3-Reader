#pragma once

#include <T5SystemUiApi.h>

// Implementation unit for firmware-owned reusable UI services exposed to ELF
// applications. The exported symbol is declared in T5SystemUiApi.h.

// Keyboard is the existing generic "system activity pending" handoff used by
// runNativeApp. Wi-Fi selection aliases that same state so the host resumes the
// Apps launcher only after either firmware-owned child activity completes.
enum class NativeSystemUiNavigation { None, Keyboard, Wifi = Keyboard, Home };
void nativeSystemUiBegin();
NativeSystemUiNavigation nativeSystemUiTakeNavigation();
