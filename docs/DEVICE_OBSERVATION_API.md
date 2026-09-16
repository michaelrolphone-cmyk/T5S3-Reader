# RiscRTE Device Observation API — v1 compatibility and implementation

Authority: [RiscRTE Platform Specification](RISCRTE_PLATFORM_SPEC.md), [Platform Capability Roadmap](PLATFORM_CAPABILITY_ROADMAP.md), and [Runtime Driver Architecture](RUNTIME_DRIVER_ARCHITECTURE.md). Device acquisition and permissions are separately documented in [Device Capability Access API](DEVICE_CAPABILITY_ACCESS_API.md).

## Purpose and available interface

`lib/NativeApps/include/T5DeviceApi.h` defines `T5_DEVICE_API_VERSION=1`. An ELF imports `t5_device_get_api(T5_DEVICE_API_VERSION)` and checks for a non-null table with the matching `api_version` and `struct_size`. The loader explicitly exports only this factory, not internal registry or subscription methods. A firmware bridge at `src/native/NativeDeviceBridge.cpp` implements these functions:

| Function | Contract |
| --- | --- |
| `inventory(out, capacity, count)` | Copy current discovered devices; when capacity is insufficient, return `LIMIT`, set `*count` to the required number, and write no partial inventory. A zero-capacity/null-output query discovers the required capacity. |
| `subscribe(out)` | Allocate an opaque generation-safe handle tied to the current application invocation; start at the current journal sequence, not a preceding app's history. |
| `snapshot(sub, out, capacity, count)` | Copy the complete current inventory and atomically acknowledge the subscription cursor; mandatory after a journal `GAP`. `LIMIT` does **not** acknowledge the gap. |
| `poll(sub, out, missed)` | `NEXT` copies one event; `EMPTY` indicates no event; `GAP` signals overwritten history and blocks further event delivery until a successful snapshot. `missed` is optional. |
| `unsubscribe(sub)` | Release only the calling invocation's subscription; a stale handle is rejected. |

The ABI copies fixed-width data: a generation-qualified device handle, state, transport, priority, identity, label, provider and semantic capability names. Events copy sequence, handle, kind, previous/current state, forced-revocation count, and identity. Event kinds are `ADDED`, `STATE_CHANGED`, `CAPABILITY_LOST`, `REMOVAL`; the identity remains available after the removed handle becomes invalid. Strings are bounded/NUL-terminated and are metadata, **not authority**. Transport and state values have fixed ABI enumerations and are checked against the runtime enums at build time. C and C++ clients may include this header.

The original v1 table has not changed size or member ordering. Requesting version 2 returns an extended table with the complete v1 prefix and three new access methods. V2 is **default-deny** and requires a trusted firmware-issued grant; an inventory record or manifest declaration never grants permission. See [Device Capability Access API](DEVICE_CAPABILITY_ACCESS_API.md) before integrating provider I/O with access handles.

## Security, lifecycle and consistency

Only the active app owner task with a running `ExecutionContext` and an active app host API may obtain or use this table. The firmware takes the caller identity from the execution context, not an ELF-supplied owner ID. Subscription slots (maximum eight) are owned by that context and cleaned before its ELF unload; their handles cannot be reused without changing generation. Neither the device registry, event journal nor subscription storage retains a pointer into an ELF or calls an ELF callback. Observation grants **no device permission or physical lease**. Existing semantic provider APIs retain their current, separately documented hardware-access pathways pending migration to v2 authorization enforcement.

The inventory contains at most 12 devices; the journal retains at most 24 events. Polling is bounded and pull-based. A consumer that encounters `GAP` must re-enumerate via `snapshot` and rebuild state rather than interpreting a suffix as complete history. An insufficient destination capacity leaves its cursor unchanged. Consumers should not infer that an inventory record means an installable driver was activated or tested. USB identities are session-generation qualified; a matching VID/PID/product after replug is a distinct device.

`NativeUsbDevices::Registry` receives the USB host's lock-protected callbacks. `nativeDeviceDiscoveryTick()` projects that snapshot into the unified registry on the firmware owner task, including the normal activity loop and ELF input polling. API inventory/subscribe/snapshot/poll also reconcile first. These calls do not power up USB, initiate enumeration, or load drivers. A callback cannot mutate the unified registry. No independent worker guarantees publication while a native app blocks its owner task without polling or calling these APIs.

## Validation and rollout

`test/streams/usb_device_api_test.cpp` links the production serial and device bridges against a simulated host and verifies no-context denial, version checking, all-or-nothing inventory, callback isolation, lease revocation, detach/replug generation changes, overflow and required resnapshot, explicit unsubscribe, context cleanup and stale handles after a new invocation. `test/resources/device_bridge_v2_test.cpp` adds a direct version-2 bridge regression for grant denial, context checks, rights and revocation. The tests are built with address/undefined-behavior sanitizers; the ABI header also compiles as C11. Firmware CI must build both `t5s3-pro` and `lilygo-epd47-s3` and validate loader symbol registration.

Remaining before full Device Manager completion: physical USB hot-plug/VBUS/reconnect and GNSS acceptance, active USB host discovery before a session starts, additional USB classes and BLE transport adapters, trusted grant/device-selection UI, provider-side enforcement of v2 access handles, and event delivery independent of a blocked owner task. Consumers should use semantic capabilities, not bind their behavior to a USB provider name.
