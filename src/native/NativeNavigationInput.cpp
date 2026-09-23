#include "NativeNavigationInput.h"
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
bool configured = true, enabled = true, attempted = false, quarantined = false, usable = true;
uint32_t lastPoll = 0, heldSince = 0;

void clearFrame() { frame = {}; heldSince = millis(); }
bool ready() {
    if (api) return true;
    if (attempted || quarantined || !Storage.ready()) return false;
    attempted = true;
    if (!RuntimeInstalledProviders::acquireCapability("input.navigation", 1, &lease)) {
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
    if (!enabled || !ready() || !usable) { clearFrame(); return; }
    const uint32_t now = millis();
    if (static_cast<uint32_t>(now - lastPoll) < 20) return;
    lastPoll = now;
    risc_input_navigation_frame_v1 next{};
    if (!api->poll(api->context, &next)) { clearFrame(); return; }
    if (next.buttons != frame.buttons) heldSince = now;
    frame = next;
}
const risc_input_navigation_frame_v1& nativeNavigationFrame() { return frame; }
unsigned long nativeNavigationHeldMs() {
    return frame.buttons ? static_cast<uint32_t>(millis() - heldSince) : 0;
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
void nativeNavigationRetry() { if (!api && !quarantined) attempted = false; }
static bool releaseNavigation(bool requireGraphShutdown) {
    clearFrame();
    enabled = false;
    if (!api) {
        if (lease.grant.slot && !RuntimeInstalledProviders::release(&lease)) {
            quarantined = true;
            return false;
        }
        // Failed activation can retain a physical dependency even though no
        // navigation API was returned. Absence of our grant is not quiescence.
        if (!attempted && !quarantined) return true;
        if (!requireGraphShutdown && !quarantined && RuntimeInstalledProviders::hasLiveGrants()) return true;
        quarantined = !RuntimeInstalledProviders::shutdown();
        return !quarantined;
    }
    if (!api->reset(api->context)) return false;
    const bool released = RuntimeInstalledProviders::release(&lease);
    // A failed release retains the exact grant for a later cleanup retry.
    api = nullptr;
    if (!released) { quarantined = true; return false; }
    // Releasing this composite can leave a failed lower dependency pinned.
    // Prove the whole graph quiescent before sleep, not just our top-level ELF.
    const bool shared = !requireGraphShutdown && RuntimeInstalledProviders::hasLiveGrants();
    quarantined = !released || (!shared && !RuntimeInstalledProviders::shutdown());
    return !quarantined;
}
bool nativeNavigationSuspend() { return releaseNavigation(true); }
void nativeNavigationConfigure(bool requested) {
    if (configured == requested) return;
    configured = requested;
    if (requested) nativeNavigationResume();
    else if (!releaseNavigation(false)) LOG_ERR("INPUT", "Navigation disabled; unsafe provider retained");
}
void nativeNavigationResume() {
    enabled = configured;
    nativeNavigationRetry();
    nativeNavigationBoundary();
}
