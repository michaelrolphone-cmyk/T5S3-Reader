#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../Drivers/usb_ui_navigation/driver.c"

static risc_usb_keyboard_event_v1 events[32];
static size_t event_count, event_cursor;
static unsigned keyboard_polls, pad_polls[2], subscriptions;
static bool unsubscribe_ok = true, pad_ok[2] = {true, true};
static risc_usb_keyboard_state_v1 keyboard_state = {.device = 11, .connected = 1};
static risc_usb_gamepad_state_v1 pad_state[2] = {
    {.device = 21, .hat = 8, .connected = 1}, {.device = 31, .hat = 8, .connected = 1}};
static uint64_t subscribe_test(void *ctx, uint64_t filter) {
    (void)ctx; (void)filter;
    ++subscriptions; event_cursor = event_count;
    return subscriptions;
}
static bool unsubscribe_test(void *ctx, uint64_t token) {
    (void)ctx; assert(token); return unsubscribe_ok;
}
static bool keyboard_poll_test(void *ctx, size_t bound) {
    (void)ctx; assert(bound <= 4); ++keyboard_polls; return true;
}
static int32_t next_test(void *ctx, uint64_t token, risc_usb_keyboard_event_v1 *event) {
    (void)ctx; assert(token);
    if (event_cursor == event_count) return 0;
    *event = events[event_cursor++]; return 1;
}
static bool keyboard_snapshot_test(void *ctx, risc_usb_keyboard_state_v1 *out, size_t *count) {
    (void)ctx; assert(*count >= 1); out[0] = keyboard_state; *count = 1; return true;
}
static bool pad_poll_test(void *ctx, size_t bound) {
    const unsigned index = (unsigned)(uintptr_t)ctx;
    assert(bound <= 4); ++pad_polls[index]; return pad_ok[index];
}
static bool pad_snapshot_test(void *ctx, risc_usb_gamepad_state_v1 *out, size_t *count) {
    assert(*count >= 1); out[0] = pad_state[(uintptr_t)ctx]; *count = 1; return true;
}
static void key_event(uint8_t kind, uint8_t usage) {
    assert(event_count < 32);
    events[event_count++] = (risc_usb_keyboard_event_v1){.device=11, .kind=kind, .usage=usage};
}
static risc_input_navigation_frame_v1 frame(void) {
    risc_input_navigation_frame_v1 out;
    assert(poll(0, &out)); return out;
}
int main(void) {
    risc_usb_keyboard_api_v1 keyboard_api = {1, sizeof(keyboard_api), 0,
        subscribe_test, unsubscribe_test, keyboard_poll_test, next_test, keyboard_snapshot_test};
    risc_usb_gamepad_api_v1 pad_api[2] = {
        {1, sizeof(pad_api[0]), 0, 0, 0, pad_poll_test, 0, pad_snapshot_test},
        {1, sizeof(pad_api[1]), (void*)1, 0, 0, pad_poll_test, 0, pad_snapshot_test}};
    risc_provider_dependency_v1 dependencies[] = {
        {"usb.hid.keyboard", 1, &keyboard_api},
        {"usb.hid.gamepad", 1, &pad_api[0]}, {"usb.xinput.gamepad", 1, &pad_api[1]}};
    assert(start(dependencies, 3));
    assert(!frame().buttons); // neutral startup

    pad_state[0].hat = 2;
    assert(frame().pressed == RISC_NAV_RIGHT);
    assert(frame().buttons == RISC_NAV_RIGHT && !frame().pressed);
    pad_state[0].hat = 8;
    assert(frame().released == RISC_NAV_RIGHT);
    pad_state[1].buttons = 2; // XInput A is bit one
    assert(frame().pressed == RISC_NAV_CONFIRM);
    pad_state[1].buttons = 0;
    assert(frame().released == RISC_NAV_CONFIRM);

    // Keyboard taps keep order even when both arrive before the UI reads.
    key_event(3, 0x28); key_event(4, 0x28);
    assert(frame().pressed == RISC_NAV_CONFIRM);
    assert(frame().released == RISC_NAV_CONFIRM);
    assert(!frame().buttons);

    risc_input_foreground_v1 claim = {"usb.hid.keyboard", 1};
    assert(foreground(0, &claim, 1));
    unsigned polls = keyboard_polls;
    key_event(3, 0x28); key_event(4, 0x28);
    const size_t pending = event_cursor;
    assert(!frame().buttons && keyboard_polls == polls && event_cursor == pending);
    pad_state[0].buttons = 1; // HID button one still controls navigation
    assert(frame().pressed == RISC_NAV_CONFIRM);
    pad_state[0].buttons = 0; (void)frame();

    // Returning focus drops only the UI cursor and gates a held exit key.
    keyboard_state.keys[0] = 0x28;
    assert(foreground(0, 0, 0));
    assert(!frame().buttons && !frame().released);
    keyboard_state.keys[0] = 0; (void)frame();
    key_event(3, 0x52); key_event(4, 0x52);
    assert(frame().pressed == RISC_NAV_UP);
    assert(frame().released == RISC_NAV_UP);

    claim.capability = "usb.hid.gamepad";
    assert(foreground(0, &claim, 1));
    polls = pad_polls[0]; pad_state[0].buttons = 1;
    assert(!frame().buttons && pad_polls[0] == polls);
    assert(foreground(0, 0, 0));
    assert(!frame().buttons); // no carried Confirm after app exit
    pad_state[0].buttons = 0; (void)frame();
    pad_state[0].buttons = 1; assert(frame().pressed == RISC_NAV_CONFIRM);
    pad_state[0].connected = 0;
    assert(!frame().released && !frame().buttons); // unplug cannot activate selection
    pad_state[0].buttons = 0; pad_state[0].connected = 1; (void)frame();

    // A failed/quiet gamepad must not suppress unrelated keyboard edges.
    pad_ok[0] = false;
    key_event(3, 0x29); assert(frame().pressed == RISC_NAV_BACK);
    key_event(4, 0x29); assert(frame().released == RISC_NAV_BACK);
    pad_ok[0] = true;

    unsubscribe_ok = false;
    claim.capability = "usb.hid.keyboard";
    assert(!foreground(0, &claim, 1));
    polls = keyboard_polls; (void)frame(); assert(keyboard_polls == polls);
    assert(!quiesce()); // retain subscription/dependencies on uncertain cleanup
    unsubscribe_ok = true;
    assert(quiesce()); stop();
    assert(!keyboard);
    puts("USB navigation mapping, keyboard order, focus, neutral handback and cleanup: PASS");
}
