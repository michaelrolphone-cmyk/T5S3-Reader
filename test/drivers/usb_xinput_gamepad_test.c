#include "../../Drivers/usb_xinput_gamepad/driver.c"
#include <assert.h>
#include <stdio.h>
#include <string.h>

// Composite receiver with HID first, then a non-default XInput interface and
// endpoint, followed by Xbox's unrelated shortened vendor interface record.
static uint8_t config[] = {
    9,2,54,0,3,1,0,0x80,50,
    9,4,0,0,1,3,0,0,0, 7,5,0x81,3,8,0,10,
    9,4,2,0,2,0xff,0x5d,1,0, 7,5,0x83,3,32,0,4, 7,5,4,3,32,0,8,
    6,4,3,0,0,0xff
};
static uint64_t fake_device = 51;
static uint16_t fake_vid = 0x045e, fake_pid = 0x028e;
static uint8_t packet[64];
static int32_t packet_length;
static unsigned claims, releases, configurations, reads;
static bool reject_claim;
static bool fake_poll(void *ctx, size_t budget, size_t *processed) {
    (void)ctx; assert(budget == 8); *processed = 0; return true;
}
static bool fake_devices(void *ctx, uint64_t *out, size_t *count) {
    (void)ctx; assert(*count >= 1); *count = fake_device ? 1 : 0;
    if (fake_device) out[0] = fake_device;
    return true;
}
static bool fake_config(void *ctx, uint64_t device, uint8_t *out, size_t *count,
                         uint16_t *vid, uint16_t *pid) {
    (void)ctx; assert(device == fake_device && *count >= sizeof(config));
    memcpy(out, config, sizeof(config)); *count = sizeof(config);
    *vid = fake_vid; *pid = fake_pid; ++configurations; return true;
}
static bool fake_claim(void *ctx, uint64_t device, uint8_t iface, uint8_t alt, uint64_t *out) {
    (void)ctx; assert(device == fake_device && iface == 2 && alt == 0);
    ++claims;
    if (reject_claim) return false;
    *out = device + 1000; return true;
}
static void fake_release(void *ctx, uint64_t claim) { (void)ctx; assert(claim > 1000); ++releases; }
static int32_t fake_read(void *ctx, uint64_t claim, uint8_t endpoint, uint8_t *out,
                         size_t capacity, uint32_t timeout) {
    (void)ctx;
    assert(claim == fake_device + 1000 && endpoint == 0x83 && capacity == 64 && timeout == 10);
    ++reads;
    int32_t result = packet_length; packet_length = 0;
    if (result > 0 && result <= 64) memcpy(out, packet, (size_t)result);
    return result;
}
static risc_usb_host_interrupt_v1 fake_host = {
    .discovery = {
        .host = {.api_version = 1, .struct_size = sizeof(risc_usb_host_interrupt_v1),
                 .configuration = fake_config, .claim = fake_claim, .release = fake_release},
        .poll = fake_poll, .devices = fake_devices
    }, .interrupt_read = fake_read
};
static void send(uint8_t directions, uint8_t buttons) {
    memset(packet, 0, sizeof(packet)); packet[1] = 20;
    packet[2] = directions; packet[3] = buttons; packet_length = 20;
}
static void drain(const risc_usb_gamepad_api_v1 *input, uint64_t subscription) {
    risc_usb_gamepad_event_v1 event;
    while (input->next(0, subscription, &event) > 0) {}
}
int main(void) {
    assert(!t5_driver_get(1));
    const risc_driver_v2 *driver = t5_driver_get(2);
    assert(!strcmp(driver->capability_id, "usb.xinput.gamepad"));
    const risc_usb_gamepad_api_v1 *input = driver->capability;
    risc_provider_dependency_v1 dependency = {"usb.host", 1, &fake_host};
    assert(!driver->start(0, 0));
    fake_host.discovery.host.struct_size = sizeof(risc_usb_host_discovery_v1);
    assert(!driver->start(&dependency, 1));
    fake_host.discovery.host.struct_size = sizeof(fake_host);
    assert(driver->start(&dependency, 1) && !driver->start(&dependency, 1));
    uint64_t subscription = input->subscribe(0, 0), filtered = input->subscribe(0, 999);
    assert(subscription && filtered && !driver->quiesce());
    assert(!input->poll(0, 0) && !input->poll(0, 17));
    assert(input->poll(0, 4) && claims == 1 && configurations == 1);
    assert(reads == 1); // Idle endpoint is attempted only once per poll.
    for (unsigned i = 0; i < 10; ++i) assert(input->poll(0, 4));
    assert(configurations == 1); // No repeated configuration scans.
    risc_usb_gamepad_state_v1 state;
    size_t count = 1;
    assert(input->snapshot(0, &state, &count) && count == 1 && !state.connected);
    risc_usb_gamepad_event_v1 event;
    assert(input->next(0, subscription, &event) == 0);
    for (int length = 1; length < 20; ++length) {
        send(0, 0xf0); packet_length = length;
        assert(input->poll(0, 4) && input->next(0, subscription, &event) == 0);
    }
    send(0, 0xf0); packet[0] = 1; // LED/status notification.
    assert(input->poll(0, 4) && input->next(0, subscription, &event) == 0);
    send(0, 0xf0); packet[1] = 19;
    assert(input->poll(0, 4) && input->next(0, subscription, &event) == 0);
    send(0x39, 0xf7); // NE, Back, Start, all face/shoulder buttons and Guide.
    packet[4] = packet[5] = 255;
    packet[6] = 0; packet[7] = 0x80; // X = -32768
    packet[8] = 0; packet[9] = 0x80; // Y saturates positive when inverted.
    packet[10] = 0xff; packet[11] = 0x7f;
    packet[12] = 0xff; packet[13] = 0x7f;
    assert(input->poll(0, 4));
    assert(input->next(0, subscription, &event) == 1 && event.kind == 1);
    assert(input->next(0, subscription, &event) == 1 && event.kind == 3);
    assert(event.state.device == fake_device && event.state.connected && event.state.hat == 1);
    assert(event.state.buttons == 0x13ff && event.state.x == -32768 && event.state.y == 32767);
    assert(event.state.rx == 32767 && event.state.ry == -32767);
    assert(event.state.z == 32767 && event.state.rz == 32767);
    assert(input->next(0, filtered, &event) == 0);
    // Repeated unchanged data emits no extra state transition.
    packet_length = 20;
    assert(input->poll(0, 4) && input->next(0, subscription, &event) == 0);
    const uint8_t hats[] = {8,0,4,8,6,7,5,6,2,1,3,2,8,0,4,8};
    for (uint8_t dpad = 0; dpad < 16; ++dpad) {
        send(dpad, 0); assert(input->poll(0, 4)); count = 1;
        assert(input->snapshot(0, &state, &count) && state.hat == hats[dpad]);
        drain(input, subscription);
    }
    send(0xf0, 0xf7); packet[4] = packet[5] = 128;
    assert(input->poll(0, 4)); count = 1;
    assert(input->snapshot(0, &state, &count) && state.buttons == 0x1fff);
    drain(input, subscription);
    for (unsigned i = 0; i < 40; ++i) {
        send(0, i & 1 ? 0x20 : 0); assert(input->poll(0, 4));
    }
    assert(input->next(0, subscription, &event) == -1);
    count = 1;
    assert(input->snapshot(0, &state, &count) && state.buttons == 2);
    fake_device = 0;
    assert(input->poll(0, 4) && releases == 1);
    assert(input->next(0, subscription, &event) == 1 && event.kind == 2);
    assert(!event.state.connected && !event.state.buttons && !event.state.x && event.state.hat == 8);
    count = 1; assert(input->snapshot(0, &state, &count) && count == 0);
    fake_device = 52; fake_pid = 0x0719; // Xbox wireless receiver is another protocol.
    assert(input->poll(0, 4) && claims == 1);
    fake_device = 53; fake_pid = 0x028e; config[32] = 0x81;
    assert(input->poll(0, 4) && claims == 1); // Reject wireless protocol 0x81.
    fake_device = 54; config[32] = 1; config[37] = 2;
    assert(input->poll(0, 4) && claims == 1); // Reject bulk instead of interrupt.
    fake_device = 55; config[37] = 3; config[2] = 53;
    assert(input->poll(0, 4) && claims == 1); // Reject truncated/mismatched configuration.
    fake_device = 56; config[2] = 54; reject_claim = true;
    assert(input->poll(0, 4) && claims == 2);
    char reason[64]; assert(diagnostic(0, reason, sizeof(reason)));
    assert(!strcmp(reason, "XINPUT INTERFACE CLAIM FAILED"));
    assert(input->poll(0, 4) && claims == 2); // Failed attachment never busy-retries claims.
    fake_device = 57; reject_claim = false; send(0, 0x10);
    assert(input->poll(0, 4) && claims == 3);
    assert(input->next(0, subscription, &event) == 1 && event.kind == 1 && event.state.device == 57);
    drain(input, subscription);
    packet_length = -1;
    assert(!input->poll(0, 4));
    assert(input->next(0, subscription, &event) == 1 && event.kind == 2 && !event.state.buttons);
    send(0, 0x20); assert(input->poll(0, 4));
    assert(input->next(0, subscription, &event) == 1 && event.kind == 1 && event.state.buttons == 2);
    assert(!driver->quiesce());
    assert(input->unsubscribe(0, subscription) && input->unsubscribe(0, filtered));
    assert(driver->quiesce() && releases == 2); // Still-plugged shutdown.
    driver->stop();
    assert(driver->start(&dependency, 1));
    uint64_t fresh = input->subscribe(0, 0); assert(fresh && fresh != subscription);
    assert(input->next(0, subscription, &event) == -1);
    assert(input->unsubscribe(0, fresh)); driver->stop();
    puts("XInput composite discovery, packet validation, mapping, overflow, hotplug and shutdown: PASS");
}
