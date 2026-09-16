# USB capability/stream reference: hardware validation record

Authority: [USB Capability/Stream Reference Implementation](USB_CAPABILITY_STREAM_REFERENCE_IMPLEMENTATION.md), [ESP ROM Programmer Provider](ESP_ROM_PROGRAMMER_PROVIDER.md), and [Serial Provider Resolution](SERIAL_PORT_PROVIDER_RESOLUTION.md).

## State of evidence

Host fault-injection tests and firmware builds in PR #62 are **not hardware evidence**. No baseline, timing, adapter compatibility, ESP ROM MD5 match, or power/teardown result is presumed. Keep the resident USB driver fallback and PR draft status until the on-device matrix below is recorded and reviewed. This is a procedure, not a completed test report.

## Record one evidence bundle per firmware/board/adapter combination

Record the firmware commit and board, target MCU and known-good merged image SHA-256, USB adapter VID:PID and driver name, cable/hub/power configuration, SD-card image size and hash, and whether the CDC ELF is present, absent or intentionally corrupt. Capture a **complete, unedited, timestamped** device serial log, application progress/results and target serial output when possible. The debug USB port may be suspended while OTG is active; record events through available diagnostics or external instrumentation and explicitly mark observability gaps. Do not infer event timing from the application's progress percentage.

Before claiming migration parity, capture the **same measurements on original working firmware and candidate firmware** using the same hardware and image. Preserve raw logs and timestamp method. Align external-power-monitor and firmware `millis()` clocks against a recorded trigger; do not subtract unrelated timestamps.

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

## USBREF device and driver diagnostics implemented in PR #62

`NativeUsbDevices::Registry::diagnostics()` returns a coherent cross-core snapshot of the active device and `last_revoked` identity, revocation epoch, binds and revocations, last cause, and counts for detach notifications, connection losses, host stops and binding identity changes. `last_revoked` retains the previous device ID, VID:PID and interface even though the current device is cleared after loss. Counters persist across invocations and saturate. Redundant detach or READY polling does not generate a transition; `test/streams/usb_device_registry_test.cpp` covers each cause, retained old identity and stale ID rejection.

One `USBREF event=bind` log line is emitted on a new claim, and one `USBREF event=revoke` on revocation. A direct identity replacement produces **two entries** (revoke old ID, then bind new ID). Each line contains epoch, ID, cause, VID:PID, interface, and bind/revocation counts. Logs are emitted outside the cross-core lock from coherent captured snapshots. Binding entries always have cause `0`; revocations use cause `1` detach notification, `2` connected device no longer reported, `3` host OFF, or `4` identity change. The earlier log format `binding=bound|revoked` is superseded by `event=bind|revoke`.

The CDC class-loader also emits `USBREF phase=driver_load result=active|fallback reason=...` and `USBREF phase=driver_probe result=matched|no-match`. Loader failure reasons distinguish SD unavailable, manifest missing, unreadable manifest, malformed JSON, capability/API mismatch, failed ELF payload verification, ELF load/ABI error and mutex allocation failure. A driver probe result supplies VID:PID and descriptor byte count, not raw USB pointers or descriptor contents. The resident USB fallback remains in place; an actively loaded but nonmatching CDC module is *not* silently represented as an absent package.

**Interpretation:** `cause=1` may be physical DEV_GONE **or intentional lease/app/host teardown**; it is not a physical-unplug counter. Driver `result=fallback` indicates the class ELF could not be activated, not that a particular USB adapter is supported by resident drivers. Firmware debug serial may be unavailable while USB OTG holds the internal PHY: absent `USBREF` text is an observability gap, not evidence of no transition.

**Scope limitation:** binding diagnostics begin at the claimed-interface boundary. They do not yet capture pre-claim enumeration failures, VBUS timing, stream read/write failures, ROM commands or physical timing. The snapshot is internal firmware state, not an ELF API; logs use best-effort firmware logging, not durable telemetry. For uninstrumented layers, retain original raw logs or external evidence and mark failures unclassified rather than attributing them to a provider.

## On-device regression matrix

For each board supporting the USB host, run CDC-ACM with the packaged driver, CDC with driver absent, CDC with an intentionally invalid driver package, CP210x and CH34x adapters where physically supported. Record unavailable hardware as **not tested**, never pass. Run repeated Serial Monitor launches, 9600/115200 and another target-supported baud rate, 60-second full-duplex traffic, idle/sleep handling, disconnect before READY, unplug during RX/TX, same-adapter rapid replug, and Back/Home during use. Confirm revoked stream and lease handles never attach to a replacement device, then acquire fresh handles. Compare byte/drop counters with baseline.

For Flasher, use a verified merged image (ESP header `0xE9` at offset zero) and capture image length and both source/target **actual** 32-character MD5 digests. Execute SD read/hash, ROM entry/SYNC, SPI attach/parameters, erase, all written blocks, flash MD5 comparison, reset and target boot. Inject cancellation during hash, wait for SYNC, erase and write; detach during pre-READY configuration, reset control, erase, RX and TX; reconnect and retry. Verify no further progress callback after cooperative app exit and no re-use of an old stream on a replacement device. Preserve separate I/O, target-loss, verification-mismatch and cancellation results.

Reopen apps repeatedly and verify VBUS, power locks, USB host/client/task, driver ELF, stream, pipe, serial lease and programmer-operation resources are reclaimed. **Do not kill the trusted C ELF task** to simulate process-isolated recovery: the runtime does not offer isolation for arbitrary hard faults. Record real crash behavior separately as a platform limitation.

## Acceptance and cleanup gate

For every row attach firmware version, hardware identity, timestamped evidence, measured result, baseline result (or documented absence), and reviewer disposition. Only mark hardware parity when functional tests pass, disconnect and retry work, source and target digests match for a successful flash, and no lifecycle/power leaks are observed. If a row cannot run, mark it **not tested** and leave the requirement open. Do not remove the resident fallback until the corresponding installable provider demonstrates equivalent behavior on real hardware.
