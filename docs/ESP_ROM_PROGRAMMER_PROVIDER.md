# ESP ROM Programmer: composed capability reference implementation

Authority: [RiscRTE Platform Specification](RISCRTE_PLATFORM_SPEC.md), [USB Capability/Stream Reference Implementation](USB_CAPABILITY_STREAM_REFERENCE_IMPLEMENTATION.md), [Stream/Pipe Architecture](STREAM_PIPE_ARCHITECTURE.md), and [Programmer/Debugger Architecture](PROGRAMMER_DEBUGGER_ARCHITECTURE.md). This document describes the implementation in PR #61, not a claim of on-device validation.

## Capability graph

```text
Apps/esp_rom_flasher.c: selection, confirmation, stage/progress/results, cancellation
  -> t5_stream_get_api(v1): open_file(/sd/name.bin, READ), close
  -> t5_program_esp_rom_get_api(v1): program(firmware_stream, bytes, callback, result)
      -> NativeEspRomBridge: one synchronous bounded operation per execution context
          -> serial.port: exclusive lease, semantic status, reset controls, RX/TX streams
              -> runtime USB serial provider -> USB host, VBUS and class-driver ELF
          -> firmware stream: two passes (source MD5, 1024-byte flash blocks)
          -> ESP ROM SLIP codec, SPI configuration, erase, write and MD5 verification
```

The ELF application does **not** import `T5UsbApi.h`, access USB descriptors or own ROM protocol buffers. The provider must not call USB host APIs; it acquires the versioned `serial.port` interface. The programmer's firmware source is a borrowed, readable, seekable stream. The application closes it when `program()` returns, regardless of result; the provider releases its serial lease and the lease closes both serial streams before it returns. The provider never retains the app callback or its context.

## Current API behavior

`T5ProgramEspRomApi.h` exports `program.esp_rom` API 1. It currently supplies **one synchronous programming operation**, not a persistent or asynchronous job handle. Progress callbacks execute on the app's owning task and may render the UI and cancel on Back/Home; the implementation yields and feeds the watchdog during waits. Failure is reported with an enum plus stage, percentage, ROM command/status/error and a short diagnostic. The operation's buffers are allocated on the heap and are freed on every terminal path. Concurrent attempts return BUSY. Device identity from `serial.port` is latched when READY and a detach/identity change fails the operation as TARGET_LOST; the app does not parse VID/PID or USB errors.

The existing protocol sequence is retained: merged 0xE9 ESP image at offset zero, 64 KiB–16 MiB range, streamed source MD5, 115200 8N1, DTR/RTS ClassicReset holds of 100/250/500 ms with up to five sync attempts per hold, optional extended-flash probe, SPI attach/parameters, erase begin, 1024-byte padded flash blocks with three write attempts, target MD5 comparison, and reset. Both file passes use at most 512-byte RiscRTE stream transfers. No entire firmware image is loaded into app memory.

The Flasher sidecar is version 1.0.6 and declares a firmware floor of 1.2.9 for this new API. Earlier firmware is intentionally incompatible with the migrated ELF; earlier Flasher releases are not silently reinterpreted.

## USB loss, lease revocation and fresh acquisition

The USB host publishes device removal to a cross-core synchronized runtime registry. Its monotonic binding epoch advances when a bound interface disappears or changes identity, even if an identical device is reconnected between consumer polls. Each `serial.port` lease and each USB RX/TX stream capture the epoch at acquisition. Once it changes, an existing lease reports semantic disconnection, rejects configuration and DTR/RTS changes, and its streams terminalize with `T5_STREAM_DISCONNECTED` on the next read/write. A replacement device requires a **new serial lease and new streams**; an old stream may never attach itself to the replacement. Explicit release and execution-context teardown still close streams and stop the host. Concurrent serial acquisition checks existing stream ownership before touching USB power so a BUSY request cannot tear down another client's compatibility stream.

The programmer detects a lost selected device during `read_status()` and returns `TARGET_LOST`, rather than continuing with an identical replacement. Loss before any device has become READY is currently subject to the initial connection timeout; a dedicated pre-READY semantic loss diagnostic is still outstanding. The Serial Monitor currently displays disconnected/waiting state but does not automatically reacquire a new lease after a terminal detach; restart the app for a new connection. Automatic application-level reconnect with a fresh lease is a remaining behavior-parity task, not something the runtime should implement by reviving stale stream handles.

## Automated checks

`test/programmer/esp_rom_protocol_test.cpp`, run from `test/run_driver_test.sh`, checks bounded SLIP framing and escaping, malformed input, checksum and ROM status response parsing. `test/streams/usb_device_registry_test.cpp` checks epoch monotonicity across redundant removal, identical replug and descriptor/interface changes; `test/streams/bridge_test.cpp` checks stale lease/stream rejection, replacement acquisition and app-unload cleanup. CI also compiles both firmware board targets, native apps, driver packages, and manifest checks. These are host/build checks, **not a physical programming test**.

## On-device validation before release/merge

Exercise a known-good merged image with the same hardware previously used for the Flasher. Verify SD header and source hashing, host power and enumeration, reset and SYNC, SPI attach, erase, all blocks, flash MD5 and reset. Exercise cancellation during hashing, sync and writing; unplug during reset, erase and TX/RX; USB replug between attempts; retries after errors; absent/corrupt CDC driver fallback; non-CDC CP210x/CH34x boards; app Back/Home and repeated launches. Confirm no USB task, power lock, stream, lease or stale driver ELF leaks, and compare timings against the original Flasher. A successful host CI run does not establish this hardware parity.

## Outstanding spec work

Implement an execution-context-owned asynchronous programming job with a generation-safe handle if needed by other clients, explicitly diagnose detach before initial READY, implement Serial Monitor automatic reconnection by acquiring fresh handles, and exercise hardware reset/disconnect/recovery instrumentation. Serial Monitor is the long-lived bidirectional capability/stream reference; this Flasher is the bounded composed-capability/file-stream reference. Do not remove resident USB fallback drivers until hardware parity is demonstrated.
