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
- Gamepad: HID Generic Desktop joystick/gamepad report collections, one report
  layout and optional single report ID, up to 32 buttons, six standard axes and
  a hat. Bounded descriptor parsing. Proprietary gamepad protocols, XInput-only
  transport, rumble, output reports and composite multi-report gamepads are
  outside this first version. Axis spans beyond 16 bits are rejected.
- Generic HID: configuration descriptor discovery, interface claim exclusivity,
  HID report descriptor GET_DESCRIPTOR, boot-protocol control request and
  claimed interrupt-IN reads. Maximum interrupt report is 64 bytes, descriptor
  at most 512 bytes.
- Resource caps: 16 discovered interfaces, 8 generic sessions, 4 keyboards,
  4 gamepads, 4 subscriptions per class and 32 queued events per subscriber.
  Capacity exhaustion and unsupported descriptors fail explicitly, without
  falling back to firmware HID handlers.

## Verification and deployment

`bash test/run_usb_hid_test.sh` exercises fake keyboard/gamepad reports,
subscriptions, unplug disconnects and **still-plugged final-subscriber
shutdown**. Experimental provider CI cross-compiles the Xtensa ELFs, validates
their relocation map and assembles installable packages. These tests cannot
establish physical USB OTG, VBUS, an individual controller's report profile or
T5S3 Paper Pro compatibility; board validation remains separate. This branch
does not merge, flash or publish a release.
