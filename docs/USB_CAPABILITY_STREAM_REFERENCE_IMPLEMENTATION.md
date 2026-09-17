# RiscRTE USB Capability/Stream Reference Implementation

## Status, priority, and authority

**HIGH PRIORITY — target plus historical implementation reference.** Governed by [RISCRTE_PLATFORM_SPEC.md](RISCRTE_PLATFORM_SPEC.md), [HARDWARE_AGNOSTIC_DRIVER_BOUNDARY.md](HARDWARE_AGNOSTIC_DRIVER_BOUNDARY.md), [RUNTIME_DRIVER_ARCHITECTURE.md](RUNTIME_DRIVER_ARCHITECTURE.md), [USB_OTG_HOST_ARCHITECTURE.md](USB_OTG_HOST_ARCHITECTURE.md), [STREAM_PIPE_ARCHITECTURE.md](STREAM_PIPE_ARCHITECTURE.md), and [APPLICATION_EXECUTION_CONTEXT_ARCHITECTURE.md](APPLICATION_EXECUTION_CONTEXT_ARCHITECTURE.md).

**Architecture correction:** The earlier reference milestone moved USB code out of applications into compiled firmware and called the result a runtime-owned USB provider. That achieved partial app decoupling but **did not implement an installable hardware driver**. Firmware-owned USB host/VBUS, descriptor and chipset state, interface/endpoint claims, transfers, class protocols, device projection and USB-specific loader routes are **CURRENT/LEGACY, NONCOMPLIANT**. They remain relevant to regression tests, not to new architecture. A working USB ELF must own hardware implementation through its own code and capabilities supplied by other ELFs. The generic framework MUST know nothing about USB other than that `usb` might be a requested/published capability string.

## 1. Objectives

1. Preserve existing Serial Monitor and Firmware Flasher behavior, timing and supported physical devices during migration.
2. Remove all application-owned USB controller/device/transfer behavior; apps consume semantic capabilities.
3. Remove **all production USB implementation and USB-specific manager/dispatch/discovery code from the compiled RiscRTE core**. Move controller/host, VBUS via optional board power providers, enumeration, hub, class matching, control/bulk transfers, hotplug and recovery into installable provider ELFs.
4. Implement `serial.port` as an arbitrary semantic capability supplied by USB or another provider without core special cases, and bounded RX/TX streams with explicit loss/backpressure behavior.
5. Implement `program.esp_rom` as an installable capability provider consuming `serial.port`, with ESP ROM protocol implementation outside firmware and app.
6. Stream firmware images to programming jobs instead of loading whole images into app memory.
7. Propagate provider/device loss through generic registration, lease revocation, stream/job failure and app-facing semantic results.
8. Bind grants/streams/jobs to execution contexts and deterministically reclaim them; driver ELFs, not the runtime, safely quiesce hardware.
9. Demonstrate that a new transport/chipset works after ELF installation **without firmware recompilation** and disappears after ELF removal with no hidden native fallback.

## 2. Strict responsibilities

```text
Serial Monitor ELF ----requires----> serial.port
Firmware Flasher ELF --requires----> program.esp_rom
ESP ROM provider ELF --requires----> serial.port
USB CDC ELF ---------requires----> usb.host; provides serial.port
USB host ELF ---------provides----> usb.host (and optionally usb)
USB host ELF --optional requires--> board.power.vbus provider ELF

Generic RiscRTE: opaque IDs, dependency resolution, execution contexts,
rights/consent, generic events/streams, package loading/lifecycle only.
```

The core MUST NOT implement `usb.host`, `usb.class.*`, USB enumeration, `open_usb`-specific device I/O, firmware USB host task ownership, hard-coded CDC/CP210x/CH34x logic or special USB provider activation. A driver-specific SDK ABI is allowed, but its implementation resides in ELFs. Lower-level USB host/controller provider owns host/device interface grants and actual transfers; class providers implement their entire protocol and semantic capability data plane. The core's generic lease is permission/lifetime bookkeeping, not possession of a USB interface or physical session.

## 3. CURRENT/LEGACY, NONCOMPLIANT baseline — preserve only as migration evidence

The initial extraction placed USB host setup, OTG power, enumeration, interface claims, transfer submission, class/chipset operations, and teardown in `src/native/NativeUsbBridge.cpp`. The installable `Drivers/usb_cdc/driver.c` contains descriptor matching and line/control encoders, while its `start()`/`stop()` do not manage hardware. `src/runtime/drivers/UsbCdcDriverRuntime.cpp` has a fixed `usb-cdc-acm` load path and a resident fallback. The original `T5UsbClassDriver.h` API treats USB device operations as privileged firmware calls. Firmware-owned USB discovery projection and legacy USB stream interfaces remain elsewhere. Their existence proves a transitional app migration, not hardware modularity or acceptable new precedent.

Existing app-level `serial.port`, stream and programmer-provider portions may be reused where they already satisfy generic boundaries. Do not regress functioning firmware while replacing these legacy paths, but do not add new hardware implementation to compiled firmware or call a proxy-only ELF complete. No code is migrated by this documentation change.

## 4. Phase 0 — behavior freeze and regression instrumentation

Capture working host startup/VBUS timing, attach/detach, supported CDC/vendor adapters and interfaces, line coding/flow settings, serial RX/TX and auto-detect behavior, replug and stale-handle behavior, flasher boot entry/SYNC/SLIP/erase/write/MD5/reset, shutdown, power locks and memory usage. Preserve useful phase diagnostics across the provider/stream/capability boundary without putting USB logic into the logging or event core.

## 5. Phase 1 — genuine installable host/controller provider

Move the **complete** existing functional host-controller implementation into ELF(s). The host ELF performs real hardware initialization, OTG role transition, VBUS sequencing (or consumes a separate charger/power ELF), host worker/task, enumeration/hub/port management, descriptor handling, claims, control/bulk transfers, completion callbacks, timeouts and safe teardown. Its provider ABI exports versioned `usb`/`usb.host` interfaces. Privileged ESP-IDF or MCU access, if needed, is linked into the driver or uses narrow generic platform port primitives; never add an ESP-IDF USB-operation shim to the compiled core. Host capabilities appear only after the ELF successfully starts and publishes them. Absence or invalidity results in unavailable USB capability rather than a firmware fallback.

## 6. Phase 2 — class/vendor/device-function ELFs

Move CDC ACM and vendor-specific serial implementations into class ELFs consuming the host provider. They own descriptor matching, control requests, interface/endpoint selection, setup, line coding, data transfer loops, error/recovery policy and complete teardown. `usb_cdc.elf`, `cp210x.elf`, `ch34x.elf`, `ftdi.elf` and future drivers can publish `serial.port` from USB-host access; no core class enums or VID/PID dispatch. Multiple class drivers and devices may coexist subject to host-provider interface arbitration. Device-mode functions and hub support likewise reside in USB provider ELFs. A packet-encoder-only class ELF is insufficient.

## 7. Phase 3 — generic device publication and capability leases

The USB host/class providers publish copied/opaque device observations through one transport-neutral device-publication ABI. The registry does not enumerate, interpret USB identity or poll provider hardware. Generation-qualified handles are invalidated on detach/rebind; an identical VID/PID replug is not the old resource. Capability acquisition checks generic execution-context authority and rights, then dispatches to the chosen provider. The provider enforces hardware access, performs physical reservation and conflicts, and reports capability loss. Do not create a separate firmware `NativeUsbDevices` manager or special USB registry projection as the target.

## 8. Phase 4 — Serial Monitor

Serial Monitor requests `serial.port`, configures baud/data/parity/stop/flow, consumes a bounded RX stream, writes to a bounded TX stream, observes semantic status/disconnect and releases its grant on exit. UI, scrolling, input, auto-detection and navigation remain app features. App code never parses USB descriptors, claims interfaces, or calls ESP-IDF USB APIs. The same app must work with a non-USB `serial.port` provider without rebuilding firmware or the app.

**Milestone A:** prove `app ELF -> generic capability resolver -> installed provider ELF -> real device I/O -> generic streams` with zero hardware-specific compiled-core calls. App-only transport independence is an intermediate acceptance criterion, not proof of driver independence.

## 9. Phase 5 — ESP ROM provider and Flasher

A separately installable `program.esp_rom` ELF consumes `serial.port` and owns DTR/RTS bootloader entry, SYNC, SLIP/esptool framing, ROM commands, erase/write, timeouts/retries, progress, MD5/hash verification, reset and protocol error attribution. Firmware Flasher ELF owns firmware selection, consent, progress/results and navigation, not USB or ROM protocol. The programmer takes an authorized readable/seekable firmware stream, may use two passes for hash/write, and returns a context-owned operation/job with cancellation and progress. No large whole-image allocation is required.

**Milestone B:** demonstrate `Flasher -> program.esp_rom ELF -> serial.port provider ELF -> device`, with no compiled-in ROM programming engine or USB-specific dispatch. A programming protocol provider may use a different transport in the future by depending on another capability.

## 10. Phase 6 — loss, safety and execution contexts

On physical removal, the responsible USB provider detects and publishes loss, revokes its physical handles, cancels in-flight I/O and quiesces. The generic registry revokes associated context grants, streams become terminal or rebind according to their explicit contract, and dependent jobs fail with semantic target-loss errors. The app receives no raw USB host objects. USB class and host providers must release in correct order; a generic context cleanup callback invokes provider lifecycle and cannot unmap code while hardware callbacks/tasks/DMA can still execute. Handle identity, retry, hotplug and power failures are physically tested; do not infer hardware safety solely from a green build.

Typical Serial Monitor context: semantic lease, RX/TX streams, UI resources. Flasher: programming lease, firmware input stream, job and UI. Physical USB hardware belongs to its driver/provider stack, not to either app context or core.

## 11. Test matrix

Verify: supported CDC and vendor adapters; install new class/chipset with **no firmware change**; remove ELF and prove no native fallback; VBUS startup and hardware boot output; multiple baud/data/parity/stop/flow combinations; sustained RX, TX and full-duplex data; Back/Home/exit under traffic; disconnect idle and during RX/TX; rapid replug/new generation; repeated open/close without power/resource leaks; competing provider/session claims; failed or corrupt driver package; host controller missing; class driver missing; and safe power/role teardown. For the flasher, verify boot entry, synchronization, known-good firmware, streaming progress, hashes/MD5, reset, disconnect during hash/erase/write, cancellation, app exit, failure/retry and stale-handle rejection. Also test build/host suites, package integrity, provider lifecycle and source-boundary guards.

## 12. Completion criteria and implementation order

Proceed in this order: record baseline -> define full USB host/class ELF ABI -> migrate whole host and power-dependent controller I/O -> migrate complete CDC/vendor class operation -> publish devices from providers through generic registry -> remove hard-coded firmware USB loaders, host/device/transfer code, discovery projections, direct USB stream I/O and silent resident fallbacks -> verify app semantic streams -> migrate ROM protocol provider to ELF if still compiled in -> verify end-to-end on hardware. Move code rather than rewriting proven behavior unnecessarily, but **do not preserve a USB operation in firmware merely to reduce implementation effort**.

The reference is complete only when both apps operate through generic capabilities/streams; USB host, class and ESP programming protocols are implemented by installed ELFs; unplug propagates generic loss; context cleanup is deterministic; real hardware parity is demonstrated; the normal core is USB-blind; and adding/removing a device provider requires no firmware rebuild. The prior firmware-host extraction and descriptor-only `usb-cdc-acm` package are transitional milestones, not the completed architecture.
