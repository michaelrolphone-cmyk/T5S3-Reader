# RiscRTE USB Capability/Stream Reference Implementation

## Status and priority

**HIGH PRIORITY — reference implementation for capability-driven RiscRTE application development.**

Authority: [RiscRTE Platform Specification](RISCRTE_PLATFORM_SPEC.md), [Platform Capability Roadmap](PLATFORM_CAPABILITY_ROADMAP.md), [Application Execution Context Architecture](APPLICATION_EXECUTION_CONTEXT_ARCHITECTURE.md), [Stream/Pipe Architecture](STREAM_PIPE_ARCHITECTURE.md), [USB OTG Host Architecture](USB_OTG_HOST_ARCHITECTURE.md), and [Programmer/Debugger Architecture](PROGRAMMER_DEBUGGER_ARCHITECTURE.md).

This specification defines the migration of the existing USB Serial Monitor and ESP ROM Firmware Flasher from application-owned USB OTG implementations to capability-driven applications using runtime-owned USB providers and RiscRTE streams. It is intentionally a narrow vertical slice: preserve existing results while implementing the greatest useful portion of the target architecture with the least new machinery.

The completed Serial Monitor and Firmware Flasher SHALL serve as canonical examples for future capability-driven RiscRTE apps.

## 1. Objectives

The work SHALL:

1. preserve current Serial Monitor and Firmware Flasher user-visible behavior and working hardware behavior;
2. remove USB host/device/transfer ownership from both applications;
3. establish runtime-owned USB device/provider infrastructure;
4. establish `serial.port` as a semantic capability independent of USB;
5. route serial RX/TX through bounded RiscRTE streams;
6. establish `program.esp_rom` as a semantic programming capability built on `serial.port` rather than USB;
7. feed firmware images to the programmer through streams rather than large app-owned image buffers;
8. connect device loss to capability revocation, stream termination and job failure;
9. make capability/stream resources execution-context owned and deterministically reclaimable;
10. provide a small, understandable reference architecture reusable for GNSS, BLE, UART, storage and future hardware.

## 2. Non-goals

This job SHALL NOT block on a complete implementation of every future RiscRTE facility. In particular, it need not implement all USB classes, generalized record streams, arbitrary stream graphs, full IPC, every permission/picker UI, BLE/UART serial providers, full dynamic driver installation, or the complete future Device Registry.

Where a future abstraction is not yet implemented, implement the smallest reusable subset whose API and ownership model can converge without requiring the two applications to regain transport-specific knowledge.

## 3. Target architecture

```text
                       RiscRTE Applications
                    /                       \
             Serial Monitor             Firmware Flasher
                  |                           |
             serial.port                program.esp_rom
                  |                           |
                  |                    programming job
                  |                           |
                  +--------- Streams --------+
                              |
                       Capability Resolver
                              |
                  +-----------+-----------+
                  |                       |
          USB Serial Provider      ESP ROM Provider
                  |                       |
                  +------ serial.port ----+
                  |
             USB Device Layer
                  |
          Runtime-owned USB Host
                  |
                USB OTG
```

The key architectural rule is that **USB is a transport, not the application capability**. Serial Monitor consumes `serial.port`. Firmware Flasher consumes `program.esp_rom`. The ESP ROM provider may acquire `serial.port`, which may currently be supplied by USB. Future providers may satisfy these capabilities over other transports without changing the apps.

## 4. Phase 0 — freeze current behavior

Before extraction, document and/or test the behavior that must survive the migration. At minimum capture:

- USB OTG/VBUS power behavior and timing;
- attach/detach behavior;
- supported USB serial devices/interfaces currently working;
- baud/data/parity/stop/flow-control behavior currently exposed;
- Serial Monitor receive, transmit, scrolling, input and navigation behavior;
- Firmware Flasher firmware selection, ROM boot entry, synchronization, erase/write, progress, hashing/MD5 verification, reset and error behavior;
- existing timeout/retry behavior required for devices that work today.

Add useful logging at the runtime/provider boundary so regressions can be distinguished between enumeration, capability resolution, stream movement and protocol failure.

## 5. Phase 1 — extract runtime-owned USB

Move existing working common USB OTG code out of the apps with minimal behavioral redesign.

Runtime/provider code SHALL own:

- USB host initialization/shutdown;
- VBUS/power control and required power locks;
- enumeration and detach handling;
- interface and endpoint discovery;
- interface claiming/release;
- USB transfer submission/completion;
- control transfers;
- transfer buffers owned by the USB/provider layer;
- timeout/error recovery;
- deterministic device teardown.

Applications SHALL NOT directly call ESP-IDF USB host APIs after migration.

The first extraction milestone is successful when both applications retain existing behavior while their former USB implementation has been replaced by calls into runtime/provider code. Protocol-specific flashing logic may remain in the Flasher during this phase.

## 6. Phase 2 — minimal USB device objects and opaque handles

Enumerated USB devices SHALL become runtime-owned objects. The minimum device record should include runtime identity, transport=`usb`, VID/PID, interface information, presence/state, provider binding and advertised semantic capabilities.

Application/provider boundaries SHALL use generation-safe opaque handles rather than persistent raw pointers to USB objects. Handle resolution must validate ownership/type/generation before use.

This is the reference implementation pattern for later BLE, UART, I2C, SPI and other device providers.

## 7. Phase 3 — `serial.port` capability

Implement the minimum resolver/lease functionality necessary for a provider to register and a consumer to acquire:

```text
serial.port
```

A request MAY constrain a particular device when the user/application has selected one, but SHALL NOT name the concrete USB driver implementation.

A `serial.port` lease SHALL provide semantic serial operations rather than USB operations. The minimum contract includes:

```text
configure(baud, data_bits, parity, stop_bits, flow_control)
rx_stream
tx_stream
status
close/release
```

No application-facing API should expose CDC control requests, endpoint addresses, transfer descriptors or other USB implementation details.

The design SHALL permit future UART/BLE/network providers to supply `serial.port` without changing Serial Monitor.

## 8. Phase 4 — Serial Monitor conversion to streams

This is the first required end-to-end capability-driven reference application.

Serial Monitor SHALL:

1. request/select a `serial.port` capability;
2. configure it using semantic serial settings;
3. consume its RX stream;
4. write user input to its TX stream;
5. respond to semantic status/disconnect results;
6. release the lease by normal execution-context teardown or explicit close.

Serial Monitor SHALL NOT own USB transfer buffers or USB enumeration/claim/control logic.

The existing UI, terminal rendering, scrolling, input, baud selection, navigation and other presentation behavior SHOULD remain unchanged during transport refactoring.

The RX/TX streams SHALL use bounded buffering and explicit backpressure/overflow/disconnect semantics consistent with `STREAM_PIPE_ARCHITECTURE.md`.

### Milestone A — architectural proof

When Serial Monitor operates entirely through `serial.port` + streams with no USB knowledge, RiscRTE has a complete baseline proof of:

```text
application -> semantic capability -> resolver/lease -> provider -> stream -> transport
```

Do not delay this milestone for features not required to preserve existing Serial Monitor behavior.

## 9. Phase 5 — ESP ROM programmer provider

Extract the working non-UI ESP ROM flashing engine from Firmware Flasher into a reusable provider exposing:

```text
program.esp_rom
```

The provider owns protocol behavior including, as applicable to the current implementation:

- DTR/RTS ROM bootloader entry sequencing;
- ROM synchronization;
- SLIP/esptool framing;
- ROM command encoding/decoding;
- erase/write sequencing;
- timeouts/retries;
- progress calculation;
- MD5/hash verification;
- reset/run sequencing;
- protocol-specific errors.

The ESP ROM provider SHALL consume `serial.port`; it SHALL NOT directly own USB host infrastructure.

Firmware Flasher retains application responsibilities such as firmware selection, user confirmation, progress/result presentation and navigation.

## 10. Phase 6 — firmware stream and programming job

`program.esp_rom` SHOULD expose a bounded operation represented as a job or equivalent owned operation:

```text
start_program(target, firmware_stream, options) -> job_handle
```

The firmware image SHALL be supplied as a stream/file-stream rather than being materialized as a large application-owned buffer.

If the ESP protocol requires a hash before programming, the initial implementation MAY reopen/seek and perform two streaming passes rather than implementing generalized stream tee/replay. Do not add a large RAM buffer merely to avoid a second file pass.

The design should allow future checksum/progress transforms without making them prerequisites for this migration.

### Milestone B — composed capability proof

When Firmware Flasher operates as:

```text
Firmware Flasher
      |
program.esp_rom
      |
ESP ROM provider
      |
serial.port
      |
USB serial provider
      |
USB
```

RiscRTE has demonstrated that one semantic capability/provider can itself consume another capability while the application remains independent of both transport and lower-level protocol implementation.

## 11. Phase 7 — capability loss and deterministic failure

USB removal SHALL propagate upward through the architecture rather than requiring USB-specific application logic:

```text
USB removed
 -> device unavailable
 -> serial.port lease revoked/unavailable
 -> RX/TX streams disconnect/finish
 -> dependent programming job fails/cancels
 -> application receives semantic result
```

Serial Monitor should receive a serial/device-disconnected result. Firmware Flasher should receive a programming-target-lost/failure result. Neither should need to interpret USB errors.

## 12. Phase 8 — execution-context ownership

All app-acquired resources in this slice SHALL converge on the execution-context ownership model.

Typical Serial Monitor context:

```text
serial.port lease
RX stream
TX stream
UI/session resources
```

Typical Firmware Flasher context:

```text
program.esp_rom lease
firmware input stream
programming job
UI/session resources
```

The Flasher application does not own the underlying USB device simply because its provider ultimately uses USB.

On application unload/exit/crash, the runtime SHALL be able to cancel/revoke/close/release all resources owned by that invocation without invoking stale ELF callbacks.

## 13. Reference-app requirements

After successful migration, documentation SHALL identify:

- **Serial Monitor** as the reference for a long-lived bidirectional capability + stream application;
- **Firmware Flasher** as the reference for a bounded semantic capability + job + input-stream application;
- the USB serial provider as the reference transport provider that converts hardware transfers into semantic capabilities and streams;
- the ESP ROM provider as the reference composed provider that consumes another semantic capability.

Future hardware-facing apps SHOULD be compared against these examples before introducing app-private transport code.

## 14. Compatibility and migration rules

Existing working implementation details may be retained temporarily behind the new provider boundary. Migration should prefer moving proven code rather than rewriting it unnecessarily.

During conversion:

- maintain working device compatibility and timing;
- avoid simultaneous UI rewrites unless required by the capability boundary;
- avoid introducing a second USB stack alongside the proven implementation;
- remove duplicate app-owned USB code once equivalent provider behavior is verified;
- do not expose concrete USB implementation objects through the new capability APIs;
- do not make the new APIs ESP-ROM-specific where a generic serial abstraction is sufficient;
- do not make the generic serial capability aware of ESP flashing.

## 15. Validation matrix

The migration is incomplete until at least these cases are exercised:

```text
Serial Monitor opens supported USB serial device
Serial Monitor configures multiple baud rates
RX sustained traffic
TX sustained traffic
bidirectional traffic
Back/Home/app exit during active stream
USB disconnect while idle
USB disconnect during RX/TX
reconnect/reopen
repeated open/close without leaked resources
Flasher enters ESP ROM bootloader
Flasher synchronizes
Flasher programs known-good firmware
progress remains functional
MD5/hash verification remains functional
successful reset into programmed firmware
USB disconnect during hash
USB disconnect during erase/write
app exit during programming
program failure followed by successful retry
stale handle rejected after close/reuse
all execution-context resources reclaimed after app unload
```

Logging should make the responsible layer identifiable for failures.

## 16. Completion criteria

This high-priority roadmap item is complete when:

1. neither Serial Monitor nor Firmware Flasher directly owns USB host/device/transfer infrastructure;
2. Serial Monitor uses `serial.port` and bounded streams for RX/TX;
3. Firmware Flasher uses `program.esp_rom` rather than USB-specific programming logic in the app;
4. the ESP ROM provider obtains its transport through `serial.port`;
5. firmware data reaches the programmer through a stream/file stream without large app-owned whole-image buffering;
6. USB disconnect propagates as capability/stream/job state rather than USB-specific app handling;
7. leases, streams and jobs are execution-context owned and deterministically reclaimed;
8. existing supported Serial Monitor and Firmware Flasher behavior is preserved;
9. the resulting code and documentation are suitable as the canonical capability-driven examples for future RiscRTE app development.

## 17. Recommended implementation order

Execute in this order to maximize architectural progress while minimizing simultaneous change:

1. freeze behavior/tests/logging;
2. extract common runtime-owned USB host/device/serial code;
3. introduce USB runtime device records and opaque handles;
4. implement minimum capability registration/resolution/leases;
5. expose USB serial as `serial.port`;
6. convert Serial Monitor to `serial.port` + streams — **Milestone A**;
7. extract ESP ROM protocol as `program.esp_rom` provider consuming `serial.port`;
8. convert firmware input to a file stream and programming operation to a job;
9. implement disconnect -> revocation -> stream/job termination;
10. complete execution-context ownership/teardown;
11. designate/document the two apps as reference implementations;
12. remove obsolete duplicated compatibility paths after regression verification.

The implementation should favor a working vertical slice over broad framework completeness. Each new primitive introduced by this job must nevertheless be reusable and consistent with the RiscRTE master architecture.