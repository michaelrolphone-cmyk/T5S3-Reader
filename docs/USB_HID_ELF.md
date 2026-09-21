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

The canonical capability identifiers are **lowercase**, matching the package
manifests and normal provider registry conventions. `USB.hid` in a design
brief refers to `usb.hid` in runtime calls.

The ABI is `sdk/driver/RiscUsbHidV1.h`. Both classes publish `api_version=1` and
`struct_size`; consumers must verify those fields before accessing a callback.
The base `usb.controller` and `usb.host` APIs retain their existing v1 prefixes;
`RiscUsbInterruptV1.h` appends a claimed interrupt-IN operation and requires
`struct_size >= sizeof(the extended table)` for HID providers. Updating the host
and physical controller ELFs together is necessary; an old controller fails
closed when the new host requests interrupt I/O.

## Subscribing in an admitted application

Acquire an execution-context-owned lease for `usb.hid.keyboard` or
`usb.hid.gamepad` through the normal provider registry. Do not cache its API
pointer after the lease is released. The exact registry call sequence is the
same as any other installed driver; no HID-only firmware registry is added.

After acquiring the keyboard API, use `subscribe(api->context, 0)` to observe
all keyboards or `subscribe(api->context, device_id)` to filter by one current
USB-host-generation-qualified device. The call returns a nonzero subscription
handle or zero on exhaustion. Use `poll(api->context, max_reports)` to advance
input, then repeatedly call `next(api->context, subscription, &event)` until it
returns zero (empty). A result of `1` is an event. A negative result signals
an invalid subscription or queue discontinuity: discard derived key state and
call `snapshot()`. Each subscriber has its own bounded queue; slow subscribers
cannot block unrelated consumers. The gamepad API has the same subscribe,
unsubscribe, poll, next and snapshot contract with gamepad event types.

Keyboard events use USB HID keyboard-page usage codes, **not ASCII**. Event
kinds are connect=1, disconnect=2, press=3, release=4. Modifiers are the boot
keyboard modifier bitset. A disconnect releases held keys and reports device
removal, and a fresh USB attachment receives a new device identity. Keyboard
snapshots contain current modifiers and six pressed key usages.

Gamepad events use connect=1, disconnect=2, state=3. State includes up to 32
buttons (button 1 is bit 0), normalized signed-16-bit X/Y/Z/Rx/Ry/Rz, and hat
0..7 or 8 for neutral/not present. A disconnect clears the buttons and axes.
Consumers should react to `state` events and avoid hardcoded report byte offsets.

Always `unsubscribe()` before releasing a capability lease. Quiesce fails
while a class owns open HID sessions or subscriber handles, preventing an ELF
from being unmapped while consumers still hold its API table. Queue-overflow
is surfaced as a gap; snapshots restore current state.

## Scope and limits

- Keyboard: USB HID boot keyboard (interface subclass=1, protocol=1), 8-byte
  input reports and standard six-key rollover. Rollover error reports are
  ignored rather than interpreted as key release. Non-boot NKRO/consumer-page
  keys are outside this initial implementation.
- Gamepad: standard HID Generic Desktop joystick/gamepad application report
  collections, one report layout and optional single report ID, up to 32
  buttons, six standard axes and a hat. Descriptor parsing uses bounded short
  items. Proprietary controller protocols, XInput-specific transport, rumble,
  output reports and composite multi-report gamepad layouts are not supported.
- Generic HID: configuration descriptor input-interface discovery, interface
  claim exclusivity, HID report descriptor GET_DESCRIPTOR, boot-protocol class
  request and claimed interrupt-IN reads. Maximum interrupt packet/report is
  64 bytes; descriptors are bounded at 512 bytes.
- Resource caps: 16 discovered HID interfaces, 8 generic sessions, 4 keyboards,
  4 gamepads, 4 subscriptions per class and 32 queued events per subscriber.
  Overflow and unsupported descriptors fail explicitly, never silently fall
  back to firmware HID code.

## Verification and deployment

Run `bash test/run_usb_hid_test.sh` for fake-device input and disconnect
behavior. The experimental provider CI additionally compiles and audits the
Xtensa ELFs and assembles their installable packages. Those checks do not
prove physical USB OTG, VBUS supply, controller compatibility, or a given
third-party gamepad; real T5S3 Paper Pro testing must exercise those separately.
This branch does not merge, flash or publish a driver release automatically.
