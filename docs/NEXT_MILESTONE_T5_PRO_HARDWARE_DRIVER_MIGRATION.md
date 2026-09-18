# U3 — T5 Pro hardware driver migration and UI-optional headless boot

**THIRD MILESTONE; SPECIFICATION ONLY.** Begins only after accepted U2 and explicit owner direction, on a new milestone implementation PR. U3 is not a permission to merge, publish, flash or declare physical qualification. Governing docs: [four-milestone execution order](FOUR_MILESTONE_STREAM_FIRST_EXECUTION_ORDER.md), [UI-optional headless boundary](HEADLESS_RUNTIME_AND_CAMERA_SERVICE_BOUNDARY.md), [hardware-agnostic driver boundary](HARDWARE_AGNOSTIC_DRIVER_BOUNDARY.md), [stream work allocation](STREAM_PIPE_MILESTONE_ALLOCATION.md), [runtime driver architecture](RUNTIME_DRIVER_ARCHITECTURE.md), [U1](NEXT_HARDWARE_TEST_MILESTONE.md), [U2](NEXT_MILESTONE_INDEPENDENT_PACKAGE_ECOSYSTEM.md) and [U4](NEXT_MILESTONE_PROVISIONING_AND_ESP32_S3_CAM.md). Inspect actual accepted runtime, Pro board revision/BOM/schematic, linked hardware code and real owner's CAM flash/PSRAM/boot facts before choosing driver scope. Pin declarations alone are not proof a component is fitted.

## Objective and boundary

U3 has three outcomes: **(1)** every verified T5 Pro board-added peripheral's actual hardware handling migrates to separate functional installable driver/provider ELFs; **(2)** graphical UI remains compiled into firmware but is **optional at runtime**, with generic boot, package manager, scheduler, provider and service lifecycle independent of rendering, touch and foreground apps; **(3)** an applicable generic runtime image can be flashed to the owner's ESP32-S3-CAM, booted with **no Pro or CAM peripheral packages**, and run stable, observable, recoverable headless idle. No provisioning or camera capture until U4. The headless serial interface in this roadmap means supported one-way diagnostics/recovery, **not** an interactive terminal.

Excluded from U3 *board-peripheral conversion*: ESP32-S3-intrinsic CPU, integrated Wi-Fi/BLE, flash/PSRAM substrate, on-chip controller/port primitives and ROM/RTOS generic bootstrap. Use generic port functions/reusable bus providers; don't add board-specific hardware code to core. USB extraction is U1. No arbitrary new hardware families, full UI-to-ELF migration, first-run provisioning or CAM sensor drivers.

## Actual Pro hardware inventory and migration

At kickoff label each schematic/source candidate `PRESENT`, `VARIANT-DEPENDENT`, `ABSENT` or `UNCONFIRMED`, then for every confirmed part record ELF package/physical owner, lower dependencies, profile pin/bus/power data, consumer API, safe absence/unload and validation:

| Fitted candidate (verify revision) | Owner/semantic interface | Essential rule |
| --- | --- | --- |
| ED047TC1 e-paper panel and signals | Real EPD driver `display.*` | Own scan/waveform/panel pins, full/partial refresh, completion, power and bounded frame transfer |
| TPS65185 power and PCA9535 outputs | Shared EPD-power/expander owner | Safe HV/VCOM/power sequencing, single register/rail owner |
| GT911 touch | Touch ELF `input.touch` | I2C/IRQ, orientation/calibration, bounded records/events |
| SX1262 LoRa and RF enable | LoRa ELF `radio.lora` | SPI/IRQ/BUSY/reset, atomic RX/TX packets, no boot-time transmission |
| GNSS receiver/connector if fitted | GNSS ELF `location.*` | Reuse `location.fix.v1`; move firmware UART/parser/rail/poll residue to actual drivers |
| BQ25896 charger/BQ27220 gauge | Charger/battery ELFs | Battery/OTG/USB safety, appropriate telemetry and safe defaults |
| PCF85063 RTC | RTC/time ELF | I2C, alarm and sleep/wake ownership |
| PCA9535/buttons/board controls | Expander and input provider | One physical owner for shared pins and outputs |
| SD slot and media | Normal runtime storage ELF | Separate minimal isolated module-store/bootstrap/recovery path; explicit handoff |
| Other proven lighting/wake/power devices | Relevant ELF/provider | No imagined devices, maintain safe electrical idle |

Candidate I2C addresses from `lib/Board_T5S3/pin.hpp` include GT911 `0x5D`, PCF85063 `0x51`, TPS65185 `0x68`, PCA9535 `0x20`, BQ25896 `0x6B`, BQ27220 `0x55`. Verify actual revision and multiplexed SD/LoRa/EPD pins, power dependencies, interrupts and access exclusivity. SPI mutex alone does not solve pad-mux conflicts. Driver ELFs own real register I/O, probe, physical initialization, controller transfers, interrupts, DMA, power, fault recovery and teardown; thin proxies to firmware hardware implementations fail scope. Generic core handles only opaque manifest, capability resolver, rights, stream/event routing, lifecycle and package loading. Providers coordinate resources and quiesce before module unmapping; when unsure pin/quarantine rather than call unloaded code.

Consumers must use U1's generic data-plane contract: LoRa packet boundaries and metadata as typed atomic records, GNSS byte/position records, touch samples/events, other telemetry as appropriate, SD bytes and an EPD surface/frame capability with bounded chunks or owned buffers. Never force a full display frame into the v2 512-byte record queue, or keep DMA/ELF pointers after release. Follow [cooperative bounded operations](COOPERATIVE_BOUNDED_OPERATIONS.md): bytes/items and elapsed-time checkpoints, real scheduler yields, finite retries/timeouts/queues/memory, progress and recovery. Only add optional stream transforms if a required path actually needs them. Preserve one `.rte.zip` per package and U2 independent source/version/role; Pro `system-core` never means universally required on CAM.

## Make UI optional without converting it into an ELF

Conditional boot sequence: generic CPU/port and minimal recovery/module-store -> manager inventory and selected profile -> generic scheduler, providers and services -> **only then optional GUI initialization if profile requests it and working `display.*` exists**. Existing scene/navigation/render/Settings may remain firmware code. UI is a consumer of generic display/input driver capabilities; hardware panel/touch operations are not embedded in UI/core. Missing display, touch, icon/font assets or graphical app must not prevent boot, generic service work, inventory/recovery, sleep/idle or diagnostic reporting. Do not allocate framebuffer/font/EPD buffers or schedule render/ActivityManager ticks on a headless profile. A missing optional display or UI failure skips/stops graphical components safely; T5 Pro retains full functionality with its installed panel/touch drivers. Scheduler must not depend on UI event polling for progress. No replacement GUI, UI ELF, shell or interactive serial management.

## Driverless ESP32-S3-CAM acceptance

The owner's **actual** CAM boots a compatible U3 firmware image with zero T5/zero CAM-specific driver packages, and without screen, touch, Pro power IC, LoRa, RTC, gauge, optional SD, GUI app, installed camera driver or provisioning manifest. Missing capabilities mean dependent apps do not run; they never trigger crashes, watchdog resets or endless missing-driver/SD retries. Preserve a true supported minimal boot/recovery source even where there is no SD card; do not claim normal storage capability from boot-only reader. Basic one-way port/ROM/serial status is sufficient if supported, reporting startup and absence errors; interactive setup not required. No fake display or camera capability. Stable low-work idle, watchdog health and recoverability are success even if nothing runs.

Check actual port, bootloader/partition/flash/PSRAM/pin compatibility on both boards. Prefer an identical flashable binary when feasible; otherwise smallest documented bootstrap/layout/port distinction with same hardware-blind runtime source and ABI—never compile in CAM/T5 peripheral selection. Optional display buffer requirements must not break RAM/PSRAM-constrained headless boot.

## Execution, Work Complete and handoff

Maintain a compact mapping from verified hardware and reached legacy firmware owner to actual ELF/package/profile, generic consumer/data path, safe absence, removal of compiled fallback and relevant check. Implement complete slices plus UI-optional bootstrap and driverless CAM-compatible firmware on one U3 PR. Run proportionate firmware/ELF tests, source/link audits and absence/recovery checks; CI is feedback, not a stop-work gate; no repeated intermediate owner physical testing. **Work Complete** means fitted peripherals have real ELF functionality, GUI still runs on Pro drivers, generic runtime boot/idle does not require UI or T5 peripherals, compatible CAM image and no known blocking source/build defect. Actual CAM flash/stable idle and Pro hardware functionality await explicit owner **Release Qualification**, not assumed from builds. Owner alone approves merge/release/flash and U4 advancement.

**U4 handoff:** [U4](NEXT_MILESTONE_PROVISIONING_AND_ESP32_S3_CAM.md) adds real CAM-specific drivers and headless provisioning, then proves function by capturing and writing **one actual test image to a verified available output**. A continuous camera-streaming service, network client and persistent autostart are later optional work, **not** U4 requirements. U4 must not fix a U3 T5-only boot path by compiling CAM hardware or a new headless UI into core.
