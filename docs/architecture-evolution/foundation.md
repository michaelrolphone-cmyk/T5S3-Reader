# Firmware foundation and product boundaries

This analysis starts from the owner's U1–U4 roadmap, not a replacement roadmap. Its parallel constraints are expressive fast e-paper, the smallest firmware foundation that preserves the current experience through apps/drivers, and CrossPoint product parity. Daily usefulness cuts across all three: this hardware is already a mobile device, desk clock, engineering workbench, toolbox and toy. The LCD/watch direction tests the abstractions; it does not define the whole platform.

## What the four milestones already decide

All four master milestone specifications were read in full at Reader `3300229d0a232b4e6047a7c93b2f518c033c3cfa`, together with the sequencing and linked stream, bus, shared-owner, headless and provisioning contracts. The later sequencing, U3 and USB-remediation amendments on active U1 head `8d8f2472fa91b6e5797daa9f03f997ae097b48fa` were then read in full; U1/U2/U4 main documents are unchanged there. Those amendments override older exclusions [E10](evidence.md#e10-roadmap-authority).

| Stage | Existing architectural decision | Review implication |
| --- | --- | --- |
| U1 | Generic ELF endpoints first; stable I²C bus ELF; physical USB and actual VBUS chip owners; one four-kind archive/transaction engine; signing purge and launch-verification performance | Complete these foundations, not another stream runtime, manager or firmware USB proxy |
| U2 | Independent package repositories, SDK/build inputs, immutable sources, dependency plans and profile-specific system roles | Earlier SDK/provenance advice largely restated U2. Repository preparation alone is not owner acceptance of U1 or advancement into U2 |
| U3 | Fitted Pro peripherals and native bus cutovers; one owner per shared chip/pin/rail; GNSS and active Wi-Fi boundary migration; compiled GUI becomes optional; recoverable driverless CAM idle | This is the first concrete proof that the firmware can exist without the reader/display product. UI-to-ELF extraction is explicitly not this milestone |
| U4 | Provision through the same manager from a verified bootstrap route; actual CAM driver; one real saved/retrievable image while retaining Pro | Proves a second device graph, not universal ESP32 support, network-first provisioning or a continuous camera product |

U1's older workstream order and broad test-gate language are constrained by the later sequencing/workflow documents. Requirements are not implementation evidence. The open U1 branch and separate migration repositories do not establish accepted completion of all stages.

## The smallest defensible foundation

“Smallest” should mean the transitive code/resources needed to boot safely, obtain approved packages, resolve them and recover, rather than the fewest source lines or the most ELF files. A firmware byte target cannot be derived from filenames: measure the linked image and boot-resident working set after a real configuration exists.

| Responsibility | Why some root must exist | Boundary and current evidence |
| --- | --- | --- |
| CPU/boot/OS port | Reset, executable memory, scheduling, cache/interrupt/DMA primitives and recovery entry cannot depend on code not yet available | Per-CPU/ABI implementation; generic runtime above it. Current builds are S3/PSRAM-oriented; the structural app validator requires Xtensa machine 94 [E13] |
| Obtain initial package bytes | The storage or network driver cannot fetch itself from an inaccessible medium | A verified, bounded boot-only reader/staging route or already accessible boot package set; relinquish physical ownership before normal providers. Current source mounts SD before normal startup [E11] |
| Loader and generic supervision | Admit compatible bytes, relocate, preserve import policy, invoke lifecycle and retain mappings on uncertain stop | Contexts, capabilities, rights, generations, dependency pins and opaque exclusive claims; no chip-specific decision tables. Native ELFs are not memory-isolated merely because they are packages |
| Package integrity/recovery bootstrap | Interrupted replacement and installation of a ZIP service must not destroy the only route back | Keep the one ordinary engine and minimum archive reader needed for bootstrap; existing staged transactions are assets, not rewrite targets |
| Minimal inventory/profile evaluation | Select a coherent required set and reject unavailable/ambiguous dependencies before activation | Runtime validates bounded metadata/opaque claims; driver interprets physical pins/chips. A profile is not a permission grant or evidence that a guessed board matches |
| Product behavior and graphical UX | Necessary for the selected user experience, not for every supported device's safe idle | Reader engine, settings screens, fonts, shell and transfer UI can be profile/product dependencies. U3 intentionally leaves GUI compiled but inactive; full packaging is a further scope decision |

The older master U3 text excluded integrated Wi-Fi/BLE from board-added conversion. The later active sequencing/U3 amendment explicitly requires auditing `Esp32NetworkProvider.cpp` and moving actual Wi-Fi hardware/connection policy into an installed provider, retaining only justified minimal CPU/OS/boot-recovery primitives. Wi-Fi being integrated is not permission to retain a normal firmware driver. Do not conflate that explicit Wi-Fi work with unused BLE migration, which is not added for coverage. Conversely, deleting the only boot/recovery reader merely because it touches hardware would break the architecture.

## Close the bootstrap dependency graph before deleting code

The intended order is:

1. CPU/port and a genuine recovery route
2. Accessible package bytes and recoverable installed inventory
3. Compatible profile/dependency plan with authorization and exclusive resource claims
4. Bus/chip/power providers, then device and service capabilities
5. Optional product UI/apps

At zero installed drivers, missing display, touch, removable media or provisioning manifest must lead to observable recoverable idle, not an error screen that itself requires the missing drivers. A download path starts only after a functioning authorized network/storage chain exists; online availability cannot be a prerequisite for repairing that chain.

For each selected device, explicitly identify the first readable package source, how it is populated, its bounds, its own power/pin prerequisites, the last-good recovery path, and the moment bootstrap hardware ownership is released. The current SPIFFS partition entry is not evidence that a boot package store is implemented. U1 also distinguishes install/recovery integrity checks from cheap installed-generation lookup: its permanent receipt/invalidation requirement must be evidenced before treating metadata-only inspection as a durable integrity cache. Rehashing every ELF on each interaction and assuming writable media never changes are both unsuitable substitutes. CAM sensor/media/pins and the future watch remain unverified.

Credible choices:
- **Narrow boot-only media reader, then exclusive provider handoff.** Closest to the allowed roadmap. Low duplication only if its scope stays bounded; risky if a retained file handle or shared pad prevents native takeover.
- **Pre-staged required driver packages in an independently readable boot store.** Breaks a removable-media/network cycle, but needs measured flash capacity, package-generation recovery and an actual population/update route. Packaging drivers with initial deployment need not mean compiling their device behavior into core. This is an alternative to assess, not an existing facility.
- **Compile normal drivers/network/UI into a permanent rescue fallback.** Easiest initial availability but hides provider absence, duplicates owners and grows core. Reject as the normal architecture; narrowly isolated ROM/recovery remains allowed.

Falsify the first choice if its medium cannot be read without the driver being loaded, or if boot and runtime controllers cannot hand off exclusively. Falsify the second if capacity/update recovery makes it unable to retain a usable boot set. Do not pick either from an assumed CAM/watch BOM.

## Current coupling that matters

At the inspected master:
- `setup()` initializes board GPIO/power/clock/tilt, mounts SD, and on SD failure initializes display/fonts to show an error. Normal startup also initializes display/fonts and the render activity task [E11].
- `MappedInputManager::update()` performs generic-device discovery, navigation and touch work from the UI input path. Main-loop sleep/button policy knows board/button roles and reader state [E11].
- `runNativeApp()` needs renderer/input references and a `/sd/` path, creates a UI session, and starts settings/network/stream bridges. This is a useful current app host, not yet the UI-free application/service root described by the roadmap [E12].
- `settings.elf` delegates settings rendering/activation to firmware; `file_transfer.elf` requests a compiled transfer activity. The reader dispatches compiled EPUB/TXT/XTC/BMP activities [E12].

Therefore “there is an ELF for this menu” is not evidence that its functionality left firmware. Preserve these paths while separating product policy from the mechanisms they use. U3's optional-UI stage is the safe prerequisite; do not silently convert this analysis into an immediate GUI-ELF implementation order.

## Product boundary that serves CrossPoint and a smaller core

Prefer a coherent reader application/product bundle over either an ever-growing product-specific firmware ABI or one ELF per tiny function. Reader parsing/layout/navigation, reading settings and content-state policy belong together unless a second real consumer justifies a reusable service. Display/input/storage/network/time/power are capability contracts; trusted resource selection and permissions remain explicit. Shared reusable rendering/decoding can be a library or optional service without becoming a universal firmware dependency.

The first compatibility target should be a pinned upstream reader revision and a declared source/build interface, with a feature/behavior fixture. “Their app” is not yet a supplied RiscRTE ELF. Do not promise to load a complete upstream firmware binary as an app, or to make different ISAs share machine code. Keep current reader behavior as the comparison during extraction; removing duplicate firmware logic is the final consequence of demonstrated ownership transfer, not the first step.

A thin source-facing compatibility layer over stable capability services is preferable if it lets upstream reader code evolve with limited adaptation. Reject it if it simply forwards every reader function to compiled product code: that recreates today's bridge problem. Compare the source adaptation size and retained product-specific firmware symbols, not just a successful entry point.

## Daily utility is an architectural acceptance constraint

The owner uses serial/USB tools and firmware flashing at work, keeps the device as a desk clock, and intends external I²C gadgets. These are not disposable legacy examples. Preserve their user-visible behavior and safe state while changing ownership: an in-use programmer/serial dependency cannot be updated or powered down beneath a job; leaving a game must restore navigation/display; sleep/wake must preserve the clock/reader experience and recoverable state.

A concrete counterexample to naive extraction is the desk clock: its current retained timer-wake path paints without mounting SD, discovering apps, starting touch/network or running ActivityManager [E14]. A future packaged clock needs an equivalent small wake-dependency path; forcing full package/UI startup every minute could regress responsiveness and power. Retained state should identify a versioned resumable workflow, not persist cross-reset ELF pointers. Until an equivalent path is demonstrated, preserve this daily behavior rather than declare it unnecessary firmware.

Use the existing package-use gates, execution-context teardown and failed-quiesce retention. For each proposed extraction record the affected daily workflow, safe refusal/defer behavior, last-good firmware/package set and a verified recovery route if the primary UI or USB path is unavailable. An attractive reduction in firmware bytes is a regression if routine launch latency, power behavior, tool availability or return-to-Home reliability deteriorates.

The future consumer ecosystem strengthens the case for understandable compatibility and recovery, but does not authorize a marketplace, publisher onboarding, signing or new security system. Current U2 source management and U4 provisioning remain the bounded foundation.

## Keep expressive e-paper on the fast side of the boundary

U1–U4 already allow owned frames/chunks rather than stuffing full frames into 512-byte records. Dynamic resolution and authorization belong at acquisition/session boundaries; the render/scan loop should use bounded pinned interfaces or owned buffers, without per-pixel resolver calls, unbounded global-lock I/O or automatic full-frame copies.

Current Hollow Trail overlaps packing with scanning, preserves a no-copy path when a backbuffer is free, and records render/pack/wait/copy/input plus scan preparation/DMA/pacing. The scan engine owns two buffers, transition state and DMA rows [E14]. Preserve those performance mechanisms while moving panel-specific behavior into its actual owner. Generic control does not require a lowest-common-denominator scan algorithm.

Performance evidence must compare identical scene/quality, frame ownership and app/scan scheduling before and after a boundary move. Host render timings exclude panel/DMA/input costs; scan rate is not application FPS. Optional e-paper quality/low-latency behavior can coexist with a portable default and clear unsupported-feature results for reflective LCD/AMOLED. No universal buffer count, refresh waveform, PSRAM budget or touch input follows from the capability name.
