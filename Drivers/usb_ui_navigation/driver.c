#include "RiscInputNavigationV1.h"
#include "RiscUsbHidV1.h"

/* UI policy is an installed composite provider. The firmware only consumes
 * input.navigation; physical USB startup/power stays in the controller ELF.
 * Retaining dependency grants across focus switches avoids reenumeration. */
#define DEVICES 4u
#define KEYBOARD 1u
#define HID_PAD 2u
#define XINPUT_PAD 4u
static const risc_usb_keyboard_api_v1 *keyboard;
static const risc_usb_gamepad_api_v1 *pads[2];
static uint64_t subscription;
static uint8_t blocked, gated = 7u;
static uint32_t states[3], previous;
static bool sync_frame;
typedef struct { uint64_t device; uint16_t keys; } key_state;
static key_state keys[DEVICES];
static uint64_t pad_devices[2][DEVICES];

static bool equal(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static const uint8_t usages[] = {0x29, 0x28, 0x58, 0x2c, 0x50, 0x4f,
                               0x52, 0x51, 0x4b, 0x4e, 0x4a};
static const uint32_t actions[] = {RISC_NAV_BACK, RISC_NAV_CONFIRM,
    RISC_NAV_CONFIRM, RISC_NAV_CONFIRM, RISC_NAV_LEFT, RISC_NAV_RIGHT,
    RISC_NAV_UP, RISC_NAV_DOWN, RISC_NAV_PAGE_BACK, RISC_NAV_PAGE_FORWARD,
    RISC_NAV_HOME};
static uint16_t key_bit(uint8_t usage) {
    for (unsigned i = 0; i < sizeof(usages); ++i)
        if (usages[i] == usage) return (uint16_t)(1u << i);
    return 0;
}
static uint32_t key_buttons(void) {
    uint16_t held = 0;
    for (unsigned i = 0; i < DEVICES; ++i) held |= keys[i].keys;
    uint32_t result = 0;
    for (unsigned i = 0; i < sizeof(usages); ++i)
        if (held & (1u << i)) result |= actions[i];
    return result;
}
static bool unsubscribe_keyboard(void) {
    if (subscription && !keyboard->unsubscribe(keyboard->context, subscription)) return false;
    subscription = 0;
    return true;
}
static void clear_source(unsigned source) {
    if (states[source]) sync_frame = true;
    states[source] = 0;
    gated |= (uint8_t)(1u << source);
    if (!source) for (unsigned i = 0; i < DEVICES; ++i) keys[i] = (key_state){0};
}
static bool reset(void *context) {
    (void)context;
    for (unsigned i = 0; i < 3; ++i) clear_source(i);
    previous = 0;
    sync_frame = true;
    return !keyboard || unsubscribe_keyboard();
}
static bool foreground(void *context, const risc_input_foreground_v1 *claims, size_t count) {
    (void)context;
    if (count > RISC_INPUT_NAVIGATION_MAX_FOREGROUND || (count && !claims)) return false;
    uint8_t next = 0;
    for (size_t i = 0; i < count; ++i) {
        const char *cap = claims[i].capability;
        if (!cap || !claims[i].api_version) return false;
        if (equal(cap, "usb.hid.keyboard")) next |= KEYBOARD;
        if (equal(cap, "usb.hid.gamepad")) next |= HID_PAD;
        if (equal(cap, "usb.xinput.gamepad")) next |= XINPUT_PAD;
        if (equal(cap, "usb.hid")) next |= KEYBOARD | HID_PAD;
        if (equal(cap, "usb.host") || equal(cap, "usb.controller") ||
            equal(cap, "board.power.vbus") || equal(cap, "input.navigation")) next = 7u;
    }
    const uint8_t changed = blocked ^ next;
    blocked = next;
    for (unsigned i = 0; i < 3; ++i) if (changed & (1u << i)) clear_source(i);
    return !((changed | blocked) & KEYBOARD) || unsubscribe_keyboard();
}
static bool keyboard_resync(void) {
    risc_usb_keyboard_state_v1 snapshot[DEVICES] = {{0}};
    size_t count = DEVICES;
    if (!keyboard->snapshot(keyboard->context, snapshot, &count) || count > DEVICES) return false;
    for (unsigned i = 0; i < DEVICES; ++i) keys[i] = (key_state){0};
    for (size_t i = 0; i < count; ++i) if (snapshot[i].connected) {
        keys[i].device = snapshot[i].device;
        for (unsigned k = 0; k < 6; ++k) keys[i].keys |= key_bit(snapshot[i].keys[k]);
    }
    /* Fresh cursor drops UI-only history. Foreground keyboard subscriptions
     * are independent and never drained or coalesced here. */
    if (!unsubscribe_keyboard()) return false;
    subscription = keyboard->subscribe(keyboard->context, 0);
    return subscription != 0;
}
static bool poll_keyboard(void) {
    if (!subscription) {
        subscription = keyboard->subscribe(keyboard->context, 0);
        if (!subscription) return false;
    }
    if (!keyboard->poll(keyboard->context, 4)) return false;
    if (gated & KEYBOARD) {
        if (!keyboard_resync()) return false;
        if (!key_buttons()) gated &= (uint8_t)~KEYBOARD;
        states[0] = 0;
        return true;
    }
    /* Deliver at most one navigation transition per UI frame, preserving
     * quick keyboard taps; skip at most one bounded queue of unrelated keys. */
    for (unsigned n = 0; n < RISC_USB_INPUT_QUEUE_LENGTH; ++n) {
        risc_usb_keyboard_event_v1 event = {0};
        const int32_t rc = keyboard->next(keyboard->context, subscription, &event);
        if (!rc) break;
        if (rc < 0 || event.kind == 5) { clear_source(0); return keyboard_resync(); }
        key_state *slot = 0;
        for (unsigned i = 0; i < DEVICES; ++i)
            if (keys[i].device == event.device) { slot = &keys[i]; break; }
        if (event.kind == 2) {
            if (slot) *slot = (key_state){0};
            sync_frame = true;
        } else if (event.kind == 1 || event.kind == 3 || event.kind == 4) {
            if (!slot) for (unsigned i = 0; i < DEVICES; ++i)
                if (!keys[i].device) { slot = &keys[i]; slot->device = event.device; break; }
            if (!slot) return false;
            if (event.kind == 3) slot->keys |= key_bit(event.usage);
            if (event.kind == 4) slot->keys &= (uint16_t)~key_bit(event.usage);
        }
        const uint32_t next = key_buttons();
        if (next != states[0]) { states[0] = next; break; }
    }
    return true;
}
static uint32_t pad_buttons(const risc_usb_gamepad_state_v1 *pad, bool xinput) {
    if (!pad->connected) return 0;
    uint32_t out = 0;
    if (pad->buttons & (xinput ? 2u : 1u)) out |= RISC_NAV_CONFIRM;
    if (pad->buttons & (xinput ? 1u : 2u)) out |= RISC_NAV_BACK;
    if (pad->buttons & 0x10u) out |= RISC_NAV_PAGE_BACK;
    if (pad->buttons & 0x20u) out |= RISC_NAV_PAGE_FORWARD;
    if (pad->x < -16000 || (pad->hat >= 5 && pad->hat <= 7)) out |= RISC_NAV_LEFT;
    if (pad->x > 16000 || (pad->hat >= 1 && pad->hat <= 3)) out |= RISC_NAV_RIGHT;
    if (pad->y < -16000 || pad->hat == 7 || pad->hat == 0 || pad->hat == 1) out |= RISC_NAV_UP;
    if (pad->y > 16000 || (pad->hat >= 3 && pad->hat <= 5)) out |= RISC_NAV_DOWN;
    return out;
}
static bool poll_pad(unsigned index) {
    const risc_usb_gamepad_api_v1 *api = pads[index];
    risc_usb_gamepad_state_v1 snapshot[DEVICES] = {{0}};
    size_t count = DEVICES;
    if (!api->poll(api->context, 4) || !api->snapshot(api->context, snapshot, &count) || count > DEVICES)
        return false;
    uint32_t next = 0;
    for (unsigned old = 0; old < DEVICES; ++old) if (pad_devices[index][old]) {
        bool present = false;
        for (size_t n = 0; n < count; ++n)
            if (snapshot[n].device == pad_devices[index][old] && snapshot[n].connected) present = true;
        if (!present) sync_frame = true;
    }
    for (unsigned i = 0; i < DEVICES; ++i) pad_devices[index][i] = 0;
    for (size_t i = 0; i < count; ++i) {
        if (snapshot[i].connected) pad_devices[index][i] = snapshot[i].device;
        next |= pad_buttons(&snapshot[i], index == 1);
    }
    const uint8_t bit = (uint8_t)(1u << (index + 1));
    if (gated & bit) {
        if (!next) gated &= (uint8_t)~bit;
        next = 0;
    }
    states[index + 1] = next;
    return true;
}
static bool poll(void *context, risc_input_navigation_frame_v1 *out) {
    (void)context;
    if (!keyboard || !out) return false;
    if (!(blocked & KEYBOARD) && !poll_keyboard()) clear_source(0);
    for (unsigned i = 0; i < 2; ++i)
        if (!(blocked & (1u << (i + 1))) && !poll_pad(i)) clear_source(i + 1);
    const uint32_t current = states[0] | states[1] | states[2];
    *out = (risc_input_navigation_frame_v1){current,
        sync_frame ? 0 : current & ~previous, sync_frame ? 0 : previous & ~current};
    previous = current;
    sync_frame = false;
    return true;
}
static bool start(const risc_provider_dependency_v1 *deps, size_t count) {
    if (keyboard || !deps || count != 3) return false;
    const char *names[] = {"usb.hid.keyboard", "usb.hid.gamepad", "usb.xinput.gamepad"};
    for (unsigned i = 0; i < 3; ++i)
        if (!equal(deps[i].capability_id, names[i]) || deps[i].api_version != 1 || !deps[i].api) return false;
    const risc_usb_keyboard_api_v1 *k = deps[0].api;
    if (k->api_version != 1 || k->struct_size < sizeof(*k) || !k->subscribe ||
        !k->unsubscribe || !k->poll || !k->next || !k->snapshot) return false;
    for (unsigned i = 0; i < 2; ++i) {
        const risc_usb_gamepad_api_v1 *p = deps[i + 1].api;
        if (p->api_version != 1 || p->struct_size < sizeof(*p) || !p->poll || !p->snapshot) return false;
    }
    keyboard = k;
    pads[0] = deps[1].api; pads[1] = deps[2].api;
    blocked = 0;
    return reset(0);
}
static bool quiesce(void) { return !keyboard || reset(0); }
static void stop(void) {
    if (!quiesce()) return;
    keyboard = 0; pads[0] = pads[1] = 0;
}
static const risc_input_navigation_api_v1 api = {
    RISC_INPUT_NAVIGATION_API_V1, sizeof(risc_input_navigation_api_v1), 0,
    poll, foreground, reset
};
static const risc_driver_v2 driver = {
    RISC_PROVIDER_DRIVER_ABI_V2, sizeof(risc_driver_v2), "usb-ui-navigation",
    "input.navigation", RISC_INPUT_NAVIGATION_API_V1, &api, start, stop, quiesce
};
__attribute__((visibility("default"))) const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    return abi == RISC_PROVIDER_DRIVER_ABI_V2 ? &driver : 0;
}
