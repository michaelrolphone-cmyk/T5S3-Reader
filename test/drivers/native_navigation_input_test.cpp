#include <cassert>
#include <cstdio>
#include <HalStorage.h>
#include "../../src/native/NativeNavigationInput.cpp"

uint32_t fakeTime = 1000;
bool storageReady = true, closeOk = true;
std::map<std::string, std::shared_ptr<TestFile>> files;
HalStorage Storage;
static bool available, shutdownOkay = true, sharedGrant = false;
static unsigned acquisitions, releases, shutdowns, polls, resets;
static size_t foregroundCount;
static bool fakePoll(void*, risc_input_navigation_frame_v1* out) {
    ++polls;
    *out = {RISC_NAV_CONFIRM, RISC_NAV_CONFIRM, 0};
    return true;
}
static bool fakeForeground(void*, const risc_input_foreground_v1*, size_t count) {
    foregroundCount = count; return true;
}
static bool fakeReset(void*) { ++resets; return true; }
static const risc_input_navigation_api_v1 navigation{
    1, sizeof(navigation), nullptr, fakePoll, fakeForeground, fakeReset};
namespace RuntimeInstalledProviders {
bool acquireCapability(const char* capability, uint32_t version, Lease* out) {
    assert(!std::strcmp(capability, "input.navigation") && version == 1);
    ++acquisitions;
    if (!available) return false;
    *out = {{1, 1}, &navigation}; return true;
}
bool release(Lease* out) { ++releases; *out = {}; return true; }
bool shutdown() { ++shutdowns; return shutdownOkay; }
bool hasLiveGrants() { return sharedGrant; }
const char* lastError() { return "fixture unavailable"; }
}
int main() {
    nativeNavigationTick();
    for (unsigned i = 0; i < 100; ++i) { fakeTime += 20; nativeNavigationTick(); }
    assert(acquisitions == 1); // absent provider never causes per-frame SD scans
    shutdownOkay = false;
    assert(!nativeNavigationSuspend()); // failed start may retain a lower module without an API
    shutdownOkay = true;
    assert(nativeNavigationSuspend());
    nativeNavigationResume();
    assert(nativeNavigationClaim(1, "test.input", 1));
    available = true; nativeNavigationRetry(); nativeNavigationTick();
    assert(acquisitions == 2 && foregroundCount == 1 && polls == 1);
    nativeNavigationTick(); // same-time tick cannot replay an edge
    assert(!nativeNavigationFrame().pressed && polls == 1);
    nativeNavigationRelease(1);
    assert(foregroundCount == 0 && !nativeNavigationFrame().buttons);
    assert(acquisitions == 2 && releases == 0); // focus transfer keeps host lease
    nativeNavigationBoundary(); assert(resets >= 2);
    assert(nativeNavigationSuspend() && releases == 1 && shutdowns == 3);
    nativeNavigationTick(); assert(acquisitions == 2);
    nativeNavigationResume(); fakeTime += 20; nativeNavigationTick();
    assert(acquisitions == 3);
    sharedGrant = true;
    nativeNavigationConfigure(false);
    assert(releases == 2 && shutdowns == 3); // app keeps its independent grant
    nativeNavigationResume(); nativeNavigationTick();
    assert(acquisitions == 3); // persisted Off survives wake
    sharedGrant = false;
    nativeNavigationConfigure(true); fakeTime += 20; nativeNavigationTick();
    assert(acquisitions == 4);
    shutdownOkay = false;
    assert(!nativeNavigationSuspend()); // lower-provider failure blocks sleep
    nativeNavigationResume(); nativeNavigationTick();
    assert(acquisitions == 4); // quarantined resources never silently restarted
    puts("Firmware navigation acquisition, focus, polling and sleep lifetime: PASS");
}
