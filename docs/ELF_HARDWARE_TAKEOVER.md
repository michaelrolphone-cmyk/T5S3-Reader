# Native ELF hardware takeover (short-term compatibility interface)

Some native ELFs contain their own complete hardware driver and must temporarily
replace RiscRTE's display implementation. They can opt into an exclusive resource
handoff **without** implementing the long-term RiscRTE provider/lease architecture.
This is not specific to GameBoy or to a particular application manifest.

## Opt in

Include `lib/NativeApps/include/T5HardwareTakeover.h` in an ELF build, and
export a function with the following exact C name and signature:

```c
#include <T5HardwareTakeover.h>

__attribute__((visibility("default")))
uint32_t app_hardware_takeover(void) {
    return T5_HARDWARE_TAKEOVER_DISPLAY;
}

__attribute__((visibility("default")))
void app_main(void) {
    /* Initialize, run and completely stop the app's display driver here. */
}
```

No export means no takeover: all existing apps retain their normal behavior.
This function MUST only declare a resource mask, not touch hardware. The
currently supported mask is `T5_HARDWARE_TAKEOVER_DISPLAY`; unknown bits fail
closed. The T5S3 display has the implementation; other board targets reject
exclusive display requests instead of pretending to release their hardware.

## Actual launcher sequence

1. RiscRTE validates dependencies and `dlopen()` maps the ELF. It verifies
   `app_main` before attempting hardware handoff.
2. If the optional declaration requests display ownership, the host (with
   `RenderLock` already held) waits for M5GFX transfers to finish, turns off
   the panel drive, explicitly deletes its LCD panel-I/O handle and i80 bus,
   and destroys its display backend. It retains the SD mount, logical display
   framebuffers, touch subsystem and firmware renderer object.
3. `app_main()` runs. The ELF may initialize and control its own display
   driver; it must not use the firmware's `t5_app_api` drawing/present functions
   while it owns the physical panel.
4. **Before returning**, the ELF stops/joins all tasks using its executable
   code, unregisters callbacks/interrupts and completes DMA, powers down its
   display driver, and deletes its panel-I/O and i80 bus handles. Returning
   with those resources still owned is a programming error; firmware cannot
   safely reclaim an in-flight third-party DMA callback after the ELF unloads.
5. RiscRTE reinitializes its original display backend, requests a full
   refresh, releases the ELF's remaining capabilities and performs `dlclose()`.
   The calling activity subsequently redraws its UI.

The RiscRTE ELF launcher is synchronous and supports one app at a time. An ELF
crash or abrupt task termination is **not** recoverable by the normal return
path; the contract covers orderly returns only. The display handoff does not
relinquish other board resources such as SD, shared I2C, touch or battery.

## Safety / compatibility

RiscRTE's pinned M5GFX `Bus_EPD` implementation inherits a no-op `release()`.
`HalDisplay.cpp` adds real, ordered teardown on this board: wait for DMA,
turn off panel power, delete panel I/O, then delete the i80 bus. If teardown
fails, the launcher refuses to run the requesting ELF. Firmware display
reinitialization failures are surfaced as launcher errors. The first refresh
following restoration is forced full to avoid assuming any retained pixel state.

This opt-in exists to support complete hardware-owning application ports now.
It is not an installable-driver ABI, generalized privilege sandbox, or a grant
of ownership of the entire MCU. Additional resource bits require matching
explicit teardown/restoration implementations, not just another mask value.
