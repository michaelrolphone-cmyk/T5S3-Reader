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
bool enabled = true, attempted = false, quarantined = false, usable = true;
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
        lease = {};
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
bool nativeNavigationSuspend() {
    clearFrame();
    enabled = false;
    if (!api) return !quarantined;
    if (!api->reset(api->context)) return false;
    const bool released = RuntimeInstalledProviders::release(&lease);
    // Graph release may consume the grant even when a module is quarantined.
    api = nullptr; lease = {};
    // Releasing this composite can leave a failed lower dependency pinned.
    // Prove the whole graph quiescent before sleep, not just our top-level ELF.
    quarantined = !released || !RuntimeInstalledProviders::shutdown();
    return !quarantined;
}
void nativeNavigationResume() {
    enabled = true;
    nativeNavigationRetry();
    nativeNavigationBoundary();
}
