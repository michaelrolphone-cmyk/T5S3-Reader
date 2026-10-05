#include <cassert>
#include <cstdio>
#include <HalStorage.h>
#include "../../src/native/NativeNavigationInput.cpp"

uint32_t fakeTime = 1000;
bool storageReady = true, closeOk = true;
std::map<std::string, std::shared_ptr<TestFile>> files;
HalStorage Storage;
static bool available, shutdownOkay = true, sharedGrant = false, releaseOkay = true;
static bool revoked = false, partialAcquire = false, unsafeNavigation = false;
static unsigned acquisitions, releases, shutdowns, drains, polls, resets, generation;
static size_t foregroundCount;
static bool fakePoll(void* context, risc_input_navigation_frame_v1* out) {
    assert(!revoked);
    ++polls;
    const uint32_t bits = context ? RISC_NAV_LEFT : RISC_NAV_CONFIRM;
    *out = {bits, bits, 0};
    return true;
}
static bool fakeForeground(void*, const risc_input_foreground_v1*, size_t count) {
    assert(!revoked);
    foregroundCount = count; return true;
}
static bool fakeReset(void*) { assert(!revoked); ++resets; return true; }
static const risc_input_navigation_api_v1 navigation{
    1, sizeof(navigation), nullptr, fakePoll, fakeForeground, fakeReset};
static int physicalMarker;
static const risc_input_navigation_traits_v1 physicalNavigation{
    {1, sizeof(physicalNavigation), &physicalMarker, fakePoll, fakeForeground, fakeReset},
    RISC_INPUT_NAVIGATION_TRAITS_TAG, RISC_INPUT_NAVIGATION_TRAITS_VERSION,
    RISC_INPUT_NAVIGATION_PHYSICAL_PAGE_PAIR};
static const risc_input_navigation_api_v1* selectedNavigation = &navigation;
namespace RuntimeInstalledProviders {
bool acquireCapability(const char* capability, uint32_t version, Lease* out) {
    assert(!std::strcmp(capability, "input.navigation") && version == 1);
    assert(!out->grant.slot); // Never replace a retained release token.
    ++acquisitions;
    if (!available || unsafeNavigation) return false;
    revoked = partialAcquire;
    *out = {{1, ++generation}, partialAcquire ? nullptr : selectedNavigation};
    return !partialAcquire;
}
bool release(Lease* out) {
    ++releases; assert(out->grant.slot == 1);
    assert(out->grant.generation == generation);
    revoked = true; // Failed quiescence has already revoked interface access.
    if (!releaseOkay) return false;
    *out = {}; return true;
}
bool shutdown() { ++shutdowns; return shutdownOkay; }
bool hasLiveGrants() { return sharedGrant; }
const char* lastError() { return "fixture unavailable"; }
}
bool drainPlatformProvidersForSleep() { ++drains; return shutdownOkay; }
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
    assert(nativeNavigationSuspend() && releases == 1 && drains == 3 && !shutdowns);
    nativeNavigationTick(); assert(acquisitions == 3);
    nativeNavigationResume(); fakeTime += 20; nativeNavigationTick();
    assert(acquisitions == 4);
    sharedGrant = true;
    nativeNavigationConfigure(false);
    assert(releases == 2 && drains == 3 && !shutdowns); // app keeps its independent grant
    nativeNavigationResume(); nativeNavigationTick();
    assert(acquisitions == 4); // persisted Off survives wake
    sharedGrant = false;
    nativeNavigationConfigure(true); fakeTime += 20; nativeNavigationTick();
    assert(acquisitions == 5);
    shutdownOkay = false;
    assert(!nativeNavigationSuspend()); // unrelated provider still blocks sleep
    const unsigned drainsBeforeCancel = drains;
    nativeNavigationResume(); nativeNavigationTick();
    assert(acquisitions == 6 && api && !quarantined);
    assert(drains == drainsBeforeCancel && !shutdowns); // cancellation is not global teardown
    unsafeNavigation = true;
    assert(!nativeNavigationSuspend());
    nativeNavigationResume(); fakeTime += 20; nativeNavigationTick();
    assert(acquisitions == 7 && !api); // graph rejects the unsafe node itself
    nativeNavigationTick(); assert(acquisitions == 7); // bounded retry still applies
    unsafeNavigation = false;
    shutdownOkay = true;
    assert(nativeNavigationSuspend());
    nativeNavigationResume(); fakeTime += 20; nativeNavigationTick();
    releaseOkay = false;
    assert(!nativeNavigationSuspend());
    const auto pending = lease.grant;
    const unsigned beforeRetry = acquisitions, resetsBeforeRetry = resets;
    assert(pending.slot == 1 && !api); // quarantine retains exact ownership
    nativeNavigationResume(); nativeNavigationTick();
    assert(acquisitions == beforeRetry && resets == resetsBeforeRetry);
    assert(lease.grant.slot == pending.slot && lease.grant.generation == pending.generation);
    releaseOkay = true;
    nativeNavigationResume(); fakeTime += 20; nativeNavigationTick();
    assert(acquisitions == beforeRetry + 1 && api);
    assert(nativeNavigationSuspend() && !lease.grant.slot);

    // Failed acquisition can still return a pending grant. Repeated ticks and
    // resume must retain it until exact cleanup succeeds, without API calls.
    partialAcquire = true; releaseOkay = false;
    nativeNavigationResume(); nativeNavigationTick();
    assert(!api && lease.grant.slot && quarantined);
    const auto partial = lease.grant;
    const unsigned beforePartialRetry = acquisitions;
    fakeTime += 2000; nativeNavigationTick(); nativeNavigationResume();
    assert(acquisitions == beforePartialRetry && lease.grant.generation == partial.generation);
    partialAcquire = false; releaseOkay = true;
    nativeNavigationResume(); nativeNavigationTick();
    assert(api && acquisitions == beforePartialRetry + 1);
    assert(nativeNavigationSuspend());
    // Replacing the selected input provider replaces its interpretation too.
    // The frame is one provider snapshot, never OR-aggregated across providers.
    assert(!nativeNavigationHasPhysicalPagePair());
    selectedNavigation = &physicalNavigation.base;
    nativeNavigationResume(); fakeTime += 20; nativeNavigationTick();
    assert(nativeNavigationHasPhysicalPagePair());
    assert(nativeNavigationFrame().buttons == RISC_NAV_LEFT);
    releaseOkay = false;
    assert(!nativeNavigationSuspend());
    assert(!nativeNavigationHasPhysicalPagePair() && !nativeNavigationFrame().buttons);
    releaseOkay = true;
    assert(nativeNavigationSuspend());
    selectedNavigation = &navigation;
    nativeNavigationResume(); fakeTime += 20; nativeNavigationTick();
    assert(!nativeNavigationHasPhysicalPagePair());
    assert(nativeNavigationFrame().buttons == RISC_NAV_CONFIRM);
    assert(nativeNavigationSuspend());
    // A verified boot provider can feed the same UI frame consumer before SD
    // inventory exists. Its module remains owned by the boot controller.
    const unsigned acquiredBeforeBootstrap = acquisitions;
    const unsigned releasedBeforeBootstrap = releases;
    sharedGrant = true;
    assert(!nativeNavigationAttachBootstrap(&navigation));
    sharedGrant = false;
    revoked = false; // Separate boot-owned lifetime, outside the installed graph.
    assert(nativeNavigationAttachBootstrap(&navigation));
    assert(!nativeNavigationAttachBootstrap(&navigation));
    nativeNavigationResume(); fakeTime += 20; nativeNavigationTick();
    assert(nativeNavigationFrame().pressed == RISC_NAV_CONFIRM);
    assert(acquisitions == acquiredBeforeBootstrap && releases == releasedBeforeBootstrap);
    assert(!nativeNavigationSuspend()); // Borrowed module is not quiesced for sleep.
    assert(releases == releasedBeforeBootstrap);
    puts("Firmware navigation acquisition, retry, focus, polling and sleep lifetime: PASS");
}
