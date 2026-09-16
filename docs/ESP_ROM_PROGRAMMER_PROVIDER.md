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

## Automated checks

`test/programmer/esp_rom_protocol_test.cpp`, run from `test/run_driver_test.sh`, checks bounded SLIP framing and escaping, malformed input, checksum and ROM status response parsing. CI also compiles both firmware board targets, native apps, driver packages, and manifest checks. These are host/build checks, **not a physical programming test**.

## On-device validation before release/merge

Exercise a known-good merged image with the same hardware previously used for the Flasher. Verify SD header and source hashing, host power and enumeration, reset and SYNC, SPI attach, erase, all blocks, flash MD5 and reset. Exercise cancellation during hashing, sync and writing; unplug during reset, erase and TX/RX; USB replug between attempts; retries after errors; absent/corrupt CDC driver fallback; non-CDC CP210x/CH34x boards; app Back/Home and repeated launches. Confirm no USB task, power lock, stream, lease or stale driver ELF leaks, and compare timings against the original Flasher. A successful host CI run does not establish this hardware parity.

## Outstanding spec work

Implement an execution-context-owned asynchronous programming job with a generation-safe handle if needed by other clients, plus explicit transport-loss revocation through streams/leases and test instrumentation for hardware reset, disconnect and recovery. Serial Monitor is the long-lived bidirectional capability/stream reference; this Flasher is the bounded composed-capability/file-stream reference. Do not remove resident USB fallback drivers until hardware parity is demonstrated.
