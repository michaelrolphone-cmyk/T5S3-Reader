#include "NativeNavigationInput.h"
#include "platform/PlatformStorage.h"
#include "runtime/drivers/InstalledProviderGraph.h"
#include "runtime/input/NavigationFocus.h"
#include <Arduino.h>
#include <HalStorage.h>
#include <Logging.h>

namespace {
RuntimeInstalledProviders::Lease lease;
RuntimeInput::NavigationFocus focus;
const risc_input_navigation_api_v1* api = nullptr;
risc_input_navigation_frame_v1 frame{};
constexpr uint32_t kActivationRetryMs = 1000;
bool configured = true, enabled = true, attempted = false, quarantined = false, usable = true;
bool bootstrapAttached = false;
uint32_t lastPoll = 0, heldSince = 0, lastAttemptMs = 0;
uint32_t releasedHeldMs = 0;
bool singleButton(uint32_t bits) { return bits && !(bits & (bits - 1)); }

void clearFrame() { frame = {}; heldSince = millis(); releasedHeldMs = 0; }
bool ready() {
    if (quarantined) return false; // The graph may be backed by the read-only boot store.
    if (api) return true;
    if (lease.grant.slot) return false; // A revoked grant still awaits checked cleanup.
    const uint32_t now = millis();
    if (attempted && static_cast<uint32_t>(now - lastAttemptMs) < kActivationRetryMs)
        return false;
    attempted = true;
    lastAttemptMs = now;
    if (!RuntimeInstalledProviders::acquireCapability("input.navigation", 1, &lease)) {
        if (lease.grant.slot) quarantined = true;
        LOG_INF("INPUT", "Navigation provider unavailable: %s", RuntimeInstalledProviders::lastError());
        return false;
    }
    const auto* candidate = static_cast<const risc_input_navigation_api_v1*>(lease.interface);
    if (!candidate || candidate->api_version != 1 || candidate->struct_size < sizeof(*candidate) ||
        !candidate->poll || !candidate->foreground || !candidate->reset) {
        if (!RuntimeInstalledProviders::release(&lease)) quarantined = true;
        return false;
    }
    api = candidate;
    usable = focus.apply(api) && api->reset(api->context);
    LOG_INF("INPUT", "Navigation provider %s", usable ? "ready" : "handoff failed");
    return usable;
}
}
void nativeNavigationTick() {
    frame.pressed = frame.released = 0;
    releasedHeldMs = 0;
    if (!enabled || !ready() || !usable) { clearFrame(); return; }
    const uint32_t now = millis();
    if (static_cast<uint32_t>(now - lastPoll) < 20) return;
    lastPoll = now;
    risc_input_navigation_frame_v1 next{};
    if (!api->poll(api->context, &next)) { clearFrame(); return; }
    // The scalar legacy query can describe only one unambiguous gesture.
    // Keep its duration for exactly the matching release frame, before changing
    // the hold origin. Chords, button substitutions and unmatched edges are short.
    if (singleButton(frame.buttons) && !next.buttons && !next.pressed &&
        next.released == frame.buttons)
        releasedHeldMs = static_cast<uint32_t>(now - heldSince);
    if (next.buttons != frame.buttons) heldSince = now;
    frame = next;
}
const risc_input_navigation_frame_v1& nativeNavigationFrame() { return frame; }
bool nativeNavigationHasPhysicalPagePair() {
    return enabled && usable && !quarantined &&
        risc_input_navigation_has_physical_page_pair(api);
}
unsigned long nativeNavigationHeldMs() {
    if (frame.released) return releasedHeldMs;
    return singleButton(frame.buttons) ? static_cast<uint32_t>(millis() - heldSince) : 0;
}
bool nativeNavigationClaim(uint32_t token, const char* capability, uint32_t version) {
    clearFrame();
    const bool result = focus.acquire(token, capability, version, api);
    usable = result || focus.apply(api);
    return result;
}
void nativeNavigationRelease(uint32_t token) {
    clearFrame();
    usable = focus.release(token, api);
}
void nativeNavigationBoundary() {
    clearFrame();
    usable = focus.apply(api) && (!api || api->reset(api->context));
}
void nativeNavigationRetry() {
    if (!api && !quarantined) {
        attempted = false;
        lastAttemptMs = 0;
    }
}
bool nativeNavigationAttachBootstrap(const risc_input_navigation_api_v1* candidate) {
    if (api || bootstrapAttached || quarantined || lease.grant.slot ||
        RuntimeInstalledProviders::hasLiveGrants() || !candidate ||
        candidate->api_version != RISC_INPUT_NAVIGATION_API_V1 ||
        candidate->struct_size < sizeof(*candidate) || !candidate->poll ||
        !candidate->foreground || !candidate->reset) return false;
    clearFrame();
    if (!focus.apply(candidate) || !candidate->reset(candidate->context)) return false;
    api = candidate;
    bootstrapAttached = true;
    usable = true;
    enabled = configured;
    attempted = false;
    return true;
}
static bool releaseNavigation(bool requireGraphShutdown) {
    clearFrame();
    enabled = false;
    if (bootstrapAttached) {
        // The bootstrap module remains owned by its boot controller. Never
        // claim sleep quiescence or release it through the installed graph.
        return api && api->reset(api->context) && !requireGraphShutdown;
    }
    if (api && !api->reset(api->context)) return false;
    // release() can revoke API access even when quiescence fails. Retry only
    // the retained grant; never reset/poll the old provider after that point.
    api = nullptr;
    if (lease.grant.slot && !RuntimeInstalledProviders::release(&lease)) {
        quarantined = true;
        return false;
    }
    quarantined = false;
    // The board must still prove graph-wide quiescence before sleep. A failed
    // unrelated node blocks sleep, but must not disable safely released input
    // after cancellation. The graph itself keeps unsafe nodes pinned and
    // rejects their reacquisition; ordinary disable/resume never tears it down.
    return !requireGraphShutdown || drainPlatformProvidersForSleep();
}
bool nativeNavigationSuspend() { return releaseNavigation(true); }
void nativeNavigationConfigure(bool requested) {
    if (configured == requested) return;
    configured = requested;
    if (requested) nativeNavigationResume();
    else if (!releaseNavigation(false)) LOG_ERR("INPUT", "Navigation disabled; unsafe provider retained");
}
void nativeNavigationResume() {
    if ((quarantined || (!api && lease.grant.slot)) && !releaseNavigation(false)) return;
    enabled = configured;
    nativeNavigationRetry();
    nativeNavigationBoundary();
}
