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
close may leave the host active for another grant, without declaring quarantine
or restoring the boot-console PHY. Failed serial cleanup still requires verified
shutdown before reuse.

## Verification

`bash test/run_usb_hid_test.sh` covers the actual adapter and firmware consumer:
keyboard ordering, HID/XInput mappings, focus suppression, duplicate grants,
failure rollback, held-input handback, unplug, absent-provider scan bounds and
sleep cleanup. Existing keyboard/text-editor and controller DMA tests run in
the same suite. USB packaging and target-build workflows include the new ELF.

Hardware acceptance still requires installing the new firmware and navigation
package: navigate Home/settings, open an app that requests input, exit with a
button held, and verify navigation returns after release without replugging.
Check both keyboard typing and gamepad current-state behavior. These scenarios
are validation guidance, not claims that new hardware testing has occurred.
