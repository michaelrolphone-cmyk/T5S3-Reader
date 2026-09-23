# USB HID ELF drivers

## Capability composition

These are independently installable ABI-v2 providers. The runtime only registers
opaque capability names and executes the ordinary dependency lifecycle; it does
not enumerate HID usages, decode reports, open physical endpoints, or implement
USB-specific dispatch. The hardware-specific operations remain inside the
physical controller ELF.

```
board.power.vbus
    -> usb.controller  (ESP32-S3 physical controller ELF; interrupt IN)
        -> usb.host   (USB discovery, interface claims, endpoint arbitration)
            -> usb.hid  (HID descriptor discovery, class control, report reads)
                -> usb.hid.keyboard  (boot keyboard events)
                -> usb.hid.gamepad   (report-descriptor gamepad state)
```

Canonical capability identifiers are **lowercase**, matching package manifests
and provider registry conventions. `USB.hid` in the design brief refers to
`usb.hid` in runtime calls.

The ABI is `sdk/driver/RiscUsbHidV1.h`. Both classes publish `api_version=1` and
`struct_size`; consumers verify those fields before accessing callbacks. The
base `usb.controller` and `usb.host` APIs retain their v1 prefixes;
`RiscUsbInterruptV1.h` appends a claimed interrupt-IN operation and requires
`struct_size >= sizeof(the extended table)` for HID providers. Updating the host
and physical controller ELFs together is necessary; an old controller fails
closed when the new host requests interrupt I/O.

## Input subscriptions

A consumer with an admitted execution-context-owned lease for
`usb.hid.keyboard` or `usb.hid.gamepad` can obtain the versioned capability
interface from its provider grant. Do not cache the API pointer after its
lease is released. These APIs use the normal provider registry; no HID-only
firmware dispatcher is introduced. Integration into a particular app must
also comply with that app's execution-context and capability-grant rules.

Use `subscribe(api->context, 0)` to observe all devices of the selected class
or `subscribe(api->context, device_id)` for one host-generation-qualified
USB device. The return value is a nonzero subscription handle or zero on
capacity exhaustion. Use `poll(api->context, max_reports)` to advance input;
then call `next(api->context, subscription, &event)` until it returns zero
(empty). A result of `1` contains an event. A negative result means an invalid
handle or queue discontinuity: discard derived state and call `snapshot()`.
Each subscriber has a bounded independent queue. Keyboard and gamepad have
the same subscribe/unsubscribe/poll/next/snapshot shape and distinct events.

Keyboard events use USB HID usage page 0x07, **not ASCII**. Kinds are connected=1,
disconnected=2, press=3, release=4; modifiers use the boot keyboard bitset.
Disconnect emits key releases before the removal event; a new attachment has
a fresh device identity. Snapshots return modifiers and six pressed keys.

Gamepad kinds are connected=1, disconnected=2, state=3. State contains up to
32 buttons (button 1 is bit 0), normalized signed-16-bit X/Y/Z/Rx/Ry/Rz and
hat 0..7 or 8 for neutral. Removal clears buttons and axes. Consumers use
semantic state rather than hardcoded raw report offsets.

Always `unsubscribe()` before releasing the capability grant. Quiesce refuses
to unload with any live subscription, but when the last subscriber is gone it
closes remaining HID interface claims **even if the device is still attached**.
This allows the generic HID, USB host and physical controller dependencies to
quiesce and release USB power without requiring users to unplug their devices.
A failed close prevents unload and must be retried safely. Queue overflow is
surfaced as a gap; snapshots restore current state.

## Scope and limits

- Keyboard: USB HID boot keyboard (subclass 1/protocol 1), 8-byte input reports
  and six-key rollover. Rollover error reports are ignored, not translated to
  releases. NKRO and consumer-page multimedia keys are outside this first version.
- Gamepad: the first Generic Desktop joystick/gamepad application collection
  and its first supported input report, up to 32 buttons, six standard axes
  and a hat. Input offsets are tracked separately for every Report ID; other
  input, output and feature IDs and later gamepad collections do not reject
  that layout. They are not combined into additional logical controllers.
  Proprietary protocols, XInput-only transport, rumble and controls split
  across several input reports remain unsupported. Axis spans beyond 16 bits
  are rejected.
- Generic HID: configuration descriptor discovery, interface claim exclusivity,
  HID report descriptor GET_DESCRIPTOR, boot-protocol control request and
  claimed interrupt-IN reads. Maximum interrupt report is 64 bytes, descriptor
  at most 512 bytes.
- Resource caps: 16 discovered interfaces, 8 generic sessions, 4 keyboards,
  4 gamepads, 4 subscriptions per class and 32 queued events per subscriber.
  Capacity exhaustion and unsupported descriptors fail explicitly, without
  falling back to firmware HID handlers.

`usb-hid-gamepad` 0.1.2 adds the optional table suffix in
`RiscUsbGamepadDiagnosticsV1.h`. Check the base `struct_size` before calling
`diagnostic(context, out, capacity)`. It copies the latest discovery state
without USB I/O, distinguishing no HID interface, claim failure, descriptor
read failure, unsupported layout and interrupt read failure. Poll can succeed
while an individual unsupported interface is skipped; consumers should show
this diagnostic instead of interpreting a successful subscription as a
connected controller. The original API-v1 prefix and firmware are unchanged.

## Xbox 360-format receivers

`usb-xinput-gamepad` 0.1.1 is an independent class ELF requiring `usb.host@1`
and publishing `usb.xinput.gamepad@1`. It matches Xbox 360 vendor interfaces
by class/subclass/protocol `ff/5d/01` (wired-format, including `045e:028e`
2.4 GHz receivers) or `ff/5d/81` (wireless-format), including compatible
VID/PID clones and nonzero alternate settings. Standard HID, Xbox One,
force feedback and LED output are outside this driver.

The driver selects the first matching interface with an interrupt-IN endpoint
and claims its advertised alternate setting. Input requires at least 20 received
bytes, byte 0 equal to zero and byte 1 at least 14, matching the hardware-tested
Gameboy standalone decoder. Wireless-format input skips a four-byte transport
prefix; presence notifications connect/disconnect the pad without counting as
input. Short/status packets do not erase held input. Interrupt endpoint packet
sizes are limited to 20..64 bytes. The driver sends no HID report-descriptor,
protocol or initialization-output requests to the XInput interface.

The capability reuses `risc_usb_gamepad_api_v1` and its optional diagnostics
suffix. Button bits are: 0 B, 1 A, 2 Y, 3 X, 4/5 shoulders,
6/7 triggers (pressed at raw value 128), 8 Back, 9 Start, 10/11 stick clicks,
12 Guide. Face buttons follow Xbox names, matching standalone Gameboy. Y axes
use the same bitwise inversion as that decoder; trigger axes span -32768..32767.
Gameboy combines XInput D-pad and analog directions. A USB claim alone does
not report a connected gamepad: valid input or wireless presence does.

`usb-controller-esp32s3` 0.1.11 clears a stalled interrupt endpoint before the
next bounded read resubmits, matching standalone recovery. Idle transfers
remain pending, and failed physical teardown still pins the controller.

Discovery inspects one newly attached host generation per poll and caches its
result until removal. Four gamepads, four subscribers and 32 queued events per
subscriber bound memory. Each poll performs at most 16 interrupt reads with
10 ms cooperative deadlines, normally four reads when called by Gameboy.
USB removal or read failure releases held input; a later valid packet reconnects.
The last subscriber must leave before quiescence closes claims, including while
the receiver remains plugged in. Failed physical drains remain owned by the host.

Build with `python scripts/build_usb_xinput_gamepad.py`. It is included in the
installed stack, canonical driver catalog, export and existing CI/release build
paths. Gameboy 1.2.18 adds an optional grant for this capability and polls HID
and XInput independently. Install the new driver and updated app together;
updating only the app does not install the XInput driver.

## USB enumeration diagnostics

`usb-controller-esp32s3` 0.1.9 -> 0.1.10 and `usb-host-v2` 0.1.2 -> 0.1.3 add
the optional `RiscUsbDiscoveryDiagnosticsV1.h` suffix. The host forwards a
bounded snapshot of the controller's root-port state and retained IDF
enumeration error. Consumers check `struct_size` before reading the suffix;
older installed drivers retain their original API prefixes.

The build instruments its private copy of pinned IDF 4.4.7 `hub.c`, observes
enumeration stages/errors, and retains the first error per physical connection.
It changes no enumeration decisions and installs no firmware logging callback.
`PORT OFF`, `NO ATTACH`, and `ATTACHED` are controller register states; the power
bit is not a measurement of external VBUS voltage. A zero-device host snapshot
still prevents class drivers from running. Adding XInput decoding does not by
itself establish why a particular board failed to enumerate the receiver.

Gameboy displays these states/errors on its existing test screen without a
serial connection. Class diagnostics distinguish `XINPUT WAITING FOR REPORT`,
`XINPUT GAMEPAD CONNECTED`, configuration, claim and transfer failures.

## Verification and deployment

`bash test/run_usb_hid_test.sh` exercises fake keyboard/gamepad reports,
subscriptions, unplug disconnects and **still-plugged final-subscriber
shutdown**. Experimental provider CI cross-compiles the Xtensa ELFs, validates
their relocation map and assembles installable packages. These tests cannot
establish physical USB OTG, VBUS, an individual controller's report profile or
T5S3 Paper Pro compatibility; board validation remains separate. This branch
does not merge, flash or publish a release.
