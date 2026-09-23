# Firmware navigation and application input handoff

Governed by the [Platform Specification](RISCRTE_PLATFORM_SPEC.md) and
[hardware ownership contract](HARDWARE_AGNOSTIC_DRIVER_BOUNDARY.md).

## Installation and controls

Install **usb-ui-navigation 0.1.0** and its declared dependencies alongside
firmware containing this integration. It is a separate composite driver that
provides `input.navigation@1`, built and exported through the existing driver
package workflow. It requires the HID keyboard, HID gamepad and XInput gamepad
providers; physical devices of all three kinds need not be connected. The
existing controller/power drivers retain physical USB ownership and their
host-role-before-VBUS startup sequence.

Automatic charging/computer-serial switching additionally requires
**usb-controller-esp32s3 0.1.15** (was 0.1.14) and
**board-power-t5s3-v2 0.1.5** (was 0.1.4), plus the firmware change that keeps
the boot serial service initialized. These versions exceed the v1.2.65 release.
Update both drivers together with navigation Off and no app holding USB grants.
The controller checks the additive power-monitor interface size and rejects an
older power driver with an explicit upgrade diagnostic. Existing consumers of
the original power v1 prefix remain compatible.

Those package names describe the T5S3 installation, not a universal power-chip
requirement. Another board supplies its own `board.power.vbus` provider with
the same monitor contract, using its actual PMIC, GPIO or role detector. The
USB controller and generic firmware do not select a power-chip model.

**Settings → Controls → Keyboard/controller navigation** enables this UI
consumer (default On). Turn it Off to release the UI's provider/package leases
before updating USB drivers, or when external navigation is unwanted. Use touch
or hardware buttons to turn it back On. Apps' independent input grants are not
revoked by this setting. Failed cleanup still retains unsafe packages.

| Input | Navigation |
| --- | --- |
| Keyboard arrows | Move selection; turn reader pages |
| Enter, keypad Enter, Space | Confirm/open |
| Escape | Back |
| Page Up / Page Down | Previous/next reader page |
| Home | Firmware Home gesture |
| Gamepad D-pad or left stick | Move selection; turn reader pages |
| HID Button 1 / Button 2 | Confirm / Back |
| XInput A / B | Confirm / Back |
| Gamepad shoulders (bits 4/5) | Previous/next reader page |

HID button numbers are descriptor-defined, not universal face-button labels.
This default matches the tested SNES-style numbering; a different controller
may label the same buttons differently. Analog directions use a 16000 threshold
on the normalized signed-16-bit axes. Physical buttons and touch remain usable.
External input never synthesizes Power/shutdown.

The firmware attempts to acquire an unambiguous installed `input.navigation`
provider when storage becomes ready. Missing or rejected providers do not cause
repeated SD inventory scans each frame. A USB power/connection change, return
from an app/installer, or wake permits another attempt. Active providers keep
polling for hotplug. An unsafe/quarantined generation is not silently restarted.

## Ownership and handoff

The firmware consumes only the transport-neutral
[`RiscInputNavigationV1.h`](../sdk/driver/RiscInputNavigationV1.h) interface.
[`usb_ui_navigation/driver.c`](../Drivers/usb_ui_navigation/driver.c) implements
USB input policy through class capabilities; no USB enumeration, descriptors,
report decoder or rail control is added to the firmware navigation consumer.

1. The UI holds a navigation-provider grant. Its dependency grants keep the
   existing keyboard/gamepad/host/controller chain available.
2. An app acquires a manifest-declared capability through
   `t5_provider_capability_get_api()->acquire()`. Before returning its interface,
   the bridge reports the foreground claim to the navigation provider. The
   provider unsubscribes the UI keyboard cursor or stops polling the overlapping
   gamepad source. Failure rejects the new grant and rolls back foreground state.
3. The app subscribes/polls normally. GUI navigation does not consume that
   source. Non-overlapping devices remain available through ordinary app UI
   polling. Duplicate grants stay foreground until their last token is released.
4. The app unsubscribes and releases its grant. Loader cleanup also releases
   outstanding grant tokens on app return/error. Navigation resumes the source
   with a fresh UI cursor and a neutral-input guard. The dependency chain remains
   loaded across this handoff, so no deliberate VBUS cycle or reenumeration occurs.

An optional manifest declaration alone does not take input focus. An app such
as Springboard that uses ordinary `T5AppApi.poll()` continues receiving mapped
navigation without acquiring a raw input capability. An app such as Gameboy
that acquires HID/XInput receives raw current-state input through its existing
providers, without duplicate mapped navigation from those same sources.

The provider interprets overlap; the core copies opaque capability names and
generation-qualified tokens. Acquiring `usb.hid` suppresses both HID sources;
acquiring their lower host/controller/power capabilities suppresses all three.
The `input.navigation` interface is the UI adapter contract; applications that
need direct input use the underlying class capabilities instead of sharing the
UI adapter instance.

## Input delivery and lifecycle

Gamepads use bounded polling plus current snapshots, with no button-history
FIFO. Keyboard navigation retains the order of key presses/releases, including
quick taps. The UI cursor is independent of the app's keyboard cursor; focus
handoff never flushes the app's buffered typing. Only navigation keys are mapped;
text entry remains the keyboard consumer's responsibility.

Each provider poll performs at most four ready reports per input class and
consumes at most one keyboard navigation transition, scanning no more than the
existing 32-event queue. The firmware polls at most once per 20 ms owner-loop
interval and preserves held state between polls. Existing menu repeat logic
uses that held state. Rendering/scheduling can make the interval longer; no
separate worker calls provider interfaces concurrently.

App entry/return, focus changes, input gaps and failures clear derived UI state.
The returning source must become neutral before it can trigger navigation. This
prevents a held exit/launch button from activating a menu after handoff. Removal
clears held state without treating unplug as a Confirm release. Navigation
activity resets the firmware inactivity timer.

Before sleep/power-off, the firmware unsubscribes navigation and releases its
grant, then verifies graph-wide quiescence. Failed lower-provider cleanup blocks
sleep and retains unsafe modules/dependencies. Wake reacquires normally. Keyboard
and controller input are not deep-sleep wake sources in this change.

The legacy serial bridge also respects shared provider lifetime: a clean serial
close may leave the host active for another grant, without declaring quarantine.
It keeps the boot Serial/JTAG service initialized and does not call Serial.end()
or Serial.begin() during host sessions. The controller ELF alone switches the
PHY route. Failed serial cleanup still requires verified shutdown before reuse.

## Automatic charging and computer serial priority

The physical controller manages the port role while its API and dependency
grants stay valid. The firmware UI and apps do not write charger registers,
guess the USB role or repeatedly unload/reload drivers.

| Observed state | Provider behavior |
| --- | --- |
| External power at startup or while parked | Leave host uninstalled and its power output off; retain boot serial route and normal charging settings |
| Charger/computer removed | Wait for provider-qualified status, then require two absent-input readings 500 ms apart before the established host-before-VBUS startup sequence |
| Device attached, enumerating, or still claimed | Keep the host intact; no idle power probes or input interruption |
| Empty host on a board requiring source-off observation | After 2 seconds, drain hardware, release VBUS, restore board state and saved boot PHY, then ask the provider for input status |
| Empty host on a board with independent input detection | Read the provider's status without cycling output power; park when it no longer reports our own source |
| Provider reports detection still settling | Keep source off and wait cooperatively, with a generic 10-second liveness budget |
| Unknown power or failed startup | Block unsafe sourcing; at most three failed samples/start attempts before a visible stopped state |
| Cleanup failure | Retain unsafe resources; never announce a successful serial handback or restart the host |

The board-power provider owns all electrical qualification, settling and chip
registers. Its monitor reports absent input, external power, our own source,
verified-source-off/detection-settling, or an unknown/error state. SETTLING is
not counted as a failed read and never authorizes host startup. The 500 ms
consumer interval throttles polling and debounces cable changes; it is not an
assumption about any chip's detection time.

On T5S3, the BQ25896 driver uses its existing I2C capability, implements a 500 ms
qualification margin over the chip's 220 ms delay, and declares
`RISC_USB_POWER_IDLE_PROBE_REQUIRED`. A voltage reading while it is sourcing
cannot reliably distinguish our own output from another source, so only this
declared board limitation requests empty-host source-off probes. Providers with
an independent detector clear the flag and observe without those power cycles.
The legacy Board::isUsbConnected() boolean includes OTG and is **not** used for
role selection. Active enumeration/devices/claims prevent idle probes.

No computer-versus-charger guess is required: both keep host discovery stopped.
A computer with a data cable can enumerate the restored Serial/JTAG interface;
a charger supplies power without enumeration. The board's shared internal PHY
supports one role at a time. This is direct single-port role switching, not
powered-hub, USB-PD or simultaneous host-and-PC support. Charging follows the
existing battery profile and charge limits; a full battery need not draw charge.

The role machine runs from bounded controller polling. It waits by returning to
the owner loop, reads power at most once per 500 ms in normal parked operation,
backs off failed startups, and makes no repeated SD/provider scans. Empty-port
host shutdown verifies claims, interrupt/bulk DMA, callbacks, devices, client,
host and PHY before restoring VBUS/serial state. Its I2C dependency stays claimed
for observation and is released on actual provider shutdown. Physical detach
flows through the existing device events and class cleanup, preserving keyboard
event order and gamepad current-state semantics. All observations depend on the
consumer continuing to poll; rendering or long app work can delay transitions.

Role changes and failures appear through the existing controller diagnostic
interface (including Gameboy's test screen) and bounded transition logs. After
a stopped failure, close USB-using apps and toggle navigation Off/On to retry;
unverified cleanup continues to block restart.

## Verification

`bash test/run_usb_hid_test.sh` covers the actual adapter and firmware consumer:
keyboard ordering, HID/XInput mappings, focus suppression, duplicate grants,
failure rollback, held-input handback, unplug, absent-provider scan bounds and
sleep cleanup. Existing keyboard/text-editor and controller DMA tests run in
the same suite. The production role-machine test covers external power at boot,
debounced unplug, active-device protection, empty-host probing, reconnect,
failed cleanup, bounded retries and tick rollover.
The same production role machine is also tested with a slow detector and an
independent detector that does not require source-off probes; these are interface
simulations, not claims of support or hardware testing for a particular new chip.
`test/run_board_power_t5s3_v2_test.sh` exercises the real chip-owner implementation
with external/input/source status, charge restoration and repeated power leases.
USB packaging and target-build workflows include the new ELF.

Hardware acceptance still requires installing the new firmware and navigation
package: navigate Home/settings, open an app that requests input, exit with a
button held, and verify navigation returns after release without replugging.
Check both keyboard typing and gamepad current-state behavior. These scenarios
are validation guidance, not claims that new hardware testing has occurred.
Also boot with a charger and with a computer, swap a controller for each while
the UI/app stays open, confirm charging/COM-port return, then swap back to the
controller without rebooting. The gamepad test screen should show external-power
mode while connected to the computer; use the single port sequentially.
