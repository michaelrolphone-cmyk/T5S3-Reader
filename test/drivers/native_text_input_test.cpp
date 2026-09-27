#include <cassert>
#include <cstdio>
#include <cstring>

#include "../../src/native/NativeTextInput.cpp"

static bool available = true;
static bool subscribe_ok = true;
static bool unsubscribe_ok = true;
static bool poll_ok = true;
static unsigned acquisitions, releases, claims, claim_releases, polls;
static uint32_t claimed_token;
static char claimed_capability[32];

static uint64_t fake_subscribe(void*, uint64_t source) {
    assert(source == 0);
    return subscribe_ok ? 55 : 0;
}
static bool fake_unsubscribe(void*, uint64_t token) {
    assert(token == 55);
    return unsubscribe_ok;
}
static bool fake_poll(void*, size_t bound) {
    assert(bound == 8);
    ++polls;
    return poll_ok;
}
static int32_t fake_next(void*, uint64_t token, risc_text_input_event_v1* out) {
    assert(token == 55 && out);
    *out = {1, 9, 'x', RISC_TEXT_KEY_NONE, RISC_TEXT_EVENT_KEY_DOWN, 0};
    return 1;
}
static const risc_text_input_api_v1 fake_api = {
    1, sizeof(fake_api), nullptr,
    fake_subscribe, fake_unsubscribe, fake_poll, fake_next
};

namespace RuntimeInstalledProviders {
bool acquireCapability(const char* capability, uint32_t version, Lease* out) {
    assert(!std::strcmp(capability, "input.text"));
    assert(version == RISC_TEXT_INPUT_API_V1);
    ++acquisitions;
    if (!available) return false;
    *out = {{1, 1}, &fake_api};
    return true;
}
bool release(Lease* out) {
    ++releases;
    *out = {};
    return true;
}
const char* lastError() { return "fixture"; }
}  // namespace RuntimeInstalledProviders

bool nativeNavigationClaim(uint32_t token, const char* capability, uint32_t version) {
    ++claims;
    claimed_token = token;
    std::snprintf(claimed_capability, sizeof(claimed_capability), "%s", capability);
    return token == UINT32_MAX && !std::strcmp(capability, "input.text") &&
           version == 1;
}
void nativeNavigationRelease(uint32_t token) {
    assert(token == UINT32_MAX);
    ++claim_releases;
}

int main() {
    assert(nativeTextInputBegin());
    assert(nativeTextInputActive());
    assert(acquisitions == 1 && claims == 1);
    assert(claimed_token == UINT32_MAX &&
           !std::strcmp(claimed_capability, "input.text"));
    assert(nativeTextInputBegin());
    assert(acquisitions == 1);

    assert(nativeTextInputPoll() && polls == 1);
    risc_text_input_event_v1 event{};
    assert(nativeTextInputNext(event) == 1);
    assert(event.codepoint == 'x' && event.kind == RISC_TEXT_EVENT_KEY_DOWN);
    assert(nativeTextInputEnd());
    assert(!nativeTextInputActive());
    assert(releases == 1 && claim_releases == 1);

    subscribe_ok = false;
    assert(!nativeTextInputBegin());
    assert(!nativeTextInputActive());
    assert(releases == 2 && claim_releases == 2);

    subscribe_ok = true;
    available = false;
    assert(!nativeTextInputBegin());
    assert(!nativeTextInputActive());

    puts("Firmware semantic text input acquisition, focus and cleanup: PASS");
}
