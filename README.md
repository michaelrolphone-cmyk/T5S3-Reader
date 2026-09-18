# ![RiscRTE](https://raw.githubusercontent.com/michaelrolphone-cmyk/T5S3-Reader/refs/heads/master/docs/riscrte-lockup-descriptor-dark-bg.svg)

**RiscRTE — RISC Runtime Environment** is a modular embedded runtime environment for resource-constrained RISC systems. The current hardware targets are the LilyGO T5 ePaper S3 / T5S3 Pro and LilyGO EPD47 ESP32-S3 e-paper boards.

RiscRTE is no longer defined as a reader firmware tied to a single device. The reader is one application domain built on the runtime. The platform is evolving around dynamically loadable ELF applications, hardware drivers, background services, capability-based hardware access, explicit resource ownership, tiered memory, and a framework-owned UI/runtime.

> **Naming:** `RiscRTE` is the platform/runtime name. `T5S3`, `T5 ePaper S3`, and `EPD47` are hardware/board identifiers only. Existing `T5*` C ABI names may remain temporarily for binary/source compatibility until an intentional ABI migration is performed; they do not define the platform name.

## Architecture

![Architecture](https://raw.githubusercontent.com/michaelrolphone-cmyk/T5S3-Reader/refs/heads/master/docs/riscrte-architecture.svg)


## Current hardware

| Build target | Hardware | Status |
| --- | --- | --- |
| `t5s3-pro` | LilyGO T5 ePaper S3 / T5S3 Pro | Primary target |
| `lilygo-epd47-s3` | LilyGO EPD47 ESP32-S3 | Supported build target; physical acceptance work continues |

The hardware names are intentionally not used as the RiscRTE platform identity.

## Major runtime capabilities

RiscRTE currently includes and/or is being structured around:

- native `.elf` applications loaded from SD and unloaded independently of firmware;
- application manifests and an Apps springboard/App Store model;
- independently packaged ELF hardware drivers;
- pluggable ELF system services and background workers;
- capability-based hardware access so applications do not depend on concrete drivers;
- scene/navigation state separated from ELF residency;
- tiered SRAM/PSRAM/SD-backed memory architecture;
- signed module and production security architecture;
- USB OTG host/device architecture with hubs and pluggable class providers;
- multi-target programmer/debugger architecture for USB serial, SWD/JTAG, and vendor probes;
- e-paper UI, EPUB/TXT/Markdown/XTC reading, image viewing, Wi-Fi transfer, fonts, settings, and power management.

## Architecture specifications

The architecture is defined in `docs/`:

- `PLATFORM_ABSTRACTION_ARCHITECTURE.md` — platform abstraction and capability boundaries.
- `RUNTIME_DRIVER_ARCHITECTURE.md` — installable runtime hardware drivers.
- `RUNTIME_DRIVER_IMPLEMENTATION.md` — driver implementation path.
- `SCENE_RUNTIME_ARCHITECTURE.md` — application bundles, scene graphs, NavigationPath, state, and disposable controller ELFs.
- `MEMORY_ARCHITECTURE.md` — SRAM/PSRAM/storage-backed virtual-object memory model.
- `SECURITY_ARCHITECTURE.md` — hardware trust, signed modules, authorization, and production policy.
- `SERVICE_RUNTIME_ARCHITECTURE.md` — pluggable ELF system services, event delivery, scheduling, persistence, and background work.
- `USB_OTG_HOST_ARCHITECTURE.md` — USB OTG host/device roles, hubs, class providers, storage, HID, CDC, and composite functions.
- `PROGRAMMER_DEBUGGER_ARCHITECTURE.md` — multi-probe firmware programming/debugging over USB hubs, serial, SWD/JTAG, and target-specific protocols.
- `NATIVE_APPS.md` — current native ELF application ABI and implementation.

All architecture documents should use **RiscRTE** when referring to the runtime, framework, platform, core, service manager, driver manager, capability system, or application environment. Hardware-specific references may continue to use the actual board names such as **LilyGO T5S3**.

## Native ELF applications

Installed applications live under `/Apps` as an ELF plus a JSON manifest:

```text
/Apps/hello.elf
/Apps/hello.json
```

A typical manifest contains the minimum compatible firmware/runtime version, display name, ELF filename, and Font Awesome icon. Applications execute through versioned host APIs and are unloaded when their execution lifetime ends.

The long-term model is:

```text
Application ELF
      |
      v
RiscRTE application ABI
      |
      v
capabilities / handles / messages
      |
      v
RiscRTE runtime
      |
      +-- system services
      +-- signed drivers
      +-- storage/network/UI/power
```

Existing API filenames and symbols beginning with `T5` are compatibility identifiers from the current implementation. New architecture and documentation must not use `T5` as the platform name.

## Pluggable drivers and services

Drivers answer **how RiscRTE accesses hardware**. Services answer **what system work RiscRTE performs independently of the foreground application**.

```text
/Drivers/
    gps-nmea/
        driver.elf
        manifest.json

/Services/
    time-sync/
        service.elf
        manifest.json
    alarms/
        service.elf
        manifest.json
    updater/
        service.elf
        manifest.json
```

Production configurations are intended to authenticate native modules before execution and authorize them according to explicit capabilities.

## Memory model

RiscRTE treats memory as tiers rather than pretending SD storage is directly addressable RAM:

```text
Internal SRAM
    hot system state / stacks / DMA-critical buffers
        |
PSRAM
    executable ELFs / active UI / active working set / caches
        |
SD-backed storage
    durable state / inactive state / large virtual objects / resources
```

Storage-backed objects enter PSRAM only through bounded mappings. Ordinary pointers always refer to real addressable RAM.

## USB and programmer direction

The native USB OTG controller is intended to be framework-owned. In host mode RiscRTE can build a topology containing hubs and multiple simultaneously attached devices. Host providers can expose generic capabilities such as removable storage, HID input, serial transports, and programming/debug transports.

The programmer/debugger architecture is explicitly designed for a powered hub containing multiple probes at once—for example an FTDI-based ESP programming path, an ST programming/debug probe, and an MSP programming/debug probe. The initial execution model programs and verifies each target sequentially while all probes remain connected and independently addressable.

## Building firmware

Build the appropriate hardware target with PlatformIO:

```bash
pio run -e t5s3-pro
# or
pio run -e lilygo-epd47-s3
```

Upload:

```bash
pio run -e t5s3-pro -t upload
# or
pio run -e lilygo-epd47-s3 -t upload
```

Serial monitoring:

```bash
pio device monitor -b 115200
```

## SD layout

A representative SD layout is:

```text
/
  Apps/
  Drivers/
  Services/
  Books/
  .fonts/
  .sleep/
```

Legacy implementation-specific storage names may still exist in current firmware and should be migrated deliberately so existing users' state is not destroyed by a branding-only change.

## Project direction

RiscRTE is intended to make the hardware a general embedded computing platform rather than a monolithic firmware image. Applications, drivers, and services should be independently installable; hardware should be reached through capabilities; inactive executable code should be unloadable; background work should be scheduler-owned; and platform policy should remain in the RiscRTE core.

The current ESP32-S3 implementation is the first hardware implementation of that architecture. The name **RiscRTE** refers to the runtime architecture, not to a particular LilyGO board.

## License

See `LICENSE` and `THIRD_PARTY_NOTICES.md` for licensing and third-party notices.
