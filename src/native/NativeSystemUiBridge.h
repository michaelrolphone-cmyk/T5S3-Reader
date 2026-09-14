#pragma once

#include <T5SystemUiApi.h>

// Implementation unit for firmware-owned reusable UI services exposed to ELF
// applications. The exported symbol is declared in T5SystemUiApi.h.

// Navigation queued by the current ELF must run before the Apps launcher resumes.
enum class NativeSystemUiNavigation { None, Keyboard, Home };
void nativeSystemUiBegin();
NativeSystemUiNavigation nativeSystemUiTakeNavigation();
