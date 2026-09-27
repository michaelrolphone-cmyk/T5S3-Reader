#include "NativeTextInput.h"

#include "NativeNavigationInput.h"
#include "runtime/drivers/InstalledProviderGraph.h"

#include <cstdint>

namespace {
RuntimeInstalledProviders::Lease lease;
const risc_text_input_api_v1* api = nullptr;
uint64_t subscription = 0;
bool navigationClaimed = false;

/* App capability generations deliberately skip UINT32_MAX. */
constexpr uint32_t kFirmwareTextFocusToken = UINT32_MAX;

bool valid(const risc_text_input_api_v1* candidate) {
    return candidate &&
           candidate->api_version == RISC_TEXT_INPUT_API_V1 &&
           candidate->struct_size >= sizeof(*candidate) &&
           candidate->subscribe && candidate->unsubscribe &&
           candidate->poll && candidate->next;
}
}  // namespace

bool nativeTextInputBegin() {
    if (api && subscription) return true;
    /* A partially retained provider means cleanup previously failed. Do not
     * hand out or overwrite that generation. */
    if (api || lease.grant.slot || navigationClaimed) return false;

    RuntimeInstalledProviders::Lease acquired{};
    if (!RuntimeInstalledProviders::acquireCapability(
            "input.text", RISC_TEXT_INPUT_API_V1, &acquired))
        return false;

    const auto* candidate =
        static_cast<const risc_text_input_api_v1*>(acquired.interface);
    if (!valid(candidate)) {
        (void)RuntimeInstalledProviders::release(&acquired);
        return false;
    }

    lease = acquired;
    api = candidate;
    if (!nativeNavigationClaim(kFirmwareTextFocusToken, "input.text",
                               RISC_TEXT_INPUT_API_V1)) {
        if (RuntimeInstalledProviders::release(&lease)) api = nullptr;
        return false;
    }
    navigationClaimed = true;

    subscription = api->subscribe(api->context, 0);
    if (!subscription) {
        if (RuntimeInstalledProviders::release(&lease)) {
            api = nullptr;
            nativeNavigationRelease(kFirmwareTextFocusToken);
            navigationClaimed = false;
        }
        return false;
    }
    return true;
}

bool nativeTextInputActive() { return api && subscription; }

bool nativeTextInputPoll() {
    return api && subscription && api->poll(api->context, 8);
}

int32_t nativeTextInputNext(risc_text_input_event_v1& out) {
    if (!api || !subscription) return -1;
    return api->next(api->context, subscription, &out);
}

bool nativeTextInputEnd() {
    if (!api && !lease.grant.slot && !navigationClaimed) return true;
    if (!api) return false;

    if (subscription) {
        if (!api->unsubscribe(api->context, subscription)) return false;
        subscription = 0;
    }

    if (lease.grant.slot && !RuntimeInstalledProviders::release(&lease))
        return false;

    api = nullptr;
    if (navigationClaimed) {
        nativeNavigationRelease(kFirmwareTextFocusToken);
        navigationClaimed = false;
    }
    return true;
}
