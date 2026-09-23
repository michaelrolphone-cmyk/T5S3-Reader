#pragma once
#include <RiscInputNavigationV1.h>

// Firmware UI consumer of a generic installed input.navigation provider.
// All lifecycle/tick functions are invocation-owner-task only.
void nativeNavigationTick();
const risc_input_navigation_frame_v1& nativeNavigationFrame();
unsigned long nativeNavigationHeldMs();
bool nativeNavigationClaim(uint32_t token, const char* capability, uint32_t version);
void nativeNavigationRelease(uint32_t token);
void nativeNavigationBoundary();
bool nativeNavigationSuspend();
void nativeNavigationResume();
void nativeNavigationRetry();
