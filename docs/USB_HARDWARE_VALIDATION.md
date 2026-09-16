# USB capability/stream reference: hardware validation record

Authority: [USB Capability/Stream Reference Implementation](USB_CAPABILITY_STREAM_REFERENCE_IMPLEMENTATION.md), [ESP ROM Programmer Provider](ESP_ROM_PROGRAMMER_PROVIDER.md), and [Serial Provider Resolution](SERIAL_PORT_PROVIDER_RESOLUTION.md).

## State of evidence

The host fault-injection tests and firmware builds in PR #62 are **not hardware evidence**. No baseline, timing, adapter compatibility, ESP ROM MD5 match, or power/teardown result is presumed. Keep the resident USB driver fallback and PR draft status until the on-device matrix below is recorded and reviewed. This document is a test procedure, not a completed test report.

## Record one evidence bundle per firmware/board/adapter combination

Record the firmware commit and board, target MCU and known-good merged image SHA-256, USB adapter VID:PID and driver name, cable/hub/power configuration, SD-card image size and hash, and whether the CDC ELF is present, absent or intentionally corrupt. Capture a **complete, unedited, timestamped** device serial log, application progress/results and host-side target serial output when possible. The debug USB port may be suspended while OTG is active; if so, record events through the available on-device diagnostics or external instrumentation and explicitly mark observability gaps. Do not infer event timing from the application's progress percentage.

Before migration parity can be claimed, capture the **same measurements on the original working firmware and the candidate firmware**, using the same hardware and image. Preserve both original logs and the method used to obtain timestamps. Clock domains from an external power monitor and firmware `millis()` must be aligned with a recorded trigger; do not subtract unrelated timestamps.

| Measurement | Start / end evidence | Result |
|---|---|---|
| VBUS start and target boot | OTG enable request, measured VBUS rising edge, stable voltage, target boot evidence | Not measured |
| USB enumeration | First NEW_DEV event, device opened, descriptor and selected VID:PID | Not measured |
| Driver binding | ELF activation/probe or resident fallback, claimed interface, subsequent READY | Not measured |
| Serial throughput | 60 s sustained TX/RX at 9600 and 115200 baud, byte counts, dropped bytes and sample integrity | Not measured |
| ROM connection | DTR/RTS attempts, first valid SYNC reply and retries | Not measured |
| Firmware hashing | Initial read through source MD5, measured elapsed time and source SHA-256 | Not measured |
| Flash and verify | Erase, written byte count, target MD5 response, matching source MD5, reset and target boot evidence | Not measured |
| Cleanup/recovery | App exit, VBUS down, host/client/task teardown, SD/stream/lease release, reconnect READY | Not measured |

## Device-registry diagnostic counters and logs (implemented in PR #62)

`NativeUsbDevices::Registry::diagnostics()` takes a coherent, cross-core-protected snapshot containing the current claimed device/ID, revocation epoch, total bindings, total revocations, last revocation cause, and independent counts for detach notifications, loss of connection, host stop and binding identity change. Counters persist across app invocations and saturate rather than wrapping. Redundant detach and repeated READY polls must not add a false transition. `test/streams/usb_device_registry_test.cpp` exercises each category, same-identity replug and non-resetting generations.

A real binding transition or revocation also emits one firmware `LOG_INF` event after releasing the cross-core lock, tagged `USBREF` with `binding=bound|revoked`, `epoch`, opaque `id`, `cause`, VID:PID, interface, total binds and revocations. The event fields are captured together under the lock before logging; no USB pointer or app-level handle is exposed. Cause values: `0` none/first binding, `1` detach notification, `2` connected device no longer reported, `3` host OFF, `4` identity change. These event logs identify claimed-interface transitions and support correlation with the existing driver-selection log messages.

**Interpretation:** a detach notification (`cause=1`) can be emitted by real DEV_GONE **or intentional lease/app/host teardown**. It is *not* a physical-unplug counter. An identity-change binding line (`cause=4`) includes the revocation of the previous binding; compare the epoch and total revocations, not just the line's `binding=bound` text. Firmware debug serial may be unavailable while OTG is active, so missing `USBREF` text is an observability gap, not absence of a transition.

**Scope limitation:** these diagnostics measure claimed-interface transitions only. They do not capture failed pre-claim enumeration, host power timings, why an ELF probe failed, stream read/write failures, ROM commands or actual hardware timing. The snapshot is an internal firmware accessor, not an ELF API; the `USBREF` log output is best-effort through existing firmware logging, not a durable telemetry store. Record those separate layers with original raw logs or external instrumentation. An absent event is *unobserved*, not proof that it did not occur.

## On-device regression matrix

For each board supported by the USB host, run CDC-ACM with the packaged driver, CDC with the driver absent, CDC with an intentionally invalid driver package, CP210x and CH34x serial adapters where physically supported. Record unavailable hardware as **not tested**, never pass. Run repeated Serial Monitor launches, 9600/115200 and another target-supported baud rate, full-duplex sustained traffic, idle/sleep handling, disconnect before READY, unplug under RX and TX, same-adapter rapid replug, and app Back/Home during active use. Confirm old leases and stream handles remain revoked and fresh handles work only after reacquisition; collect drop counters and compare with baseline.

For the Flasher, use an explicitly verified merged image (ESP header `0xE9`, image at offset zero); capture image byte count and both source and target 32-digit MD5 digests. Execute SD read and hash, ROM entry and SYNC, SPI attach/parameters, erase, every written block, flash MD5 comparison, reset and post-flash boot. Inject cancellation while hashing, waiting for SYNC, erasing and writing; detach during pre-READY configuration, reset control, erase, RX and TX; then reconnect and retry. Confirm no later progress callback runs after cooperative app exit and that an old stream cannot operate on the replacement device. Distinguish flash I/O, target loss, verification mismatch and cancellation in the saved diagnostics.

Reopen the apps repeatedly and verify VBUS, power locks, USB host/client/task, driver ELF, stream, pipe, serial lease and programmer operation resources are reclaimed. **Do not preemptively kill the C ELF task** to simulate an isolatable process crash; the current runtime does not provide process-style crash recovery. Record real crash behavior separately as a platform limitation.

## Acceptance and cleanup gate

For every matrix row, attach the firmware version, hardware identity, timestamped evidence, measured result, baseline result or documented absence of baseline, and reviewer disposition. Only mark reference-spec hardware parity when functional tests pass, device removal and retries behave correctly, both source/target digests match for the successful flash, and no lifecycle/power leaks are observed. If a row cannot be run, mark it **not tested** and retain the open requirement. Do not remove any resident USB fallback until equivalent behavior has been demonstrated with the corresponding installable provider on real hardware.
