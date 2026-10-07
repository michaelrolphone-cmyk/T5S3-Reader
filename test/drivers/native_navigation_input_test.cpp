#include <cassert>
#include <cstdio>
#include <HalStorage.h>
#include "../../src/native/NativeNavigationInput.cpp"

uint32_t fakeTime = 1000;
bool storageReady = true, closeOk = true;
std::map<std::string, std::shared_ptr<TestFile>> files;
HalStorage Storage;
static bool available, shutdownOkay = true, sharedGrant = false, releaseOkay = true;
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
bool release(Lease* out) {
    ++releases; assert(out->grant.slot == 1);
    if (!releaseOkay) return false;
    *out = {}; return true;
}
bool shutdown() { ++shutdowns; return shutdownOkay; }
bool hasLiveGrants() { return sharedGrant; }
const char* lastError() { return "fixture unavailable"; }
}
int main() {
    nativeNavigationTick();
    assert(acquisitions == 1);

    // A provider that was not ready during the first UI frame must be retried
    // later without requiring a USB power transition. Retry is bounded so an
    // absent provider cannot cause a per-frame SD inventory scan.
    for (unsigned i = 0; i < 49; ++i) { fakeTime += 20; nativeNavigationTick(); }
    assert(acquisitions == 1);
    fakeTime += 20; nativeNavigationTick();
    assert(acquisitions == 2);

    shutdownOkay = false;
    assert(!nativeNavigationSuspend()); // failed start may retain a lower module without an API
    shutdownOkay = true;
    assert(nativeNavigationSuspend());
    nativeNavigationResume();
    assert(nativeNavigationClaim(1, "test.input", 1));
    available = true; nativeNavigationRetry(); nativeNavigationTick();
    assert(acquisitions == 3 && foregroundCount == 1 && polls == 1);
    nativeNavigationTick(); // same-time tick cannot replay an edge
    assert(!nativeNavigationFrame().pressed && polls == 1);
    nativeNavigationRelease(1);
    assert(foregroundCount == 0 && !nativeNavigationFrame().buttons);
    assert(acquisitions == 3 && releases == 0); // focus transfer keeps host lease
    nativeNavigationBoundary(); assert(resets >= 2);
    assert(nativeNavigationSuspend() && releases == 1 && shutdowns == 3);
    nativeNavigationTick(); assert(acquisitions == 3);
    nativeNavigationResume(); fakeTime += 20; nativeNavigationTick();
    assert(acquisitions == 4);
    sharedGrant = true;
    nativeNavigationConfigure(false);
    assert(releases == 2 && shutdowns == 3); // app keeps its independent grant
    nativeNavigationResume(); nativeNavigationTick();
    assert(acquisitions == 4); // persisted Off survives wake
    sharedGrant = false;
    nativeNavigationConfigure(true); fakeTime += 20; nativeNavigationTick();
    assert(acquisitions == 5);
    shutdownOkay = false;
    assert(!nativeNavigationSuspend()); // lower-provider failure blocks sleep
    nativeNavigationResume(); nativeNavigationTick();
    assert(acquisitions == 5); // quarantined resources never silently restarted
    shutdownOkay = true;
    assert(nativeNavigationSuspend());
    nativeNavigationResume(); fakeTime += 20; nativeNavigationTick();
    releaseOkay = false;
    assert(!nativeNavigationSuspend());
    assert(lease.grant.slot == 1); // quarantine retains exact ownership
    releaseOkay = true;
    assert(nativeNavigationSuspend() && !lease.grant.slot);
    // A verified boot provider can feed the same UI frame consumer before SD
    // inventory exists. Its module remains owned by the boot controller.
    const unsigned acquiredBeforeBootstrap = acquisitions;
    const unsigned releasedBeforeBootstrap = releases;
    sharedGrant = true;
    assert(!nativeNavigationAttachBootstrap(&navigation));
    sharedGrant = false;
    assert(nativeNavigationAttachBootstrap(&navigation));
    assert(!nativeNavigationAttachBootstrap(&navigation));
    nativeNavigationResume(); fakeTime += 20; nativeNavigationTick();
    assert(nativeNavigationFrame().pressed == RISC_NAV_CONFIRM);
    assert(acquisitions == acquiredBeforeBootstrap && releases == releasedBeforeBootstrap);
    assert(!nativeNavigationSuspend()); // Borrowed module is not quiesced for sleep.
    assert(releases == releasedBeforeBootstrap);
    puts("Firmware navigation acquisition, retry, focus, polling and sleep lifetime: PASS");
}
