# RiscRTE Multi-Target Programmer and Debugger Architecture

## Status and authority

**Normative target.** Governed by [RISCRTE_PLATFORM_SPEC.md](RISCRTE_PLATFORM_SPEC.md), [HARDWARE_AGNOSTIC_DRIVER_BOUNDARY.md](HARDWARE_AGNOSTIC_DRIVER_BOUNDARY.md), [USB_OTG_HOST_ARCHITECTURE.md](USB_OTG_HOST_ARCHITECTURE.md), and [RUNTIME_DRIVER_ARCHITECTURE.md](RUNTIME_DRIVER_ARCHITECTURE.md). Earlier diagrams placing a `T5 USB Core`, hub topology, USB controller, class/transfer ownership or USB-specific device selection in the framework are superseded. A hardware-blind RiscRTE core resolves arbitrary provider capabilities, execution contexts, generic leases/streams/events/jobs and policies. **Installed USB-host, hub/class/probe, serial, power and programming-protocol ELFs implement the actual hardware/protocol operations.** This specification does not imply that all target/probe providers already exist.

## 1. Use case and architecture

Use one powered hub to connect an FTDI/UART adapter to an ESP target, an ST probe to an STM32 target, and an MSP probe to an MSP target simultaneously. The USB host/controller ELF owns hub topology and physical transfers; separate adapter/probe ELFs consume its `usb.host` capability and publish `serial.port`, `debug.swd`, `debug.jtag`, `program.transport`, or vendor-specific capabilities. Target protocol ELFs consume these transport capabilities and implement the complete bootloader/program/verify/debug algorithms. A generic job facility orchestrates software jobs; a Programmer app presents selection, progress/results and audit. No compiled RiscRTE USB core or compiled target protocol is permitted.

```text
Programmer app ELF
    -> generic job / intent / stream capabilities
    -> program.esp_rom / program.stm32 / program.msp provider ELFs
    -> serial.port / debug.swd / debug.jtag / vendor transport provider ELFs
    -> USB class/probe provider ELFs (FTDI, ST-LINK, MSP/FET, ...)
    -> usb.host provider ELF -> USB controller/hub hardware

All dependency names above are opaque to RiscRTE core.
```

Sequential ESP -> STM32 -> MSP programming while all probes remain attached is the initial required flow. Parallel execution is optional future work requiring validated bandwidth/power/memory/CPU/probe independence. An adapter is not a target programmer: FTDI implements transport; Espressif ROM protocol implements target flashing.

## 2. Provider boundaries

The USB-host ELF owns controller/OTG/VBUS (or consumes a power-provider capability), enumeration/hub ports, addresses, endpoint/interface grants, transfers, hotplug, reset and recovery. USB adapter ELFs own their class/vendor matching, descriptor parsing, setup/configuration, physical serial/debug transfers and recovery. Target protocol ELFs own target-specific identification, ROM/SWD/DFU/JTAG/SBW operation, boot/reset, erase/write, progress, verify, target error handling and device-safe cancellation. They MUST NOT implement a duplicate USB host stack; instead they consume the appropriate provider capability. The core cannot contain VID/PID dispatch, a USB hub manager, a hard-coded `program.esp_rom` hardware bridge, or any USB/target-specific driver code.

A provider may combine functions into one ELF or compose several ELFs; it may not delegate device implementation to a firmware proxy. An additional USB chipset/probe/target family must install with only package/profile changes, not a RiscRTE firmware rebuild. An unavailable or corrupt provider must cause capability loss, never silent native fallback.

## 3. Discovery and multiple device identities

The host and adapter ELFs detect physical devices and publish generic bounded device records to the shared registry, which does not know USB. Inventory supports multiple simultaneous records with unique generation-safe opaque handles, provider IDs, capability advertisements and optionally provider-owned metadata such as hub path, USB serial and VID/PID. For an operator's durable probe binding, prefer reported serial number, explicit alias tied to it, weaker VID/PID plus topology path, or explicit interactive selection. A USB address is not durable identity. Replug revokes old handles even when reported properties match; no job may silently attach to a replacement.

Representative records: FTDI adapter with `serial.port`/control lines, ST probe with `debug.swd`/`program.stm32`, MSP probe with `debug.msp`/`program.msp`. One physical probe may advertise several semantic capabilities. Jobs bind an exact selected handle/target, not a singleton called 'the USB device' or 'the programmer'.

## 4. Transport contracts and hardware arbitration

Transport capabilities may include `serial.port`, `serial.control_lines`, `usb.dfu`, `debug.swd`, `debug.jtag`, `debug.sbw`, `debug.vendor.stlink`, `debug.vendor.msp` and `program.transport`. A job obtains one generation-safe context authorization grant to a selected transport provider. **The providing ELF**, possibly in cooperation with its host-controller ELF, owns the real physical claim, concurrent-access arbitration and device session. The core's generic grant/lease tracks policy and lifetime but cannot configure USB endpoints, lock a USB interface or power-cycle a probe. A session may be exclusive where the provider's hardware/protocol requires it. Different probes remain independently available.

## 5. Target programming protocols

The Espressif provider ELF supports ROM boot control using advertised semantic serial DTR/RTS or a manual reset prompt, SYNC/SLIP framing, chip identification, erase, write, verification/hash and reset/run. It works over FTDI, CP210x, CH34x or native UART when the required serial capability is equivalent. STM32 providers declare exact ROM UART, USB DFU, ST-LINK SWD/JTAG target families and flash geometry; do not assume all STM32 devices share bootloaders. MSP providers declare supported families/transports (such as SBW/JTAG/vendor), not a universal MSP protocol. Generic SWD/JTAG is preferred when sufficient; vendor extensions remain separate optional capabilities. Target identification precedes destructive work unless an explicitly verified fixed-target expert policy permits otherwise.

## 6. Independently distributed modules and metadata

Examples: `usb-host.elf`, `usb-ftdi.elf`, `usb-stlink.elf`, `usb-msp.elf`, `program-esp-rom.elf`, `program-stm32-rom-uart.elf`, `program-stm32-dfu.elf`, `program-stm32-swd.elf`, `program-msp.elf`. Manifests declare API/ABI, required/provided capability strings, actual target families, transports, image formats, operations, resource needs and signer identity/permissions. Hardware provider packages and programming protocol provider packages are separately updatable. Future `/Drivers`, `/Providers` or `/Services` package layout is implementation-defined and must not become a compiled filename whitelist in the core.

## 7. Job model and orchestration

The generic job facility tracks `QUEUED`, `WAITING_FOR_DEVICE`, `ACQUIRING`, `IDENTIFYING`, `VALIDATING_IMAGE`, `ENTERING_PROGRAM_MODE`, `ERASING`, `PROGRAMMING`, `VERIFYING`, `RESETTING`, `COMPLETE`, `FAILED`, `CANCELLED`, `DEVICE_LOST`. These are optional job-type statuses published by the programming provider; core handles generic job identity, ownership, scheduling, cancellation and bounded progress events without understanding ROM/SWD/USB phases. A replaceable programmer orchestration service/provider handles inventory snapshot, explicit selection, protocol-specific target identification, image validation, ordering, verification policy, retry/cancellation, cleanup and batch status. Protocol ELFs perform device operations; the app provides UI.

A batch is an ordered list of explicit probe, target, image, verification and options records. Initially perform only one destructive operation at a time, while all probes remain enumerated; release the completed job's transport without disconnecting unrelated devices. Future parallel operation checks provider-advertised USB bandwidth, hub/target power, SRAM/PSRAM/transfer buffers, CPU, protocol timing, storage rate and probe independence. No protocol ELF assumes global USB ownership.

## 8. Firmware images, formats and profiles

Represent images as authorized stream/file handles, not whole-file pointers. Sources can be removable, internal, downloaded or packaged volumes; use bounded streaming to target-protocol ELFs, allowing protocol ELF unloading between jobs. Providers declare raw binary, Intel HEX, ELF, UF2, vendor packages or multi-image support as actually implemented. Common format parsers may be independent provider/services, but target layout/address interpretation remains target-provider/profile responsibility. Support multi-segment plans with explicit addresses/regions (bootloader, partition table, app, etc.). Profiles describe expected identity, flash geometry, permitted ranges, boot/reset, transport, mapping, verification policy and post-program reset; a profile cannot bypass security policy.

## 9. Target validation and verification

Before erase/program, target protocol provider detects the target, compares against approved profile, and reports expected target, observed target, chosen probe and image. Mismatch stops destructive operation unless separately authorized expert policy applies. Verification should default to required; methods may be readback, device/probe digest, protocol-native verify or flash hash. Report actual verification method and distinguish successful write without verification from fully verified success. Completing a transfer is not sufficient for success.

## 10. Debug session architecture

Programming is a bounded exclusive job; debugging is a longer-lived exclusive probe/target session. Opaque debug handles bind probe identity, target identity, provider, capability authorization and connection generation. Provider-advertised operations may include halt/run/reset, memory/register access, breakpoints, watchpoints, flash and step, according to target/probe support. Disconnect invalidates the session. A future GDB-compatible service can publish a network or USB-CDC endpoint and consume `debug.swd`/`debug.jtag`; the server and USB/probe implementation remain ELFs, never hard-coded core hardware services.

## 11. Power, hot-unplug, retry and isolation

A USB adapter may not safely power its target; separate power metadata/capability indicates external, probe, hub/USB or unknown supply. Actual power switching is performed by target power/USB/power-provider ELFs, not a framework-owned power rail. A powered hub and electrically safe independent target power are recommended for the reference topology.

Probe loss transitions only affected job/session to `DEVICE_LOST`/`FAILED`; the USB host and other probes remain usable. Provider releases its physical resources while generic core revokes software handles, streams and jobs. Destructive operations never automatically resume on a new device from VID/PID alone; retry reidentifies exact probe/target and has bounded attempts. Batch policies: `STOP_ON_FAILURE`, `CONTINUE_INDEPENDENT`, `RETRY_THEN_STOP`, `RETRY_THEN_CONTINUE`. A sync failure for ESP must not tear down ST/MSP probes; failures are independently logged and batch policy determines progression.

## 12. Audit and security

Per-job audit: timestamp, job/batch ID, reported probe identity/serial and hub path, transport and protocol providers/versions, observed target, image identity/hash, requested operation, bytes, verify method/result, durations, warnings/errors and final status. Log export/persistence use authorized storage capabilities. Trust in an ELF and trust/authorization of a firmware image are separate. Production policies should support signed provider/image packages and target/profile restrictions; development mode may permit user-selected images under explicit warnings.

Applications do not receive unrestricted raw JTAG/SWD/USB simply because the UI programs targets. Privileges are scoped (`program.target`, family-specific programming, `debug.target`, SWD/JTAG) with generic core authorization and provider-side enforcement. Irreversible OTP/fuse/security/readout-protection/option-byte/boot-lock operations require a separate explicit authorization, not an incidental side effect of ordinary flashing.

## 13. Resource and memory lifecycle

A job context owns generic leases, image handles, stream/VM mappings, software power-policy requests, deadlines and progress subscriptions. Provider ELFs own USB interface/transfer/target power hardware sessions and all protocol buffers/callbacks. On complete/cancel/failure/exit/loss, core cancels generic resources, invokes provider cleanup and waits for quiescence; it must not unmap an ELF with outstanding DMA or callbacks. Entire firmware images need not fit in PSRAM: file stream -> bounded chunk -> protocol encoding -> provider transfer -> probe/target.

## 14. UI, diagnostics and acceptance

Programmer UI: connected probes, target association, image selection, job configuration, ordered queue, current operation, per-target progress, verification, errors, batch summary and logs. Distinguish the intended physical target clearly. Diagnostics present the provider-supplied chain: hub/port, USB VID/PID/serial, host/class provider, transport capability, protocol provider, observed target and selected profile/image. Core merely passes generic logs and opaque provider metadata; it does not inspect USB topology itself.

Physical acceptance requires an FTDI/ESP, ST/STM32 and MSP setup on one powered hub: discover three separate devices through ELF publication, bind correctly, execute and verify ESP -> STM32 -> MSP without disconnecting other probes, and produce three independent auditable results. Also verify alternative serial providers, wrong-target rejection, bounded images, disconnect active/inactive probe, replug/stale handles, retry identity, batch failure policy, protocol unload, cancellation/leaks, package signature policy, destructive-operation authorization, address/format validation, distinct hub power errors and debug exclusivity.

**Additional architecture gates:** Normal compiled RiscRTE has no USB host/hub/class/probe or target protocol implementation. Installing a new adapter/target provider needs no firmware rebuild; removing it removes capability and does not fall back to firmware hardware. A thin ELF calling a built-in ROM programmer or USB bridge fails acceptance. Build/host tests do not replace physical verification.

## 15. Implementation order

Build genuine host/hub-controller ELF and full provider publication first, then adapter/probe ELFs, semantic transport capabilities and physically enforced provider leases; implement Espressif target-protocol ELF, the single-job streaming app, target identity/verification/audit/cancellation, ST and MSP provider/protocol ELFs, sequential batch service, shared-hub hardware tests, failure/retry and signing policy, debug sessions and optional parallelism. Preserve tested legacy behavior as migration evidence without extending firmware-side hardware ownership.
