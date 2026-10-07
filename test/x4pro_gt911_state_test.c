#include "RiscI2cBusV1.h"
#include "RiscPlatformClockV1.h"
#include "RiscTouchV1.h"
#include "x4pro_pins.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

extern const risc_driver_v2 *t5_driver_get(uint32_t);
static bool pins[49];
static uint64_t now;
static uint8_t status, point[8];
static uint8_t claimed;

void fake_pin_output(uint32_t pin, bool level) { assert(pin < 49); pins[pin] = level; }
void fake_pin_level(uint32_t pin, bool level) { fake_pin_output(pin, level); }
void fake_pin_input(uint32_t pin, bool pullup) { (void)pin; (void)pullup; }
static uint64_t tick(void *ctx) { (void)ctx; return now; }
static void sleep_ms(void *ctx, uint32_t ms) { (void)ctx; now += ms; }
static bool claim(void *ctx, uint8_t addr, uint64_t *out) {
    (void)ctx;
    if (addr != X4PRO_I2C_GT911 || claimed) return false;
    claimed = addr; *out = 1; return true;
}
static bool release(void *ctx, uint64_t handle) {
    (void)ctx;
    if (handle != 1 || !claimed) return false;
    claimed = 0; return true;
}
static bool transact(void *ctx, uint64_t handle, const uint8_t *write, size_t wlen,
                     uint8_t *read, size_t rlen, uint32_t timeout) {
    (void)ctx; (void)timeout;
    if (handle != 1 || !claimed || wlen < 2) return false;
    const uint16_t reg = (uint16_t)((uint16_t)write[0] << 8 | write[1]);
    if (rlen) {
        if (reg == 0x8140 && rlen == 4) { memcpy(read, "911\0", 4); return true; }
        if (reg == 0x814e && rlen == 1) { read[0] = status; return true; }
        if (reg == 0x8150 && rlen == 8) { memcpy(read, point, 8); return true; }
        return false;
    }
    if (wlen == 3 && reg == 0x814e && write[2] == 0) { status = 0; return true; }
    return false;
}
int main(void) {
    const risc_i2c_bus_api_v1 bus = {RISC_I2C_BUS_API_V1, sizeof(bus), 0, claim, transact, release};
    const risc_platform_clock_api_v1 clock = {RISC_PLATFORM_CLOCK_API_V1, sizeof(clock), 0, tick, sleep_ms};
    const risc_provider_dependency_v1 deps[] = {{"i2c.bus", 1, &bus}, {"platform.clock", 1, &clock}};
    const risc_driver_v2 *driver = t5_driver_get(RISC_PROVIDER_DRIVER_ABI_V2);
    assert(driver && driver->start(deps, 2));
    assert(pins[X4PRO_PIN_PERIPH_EN] && !pins[X4PRO_PIN_TOUCH_PWR]);
    const risc_touch_api_v1 *touch = driver->capability;
    assert(touch && touch->api_version == 1);
    const uint64_t sub = touch->subscribe(0);
    assert(sub && !touch->subscribe(0));
    point[0] = 37; point[2] = 140;
    status = 0x81;
    assert(touch->poll(0, 1));
    risc_touch_event_v1 event = {0};
    assert(touch->next(0, sub, &event) == 1 && event.kind == RISC_TOUCH_EVENT_DOWN);
    assert(event.x == 37 && event.y == 140);
    status = 0x80;
    assert(touch->poll(0, 1));
    assert(touch->next(0, sub, &event) == 1 && event.kind == RISC_TOUCH_EVENT_UP);
    assert(touch->next(0, sub, &event) == 0);
    status = 0x90;
    assert(touch->poll(0, 1));
    assert(touch->next(0, sub, &event) == 1 && event.kind == RISC_TOUCH_EVENT_BUTTON_DOWN);
    risc_touch_snapshot_v1 snap = {0};
    assert(touch->snapshot(0, &snap) && snap.width == 480 && snap.height == 800);
    status = 0x82;
    assert(!touch->poll(0, 1) && touch->next(0, sub, &event) == -1);
    assert(touch->snapshot(0, &snap) && snap.contact_count == 0);
    assert(touch->unsubscribe(0, sub) && driver->quiesce());
    assert(pins[X4PRO_PIN_TOUCH_PWR]);
    puts("X4 GT911 rail, portrait touch, Home key and gap: PASS");
}
