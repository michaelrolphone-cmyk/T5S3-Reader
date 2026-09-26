# USB host startup, power and device detection

## Contract and scope

Read the [Platform Specification](RISCRTE_PLATFORM_SPEC.md) and
[hardware ownership contract](HARDWARE_AGNOSTIC_DRIVER_BOUNDARY.md). This is the
implementation guide for USB controller, power, host and class driver work.
The ordering and ownership rules below are requirements for future drivers;
the concrete calls describe the ESP32-S3 internal PHY implementation.

**Configure the host role and D+/D− host pull-downs before supplying VBUS to an
already-connected device. Prepare the host event client and transfer resources
before allowing connection detection.** Dependency activation, power acquisition,
physical attachment, enumeration, class binding and usable input are separate
states. Neither a successful capability grant nor a lit receiver proves USB
enumeration.

Controller/PHY operations belong to the physical controller ELF, rail operations
to the board power ELF, and interface/report handling to class ELFs. The generic
runtime resolves dependencies and supervises lifetimes. Applications must not
repair missing attachment by manipulating the PHY, charger or boot console.

## Confirmed implementation and incident

On September 23, 2026, the owner confirmed that the change in
[PR #139](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/139),
`usb-controller-esp32s3` **0.1.14**, fixed the reported requirement to unplug and
replug the controller after launching Gameboy. The earlier failing trace showed
`devices=0`, `VID=0000`, `PID=0000` and `NO ATTACH; NO ENUM EVENT` for about a
minute. Replugging then reached descriptor enumeration and a HID gamepad layout
for **0314:1809**. This confirmation covers the reported auto-connect case on
the owner's hardware; it is not qualification of every USB device or lifecycle
case below.

RiscRTE's [build configuration](../platformio.ini) enables USB Serial/JTAG at
boot (`ARDUINO_USB_MODE=1`, `ARDUINO_USB_CDC_ON_BOOT=1`). The controller takes
over the shared internal PHY from that device role. The previous startup
acquired VBUS first, so an attached receiver powered up before host bus
conditions existed. Moving the host preparation ahead of VBUS acquisition
resolved the observed failure. There was no captured electrical waveform, so
do not invent a receiver-internal failure mechanism beyond that evidence.

## Startup sequence

The authoritative implementation is
[`driver_base.cpp`](../Drivers/usb_controller_esp32s3/driver_base.cpp) `start()`,
[`HostStartup.h`](../Drivers/usb_controller_esp32s3/HostStartup.h)
`start_host_controller()`, and
[`PhyRoute.h`](../Drivers/usb_controller_esp32s3/PhyRoute.h).

Controller **0.1.15** adds automatic role selection in
[`RoleSwitch.h`](../Drivers/usb_controller_esp32s3/RoleSwitch.h). Provider
`start()` initially parks the host and polls the power-monitor extension.
External input keeps our source off and the boot Serial/JTAG route available;
settling or unknown readings never authorize sourcing. Only qualified absent
input permits the physical startup sequence below. Active attachment,
enumeration or claims prevent idle source-off probes. See
[Firmware Input Navigation](FIRMWARE_INPUT_NAVIGATION.md) for role switching
and UI/application handoff; handoff alone does not restart the physical host.

| Order | Operation | Required result before advancing |
| --- | --- | --- |
| 1 | Validate stopped state and `board.power.vbus@1` dependency, API size/version and callbacks. | No retained controller resources or fault permitting unsafe reentry. Save the API pointer; **do not acquire VBUS yet**. |
| 2 | Capture `RTCCNTL.usb_conf.sw_hw_usb_phy_sel` and `sw_usb_phy_sel`. | Preserve the previous software/automatic PHY route for cleanup. |
| 3 | Create PHY with `USB_PHY_CTRL_OTG`, `USB_PHY_TARGET_INT`, `USB_OTG_MODE_HOST`, `USB_PHY_SPEED_UNDEFINED`. | Internal PHY routed to OTG, host pull-downs configured. Host detects device speed. |
| 4 | Apply `USB_PHY_ACTION_HOST_FORCE_DISCONN`. | Hold the controller's receive-side attachment detector disconnected during preparation. |
| 5 | Install host with `skip_phy_setup=true` and `ESP_INTR_FLAG_LOWMED`. | The ELF retains explicit PHY ownership; the host does not create a second PHY owner. Do not share the interrupt to bypass contention. |
| 6 | Register the asynchronous host client/callback and allocate control/bulk DMA workspace. | Event handling and transfer storage exist before a device can enumerate. Interrupt-IN storage is allocated later for claimed endpoints. |
| 7 | `vTaskDelay(pdMS_TO_TICKS(20))`, with a minimum of one tick. | Bounded scheduler handoff settles the host-role transition while the receiver is still unpowered by this provider. |
| 8 | `power->acquire_host(power->context, 500, &powerLease)`. | Require success **and a nonzero lease**. The argument is **500 milliamps, not a 500 ms timeout**. |
| 9 | Apply `USB_PHY_ACTION_HOST_ALLOW_CONN`. | Restore real received line signals only after successful power acquisition. |
| 10 | Mark running and service bounded provider polls. | Host library events and client callbacks progress through attachment and enumeration. |

The 20 ms handoff is this implementation's setting, not a universal USB-device
delay or a substitute for the ordering invariant. A new controller/board port
must establish its own role, pin, power and settling requirements.

[`bq25896/driver.c`](../Drivers/bq25896/driver.c) owns the BQ25896 register
protocol, source admission, external-power/fault checks, rail configuration,
bounded settling, measured-source verification and rollback. Its separate
installed [electrical profile](BQ25896_USB_POWER_PROFILES.md) supplies board
settings; chip identity does not imply T5S3 wiring. Other chips implement the
same power-monitor contract, and electrical qualification stays in that provider. Resolving this dependency
is different from calling `acquire_host()`. Keep those operations separate;
dependency startup must not secretly power a receiver ahead of host preparation.
Do not bypass a rejected power request or adjust charger policy to conceal an
enumeration defect. See the lease contract in
[`RiscUsbVbusV1.h`](../sdk/driver/RiscUsbVbusV1.h).

In the pinned [ESP-IDF 4.4.7 PHY implementation](https://github.com/espressif/esp-idf/blob/v4.4.7/components/usb/usb_phy.c),
`HOST_FORCE_DISCONN` masks the host's received connection signals. It does not
power-cycle the receiver and cannot undo bus conditions the receiver already
saw. `HOST_ALLOW_CONN` is consequently the last startup step, not a replacement
for establishing host mode before VBUS.

## Detection: identify the first incomplete layer

The controller's bundled IDF host stack handles root attachment, debounce/reset,
addressing and descriptors/configuration. Its client receives `NEW_DEV` only
after enumeration. [`usb_host_v2`](../Drivers/usb_host_v2/driver.c) then exposes
generation-qualified devices and interface claims. HID and XInput providers
discover supported interfaces, bind their protocol/layout and receive reports.
Class discovery must also inspect the existing host snapshot when starting;
listening only for subsequent arrival events misses already-enumerated devices.

| Observation | What is established | Investigate next |
| --- | --- | --- |
| Grant/subscription succeeds; waiting for connection | Software provider access exists. | Physical controller startup and host snapshot. |
| `EXTERNAL POWER; USB SERIAL AVAILABLE` | The provider reports incoming power and the host is parked. | Expected charging/computer mode; no host enumeration should be attempted. |
| `SOURCE OFF; CHECKING USB INPUT POWER` | The host is waiting for qualified power observations. | Power-provider settling/status and continued owner polling. |
| Startup fails at `vbus-acquire` | Host preparation reached power acquisition. | Board power diagnostic, return value and lease ownership; do not assume a timeout. |
| `PORT OFF` | HPRT port-power bit is clear. | Controller lifecycle and power-provider state. This bit is not an external VBUS voltage measurement. |
| `NO ATTACH; NO ENUM EVENT`, zero devices | No root attachment/enumeration has been observed. | PHY route, host pull-downs, role-before-VBUS ordering, event pumping, cable/physical connection. Class retries and button decoders cannot create attachment. |
| `ATTACHED; GET_FULL_DEV_DESC` or another enumeration stage | Root connection detected; enumeration is progressing. | Reset/control transfer/descriptor stage and its first failure. |
| `ENUM FAIL: Root port reset failed` | That attachment attempt reached reset and failed. | Timestamp and attachment generation. It does not explain an earlier interval with no attach event. |
| Nonzero device count and VID/PID, but no class binding | Physical USB enumeration completed. | Configuration/interface descriptors, alternate setting, endpoint type, claim ownership and supported class protocol. |
| `GAMEPAD INPUT LAYOUT CONNECTED` or `XINPUT WAITING FOR REPORT` | A layout/interface or protocol session is available. | First valid report, endpoint polling and protocol decoding. Binding is not proof that input arrived. |
| Reports arrive but input lags or sticks | The failure is beyond initial enumeration. | Poll scheduling, pending transfers, state publication and consumer behavior. |
| UI flashes with an empty port | A refresh may be software-requested; this is not proof of rail instability. | Incoming-power telemetry must exclude our own OTG output. See the [1.2.66 idle regression](FIRMWARE_INPUT_NAVIGATION.md#firmware-idle-regression-after-1266). |
| `TG1WDT_SYS_RST` while external-power mode is parked | A watchdog reset occurred even without host startup. | CPU/peripheral clock transitions and interrupt stalls; do not attribute this solely to host power probes. |

VID/PID alone does not select HID versus XInput. The original Windows report
contained **045e:028e**; the later RiscRTE trace enumerated **0314:1809** as HID.
Use the actual configuration/interface descriptors. The current XInput provider
matches Xbox 360 interface class/subclass/protocol `ff/5d/01` or `ff/5d/81`;
HID uses its interface and report descriptors. Do not apply HID requests to an
XInput vendor interface. See [USB HID and XInput providers](USB_HID_ELF.md).

### Diagnostics without the USB serial connector

Expose bounded, copied diagnostics through the size-checked optional suffix in
[`RiscUsbDiscoveryDiagnosticsV1.h`](../sdk/driver/RiscUsbDiscoveryDiagnosticsV1.h).
Consumers must check `struct_size` before using it; older API prefixes remain
valid. [EnumerationDiagnostic.h](../Drivers/usb_controller_esp32s3/EnumerationDiagnostic.h)
retains the first enumeration error for each physical attachment. The build's
[`instrument_usb_enumeration.py`](../scripts/instrument_usb_enumeration.py)
observes a private copy of pinned IDF source; it must not change enumeration
decisions or modify the shared SDK cache.

Version 0.1.14 can report `NO ATTACH; NO ENUM EVENT PHY=OTG PD=11` through the
existing Gameboy test screen and rolling SD trace:

- `PHY=OTG`: software mux selects OTG, internal PHY and enabled pads.
  `JTAG`, `EXT`, `OFF` identify other selections/pad state. `AUTO` means
  hardware/eFuse selection; it does not prove which controller owns the route.
- `PD=11`: both configured pull-down bits are set with pad override enabled.
  These are **configuration bits, not measured D+/D− levels or VBUS voltage**.
- PHY details are appended when no enumeration failure is retained, preserving
  space for the failure text. The no-attach message fits the existing 48-byte
  stage field; keep diagnostics bounded.

Use timestamps and attachment generations to separate current status from
historical failures. An application may clear an obsolete enumeration warning
after confirmed enumeration while retaining it in the trace; do not erase
current class/transfer errors. A zero report count while idle is not by itself
failure. On the single connector, prefer the test screen and bounded SD logs;
requiring the owner to replace the controller with a serial cable loses the
state being diagnosed.

## Polling and input semantics

Provider calls and callbacks run through the serialized owner. A capability
being active does not service host events by itself: bounded polling must pump
both the host library and client events. Follow
[Bounded Cooperative Operations](COOPERATIVE_BOUNDED_OPERATIONS.md): cap work
and elapsed time, return between retries, and actually yield the owner loop.
Feeding the watchdog is not a scheduler yield. Do not add recursive discovery,
an unbounded drain or one blocking wait per quiet endpoint/report.

[`PendingInterrupt.h`](../Drivers/usb_controller_esp32s3/PendingInterrupt.h)
keeps controller-owned DMA armed across polls. No ready report returns zero
without waiting for future input. A completed report is copied and the endpoint
rearmed before returning, so another report can arrive while the consumer runs.
One pending/completed transfer is transport storage, not permission to replay
button history or retain an app buffer. A polling deadline never cancels DMA.

| Consumer | Required publication contract |
| --- | --- |
| HID/XInput gamepad | Poll bounded ready work, then read the latest per-device `snapshot()`. Replace buttons/axes with current state. Compatibility `next()` notifications coalesce per device through [StateMailbox.h](../Drivers/usb_hid_gamepad/StateMailbox.h); never accumulate a button-transition FIFO. Disconnect clears held state. |
| Keyboard/text editor | Preserve ordered press/release events in bounded per-subscriber queues, including quick taps between consumer reads. Surface overflow/gaps explicitly and resnapshot to restore held-key state; a snapshot cannot reconstruct lost text. Never apply gamepad coalescing to keyboard events. |

## Shutdown and failed startup

Run the same ownership-aware cleanup after partial startup and normal shutdown.
The implementation is `quiesce_with_interrupt()` in
[`driver.cpp`](../Drivers/usb_controller_esp32s3/driver.cpp), then `quiesce()`
and its physical `quiesce_host()` helper in
[`driver_base.cpp`](../Drivers/usb_controller_esp32s3/driver_base.cpp). Idle
role parking uses the same interrupt drain and physical helper while retaining
provider APIs and lower dependencies for continued power observation.

1. End consumer subscriptions/admission and release class claims. Drain/flush
   outstanding interrupt and control/bulk transfers with bounded event pumping.
   Callback completion must return DMA ownership before buffers can be freed.
   Outstanding claims or uncertain drains prevent controller unload.
2. Close device handles, free transfer storage and deregister the client,
   clearing each owned handle only after that operation succeeds.
3. Request `usb_host_device_free_all()` and boundedly service library events
   until all devices are free. **Always perform the final
   `usb_host_lib_handle_events(0, ...)` before uninstall**, including when no
   device ever attached and `device_free_all()` returned `ESP_OK`. IDF 4.4.7
   still has the last client's `NO_CLIENTS` event to consume.
4. Uninstall the host. Only then delete the explicitly owned PHY. Failed host
   uninstall or PHY deletion retains ownership and any controller-held VBUS lease.
5. Release the controller's VBUS lease through the power provider. A failed
   release retains the lease and dependency; do not claim shutdown succeeded.
6. After host, PHY and the controller's VBUS lease are released and the power
   monitor verifies source-off, restore the captured mux selection, including
   the automatic-selection case and a rejected startup. For actual provider
   shutdown, finish with the power provider's `quiesce()`; it must release its
   lower bus claim before the dependency chain can unload. Idle role parking
   retains that dependency and bus claim to continue observing input power.

Power acquisition failure needs two distinct checks. The VBUS API requires a
false return to expose no lease; its provider must internally retain an unsafe
partially applied transition and refuse quiescence. The controller additionally
defends against a false return with a nonzero lease: retain it and use normal
cleanup. A true return without a lease is rejected. **Zero in the caller's lease
variable is not proof that the power provider is safe to unload.** Likewise,
restoring the mux alone is not proof that the whole dependency chain quiesced.
Successful release means the board stopped sourcing; external VBUS may remain.

Do not force-unmap a faulted provider, release dependencies under live callbacks,
or repeatedly reset/power-cycle hardware to hide retained ownership. Retry
cleanup only under the existing bounded lifecycle rules.

## Earlier approaches and why they did not solve startup

| Change or assumption | Lesson for future implementations |
| --- | --- |
| [PR #137](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/137): bounded XInput configuration/claim retries | Useful after host enumeration. Cannot fix a zero-device root port with no attach event. |
| [PR #138](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/138), controller 0.1.13: explicit PHY and detector barrier | VBUS was still acquired before entering the host startup helper. Masking the local detector after receiver power-up left the ordering defect intact. Passing helper tests did not establish the order at the power-provider boundary. |
| [PR #139](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/139), controller 0.1.14 | Put actual VBUS acquisition inside the tested sequence, after host preparation and before allowing detection. Owner confirmed auto-connect works. |
| Legacy serial path already appeared to hand over USB correctly | Before controller 0.1.15, the serial bridge ended the boot console before acquiring providers, while direct HID/XInput acquisition did not traverse that helper. Current [NativeUsbBridge.cpp](../src/native/NativeUsbBridge.cpp) leaves the boot service initialized and the controller ELF owns PHY handback. Correctness belongs in that ELF for every acquisition path. |
| Working standalone firmware implies the ELF path is equivalent | Compare boot USB role, physical power timing, event execution and scheduling as well as class decoding. The same decoder cannot compensate for a different controller startup sequence. |

## Focused regression coverage for future changes

Keep checks at the boundary that failed. Extend existing tests for an actual
change rather than creating a new blanket qualification gate.

| Existing check | What it protects |
| --- | --- |
| [controller_host_startup_test.cpp](../test/drivers/controller_host_startup_test.cpp), included in [run_provider_graph_v2_test.sh](../test/run_provider_graph_v2_test.sh) | Production startup/route helpers: host role, client, DMA and scheduler handoff before the **first power call**; preattached/absent device; startup failures, invalid/retained leases, PHY-delete failure and route restoration. Fakes do not model receiver electronics. |
| [controller_role_switch_test.cpp](../test/drivers/controller_role_switch_test.cpp) and [run_board_power_t5s3_v2_test.sh](../test/run_board_power_t5s3_v2_test.sh) | Qualified power observations, external-power parking, active-device protection, bounded retries and real chip/profile policy with simulated hardware. |
| [controller_startup_diagnostic_test.cpp](../test/drivers/controller_startup_diagnostic_test.cpp) | Retained first errors, bounded formatting and PHY status fitting the consumer field. |
| [usb_teardown_retry_test.py](../test/drivers/usb_teardown_retry_test.py) and [usb_package_test.py](../test/drivers/usb_package_test.py) | Ownership/cleanup wiring, bounded teardown and final no-client event pumping. |
| [run_usb_hid_test.sh](../test/run_usb_hid_test.sh) | HID/XInput parsing, current gamepad state, ordered keyboard events and shutdown while devices remain attached. |
| [USB ELF workflow](../.github/workflows/usb-elf-provider-v2.yml) | Target compilation, imports/relocations and installable package checks; these do not prove physical attachment. |

When hardware validation is requested for a changed path, include the applicable
cases together: receiver present before launch; attach after launch; unplug while
buttons are held; reconnect with a fresh identity; exit/relaunch with receiver
left plugged in; HID and XInput devices where available; ordered keyboard taps;
quiet endpoints and slow consumers; rejected startup and failed cleanup; and
boot-console handback after safe shutdown. Do not mark these all passed based
on the single confirmed auto-connect case, or demand a new owner test between
each development commit.

Before publishing driver changes, preserve stable package IDs and compatible
API prefixes, size-check optional callbacks, and bump each changed distributable
package above its prior published version. Verify installed ELF/manifest/catalog
versions match. Documentation-only edits need no package bump. Use a fresh PR
from current master after earlier work merges; record the exact source version,
checks and hardware evidence separately.
