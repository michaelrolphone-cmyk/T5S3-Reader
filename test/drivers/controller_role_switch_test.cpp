#include "../../Drivers/usb_controller_esp32s3/RoleSwitch.h"
#include <cassert>
#include <cstdio>

struct Port {
    uint32_t clock = 0;
    int32_t status = RISC_USB_POWER_ABSENT;
    bool occupied = false, parkOK = true, startOK = true, powered = false;
    bool probeRequired = true;
    unsigned starts = 0, parks = 0, reads = 0, reports = 0;
    uint32_t now() const { return clock; }
    bool busy() const { return occupied; }
    bool idle_probe_required() const { return probeRequired; }
    int32_t input() { ++reads; assert(!powered || !probeRequired); return status; }
    bool park() { ++parks; if (parkOK) powered = false; return parkOK; }
    bool start() { ++starts; assert(!powered); powered = true; return startOK; }
    void report(const char *) { ++reports; }
    void advance(UsbRoleSwitch &role, uint32_t ms) { clock += ms; role.poll(*this, clock); }
};
using State = UsbRoleSwitch::State;

int main() {
    UsbRoleSwitch role;
    Port port;
    port.status = RISC_USB_POWER_EXTERNAL;
    role.begin(port.clock);
    // Charger/PC already connected at launch: no PHY takeover or source.
    for (unsigned i = 0; i < 120; ++i) port.advance(role, 500);
    assert(!port.starts && !port.parks && port.reports == 1);
    assert(role.state() == State::Sense);
    // Cable removal is debounced; a bounced external reading resets absence.
    port.status = RISC_USB_POWER_ABSENT;
    port.advance(role, 500); assert(!port.starts);
    port.status = RISC_USB_POWER_EXTERNAL;
    port.advance(role, 500); assert(!port.starts);
    port.status = RISC_USB_POWER_ABSENT;
    port.advance(role, 500); assert(!port.starts);
    port.advance(role, 500); assert(port.starts == 1 && port.powered);
    // Attached/enumerating device, pending detach event or outstanding claim
    // keeps the host intact. No power polling/probing during active gameplay.
    port.occupied = true;
    const auto reads = port.reads;
    for (unsigned i = 0; i < 3600; ++i) port.advance(role, 1000);
    assert(!port.parks && port.starts == 1 && reads == port.reads);
    port.occupied = false;
    port.advance(role, 1999); assert(!port.parks);
    port.advance(role, 1); assert(port.parks == 1 && !port.powered);
    // Source-off window discovers a PC which appeared while we supplied VBUS.
    port.status = RISC_USB_POWER_EXTERNAL;
    port.advance(role, 499); assert(port.reads == reads);
    port.advance(role, 1); assert(port.reads == reads + 1);
    for (unsigned i = 0; i < 120; ++i) port.advance(role, 500);
    assert(port.starts == 1 && !port.powered);
    // A receiver plugged after that PC is removed starts without reloading
    // the provider or invalidating its consumer's API/grants.
    port.status = RISC_USB_POWER_ABSENT;
    port.advance(role, 500); port.advance(role, 500);
    assert(port.starts == 2 && role.state() == State::Host);
    // Failed physical cleanup never restarts the host or exposes serial.
    port.parkOK = false;
    port.advance(role, 2000);
    assert(role.state() == State::Failed && port.powered);
    auto starts = port.starts;
    auto parks = port.parks;
    for (unsigned i = 0; i < 100; ++i) port.advance(role, 1000);
    assert(port.starts == starts && port.parks == parks);

    Port unknown;
    unknown.status = RISC_USB_POWER_UNKNOWN;
    role.begin(unknown.clock);
    for (unsigned i = 0; i < 10; ++i) unknown.advance(role, 500);
    assert(role.state() == State::Failed && unknown.reads == 3 && !unknown.starts);

    // Different board implementation: detector takes longer than the T5S3.
    // The controller waits for the provider instead of applying a chip delay
    // or treating legitimate settling as three failed I2C reads.
    Port slow;
    slow.status = RISC_USB_POWER_SETTLING;
    role.begin(slow.clock);
    for (unsigned i = 0; i < 8; ++i) slow.advance(role, 500);
    assert(role.state() == State::Sense && !slow.starts);
    slow.status = RISC_USB_POWER_ABSENT;
    slow.advance(role, 500); slow.advance(role, 500);
    assert(slow.starts == 1);

    Port stalled;
    stalled.status = RISC_USB_POWER_SETTLING;
    role.begin(stalled.clock);
    for (unsigned i = 0; i < 30; ++i) stalled.advance(role, 500);
    assert(role.state() == State::Failed && !stalled.starts);

    // Another board has an independent role/VBUS detector: observe it while
    // an empty host is powered, without periodic source-off probing.
    Port independent;
    independent.probeRequired = false;
    role.begin(independent.clock);
    independent.advance(role, 500); independent.advance(role, 500);
    assert(independent.starts == 1);
    independent.status = RISC_USB_POWER_SOURCE;
    for (unsigned i = 0; i < 60; ++i) independent.advance(role, 500);
    assert(independent.powered && !independent.parks);
    independent.status = RISC_USB_POWER_EXTERNAL;
    independent.advance(role, 500);
    assert(!independent.powered && independent.parks == 1);
    for (unsigned i = 0; i < 60; ++i) independent.advance(role, 500);
    assert(independent.starts == 1 && role.state() == State::Sense);

    Port failed;
    failed.startOK = false;
    role.begin(failed.clock);
    for (unsigned i = 0; i < 60; ++i) failed.advance(role, 500);
    assert(role.state() == State::Failed && failed.starts == 3 && failed.parks == 3);
    assert(!failed.powered);
    // Tick rollover retains intervals, and a disabled role does no work.
    Port wrap;
    wrap.clock = UINT32_MAX - 400;
    role.begin(wrap.clock);
    wrap.advance(role, 500); wrap.advance(role, 500);
    assert(wrap.starts == 1);
    role.stop();
    wrap.advance(role, 10000); assert(!wrap.parks);
    puts("USB charging/serial role switching: PASS");
}
