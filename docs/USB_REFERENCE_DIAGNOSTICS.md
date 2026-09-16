# USB reference implementation: diagnostic event contract

Authority: [USB Capability/Stream Reference Implementation](USB_CAPABILITY_STREAM_REFERENCE_IMPLEMENTATION.md), Phase 0 and validation matrix. Hardware evidence procedure: [USB Hardware Validation](USB_HARDWARE_VALIDATION.md).

These logs are internal, best-effort firmware diagnostics. They do not extend the application ABI, publish USB pointers or include serial bytes/firmware contents. No guarantee of log delivery is made while the ESP32-S3 debug USB PHY is in USB OTG host mode; a missing log line must be recorded as *not observed*, not as a successful or failed operation.

## Available event families

| Family | Source | Emission and interpretation |
|---|---|---|
| `USBREF event=bind` | Runtime USB device registry | Interface claim becomes a new opaque device ID; `cause=0`, epoch, VID:PID, interface, binding/revocation counters. |
| `USBREF event=revoke` | Runtime USB device registry | Previously bound **old** device ID and VID:PID/interface, epoch and reason. Identity replacement emits revoke-old then bind-new. `cause=1` is a detach *notification*, not proof of unplug. |
| `USBREF phase=driver_load` | Installable USB CDC class runtime | `active` or `fallback` with a specific class-package/load failure reason. An available resident driver is not evidence a particular adapter was successfully bound. |
| `USBREF phase=driver_probe` | Installable USB CDC class runtime | `matched`, `no-match`, or `invalid-input` with VID:PID and descriptor length. No raw descriptor bytes or handles are logged. |
| `SERREF action=acquire` | Semantic `serial.port` resolver | Public result, selected public lease on success, requested opaque device ID (zero means provider selection). Errors are recorded even when no provider was selected. |
| `SERREF action=configure`, `control-lines`, `release` | Semantic `serial.port` resolver | Public result and opaque lease; no physical USB control request exposed. |
| `SERREF action=status` | Semantic `serial.port` resolver | **Transitions only** in semantic state, connected flag, device ID, or last error; cumulative RX, TX and dropped-byte counters are snapshots at that transition, not a per-byte log. |
| `SERREF action=read-status` | Semantic `serial.port` resolver | Emitted only when the status API itself returns an error. |
| `SERREF action=context-begin`, `context-end` | Semantic `serial.port` resolver | App invocation scope for the provider lease; not evidence that a USB device has physically disconnected or that VBUS has dropped. |

`USBREF` epoch and device ID correlate an old USB binding with its eventual revocation; `SERREF` public lease and device ID correlate semantic operations. The runtime can also bind a non-USB provider: **do not attribute every `SERREF` line to USB**. USB epochs apply only to USB bindings, not alternate UART/BLE/network providers. Numeric results and status values are defined in `lib/NativeApps/include/T5SerialPortApi.h`; revocation causes are defined in `src/native/NativeUsbDeviceRegistry.h`.

## Classification procedure

1. If no USB device ever bound, inspect `driver_load` and `driver_probe` to distinguish a missing, invalid or incompatible class package; without pre-claim host telemetry, do not infer that enumeration succeeded or failed.
2. If a binding exists, compare its ID and epoch to `event=revoke`. Old leases cannot be silently rebound to a new generation. A `SERREF` failure following revocation should be evaluated against that timeline.
3. If provider acquisition fails with no lease, use the reported semantic result and selected/requested device, rather than guessing that a stream or ESP ROM command was involved.
4. If an acquired port reports READY but no data flows, consult stream counters/results and target-side instrumentation. **Per-operation stream errors are not yet included in this event family.**
5. If ROM SYNC, erase, write, MD5 or reset fails, retain the programmer's terminal status including command, ROM status/error and message. **Dedicated per-command ROM event logs remain outstanding.**

## Coverage and remaining work

Host tests cover USB identity revocation and semantic resolver behavior. Firmware compilation establishes that the instrumentation builds on supported targets; neither constitutes on-device timing, adapter-compatibility or real flash verification. Pre-claim USB host/power diagnostics, stream read/write failure logging, protocol command attribution and hardware parity are still open. The resident driver fallback must stay in place until actual hardware tests establish equivalent behavior.
