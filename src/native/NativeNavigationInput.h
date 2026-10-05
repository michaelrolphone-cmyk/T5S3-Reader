#pragma once
#include <RiscInputNavigationV1.h>

// Firmware UI consumer of a generic installed input.navigation provider.
// All lifecycle/tick functions are invocation-owner-task only.
void nativeNavigationTick();
void nativeNavigationConfigure(bool enabled);
const risc_input_navigation_frame_v1& nativeNavigationFrame();
unsigned long nativeNavigationHeldMs();
// Optional trait of the current live provider, never inferred from board type.
bool nativeNavigationHasPhysicalPagePair();
bool nativeNavigationClaim(uint32_t token, const char* capability, uint32_t version);
void nativeNavigationRelease(uint32_t token);
void nativeNavigationBoundary();
bool nativeNavigationSuspend();
void nativeNavigationResume();
void nativeNavigationRetry();
// Borrow a verified boot provider whose module owner outlives this consumer.
// Used before removable storage can host the normal installed provider graph.
bool nativeNavigationAttachBootstrap(const risc_input_navigation_api_v1* candidate);
