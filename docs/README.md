# RiscRTE Documentation

For architecture or implementation work, begin with **[RiscRTE Platform Specification](RISCRTE_PLATFORM_SPEC.md)**. It is the master specification and defines document precedence, naming, architectural invariants, and the branch map for all other specifications.

Next read the **[Platform Capability Roadmap](PLATFORM_CAPABILITY_ROADMAP.md)** for the intended platform direction and the roadmap section covering your work. Then follow the child specification links from the master spec.

## Architecture path

```text
RISCRTE_PLATFORM_SPEC.md                 <- START HERE
        |
        +-- PLATFORM_CAPABILITY_ROADMAP.md
        |
        +-- Platform contract
        |     +-- PLATFORM_ABSTRACTION_ARCHITECTURE.md
        |     +-- RUNTIME_DRIVER_ARCHITECTURE.md
        |     +-- RUNTIME_DRIVER_IMPLEMENTATION.md   [current implementation]
        |     +-- MEMORY_ARCHITECTURE.md
        |     +-- SECURITY_ARCHITECTURE.md
        |
        +-- Execution
        |     +-- SCENE_RUNTIME_ARCHITECTURE.md
        |     +-- SERVICE_RUNTIME_ARCHITECTURE.md
        |     +-- NATIVE_APPS.md                      [current ABI; legacy filename]
        |
        +-- Data movement
        |     +-- STREAM_PIPE_ARCHITECTURE.md
        |     +-- STREAM_PIPE_MVP.md                  [implemented subset]
        |
        +-- Devices / transports
              +-- BLUETOOTH_SENSOR_ARCHITECTURE.md    [transport precursor]
              +-- GPS_DRIVER.md                       [current implementation]
              +-- USB_OTG_HOST_ARCHITECTURE.md
              +-- PROGRAMMER_DEBUGGER_ARCHITECTURE.md
```

## Naming and legacy documentation

The platform is **RiscRTE**. Board names such as T5S3 and EPD47, and historical `T5*` / `native_*` API symbols, appear only where they identify hardware or current compatibility implementations. Legacy filenames are retained where renaming would break repository links or obscure the implementation being documented; their document titles and forward-looking terminology should use RiscRTE.

A document may contain both a target design and current legacy implementation. The target design comes first. Current/legacy material remains when it is necessary to understand code that has not migrated yet.

## Non-architecture documentation

User/developer guides such as release procedures, file formats, fonts, localization, troubleshooting, web-server documentation, and board adaptation notes remain in `docs/`. They are operational/supporting documentation and do not override the architecture tree above.

Automated agents are also directed to this hierarchy by the repository-root `AGENTS.md`.