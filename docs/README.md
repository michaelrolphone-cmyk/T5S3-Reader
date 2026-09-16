# RiscRTE Documentation

For architecture or implementation work, begin with **[RiscRTE Platform Specification](RISCRTE_PLATFORM_SPEC.md)**. It is the master specification and defines precedence, naming, invariants and the branch map. Then read the **[Platform Capability Roadmap](PLATFORM_CAPABILITY_ROADMAP.md)** section governing the change and the applicable child specification.

```text
RISCRTE_PLATFORM_SPEC.md                         <- START HERE
 |
 +-- PLATFORM_CAPABILITY_ROADMAP.md              <- platform direction/sequencing
 |
 +-- Platform contract / runtime
 |    +-- PLATFORM_ABSTRACTION_ARCHITECTURE.md
 |    +-- RUNTIME_DRIVER_ARCHITECTURE.md
 |    +-- RUNTIME_DRIVER_IMPLEMENTATION.md       [migration/current state]
 |    +-- MEMORY_ARCHITECTURE.md
 |    +-- SECURITY_ARCHITECTURE.md
 |
 +-- Applications / execution
 |    +-- SCENE_RUNTIME_ARCHITECTURE.md
 |    +-- SERVICE_RUNTIME_ARCHITECTURE.md
 |    +-- NATIVE_APPS.md                          [RiscRTE apps; legacy filename/ABI]
 |    +-- NATIVE_UI_API.md                        [RiscRTE UI host; compatibility ABI]
 |    +-- NATIVE_NETWORK_API.md                   [RiscRTE network host; compatibility ABI]
 |    +-- ADDING_APPS.md                          [legacy compiled Activity path]
 |
 +-- Streams / IPC / data movement
 |    +-- STREAM_PIPE_ARCHITECTURE.md             [authoritative]
 |    +-- STREAM_PIPE_MVP.md                      [implemented subset]
 |
 +-- Devices / sensors / transports
      +-- BLUETOOTH_SENSOR_ARCHITECTURE.md        [BLE precursor to generic sensors]
      +-- GPS_DRIVER.md                           [GNSS/location migration state]
      +-- USB_OTG_HOST_ARCHITECTURE.md
      +-- PROGRAMMER_DEBUGGER_ARCHITECTURE.md
```

## Roadmap branches awaiting/demanding dedicated specs

When implementation begins to make one of these contracts concrete, create or promote a child architecture spec and link it from both the roadmap and master spec: Unified Device/Peripheral Registry; expanded Capability Resolver and resource leases; generic Sensor Framework; I2C/SPI/GPIO managers; Location/Observation/Fusion; structured IPC/Event Bus; unified Storage/Volumes and Recorder; content intents/share/search; reusable Networking/Communications/Discovery; Notifications/Alarms/Automation; credential vault; package/dependency lifecycle; power/reliability/diagnostics.

Do not create an app-private substitute for one of these roadmap facilities simply because its dedicated child spec has not been written yet. The roadmap requirements already govern the direction.

## Naming and legacy documentation

The platform is **RiscRTE**. T5S3/EPD47 are board identifiers. Historical `T5*`, `t5_*`, `native_*`, concrete class names, package names and source paths remain only where needed to identify deployed/current code or preserve ABI/source references. Legacy filenames may remain to avoid breaking links; document titles and forward-looking concepts use RiscRTE terminology.

When a document must describe an older implementation, structure it as target contract first, current implementation second, legacy compatibility where needed, then migration requirements. Accurate legacy state should not be deleted, but it does not override the target architecture.

## Supporting/operational documentation

`RELEASING.md`, board adaptation notes, activity-manager implementation notes, file/font/i18n/format guides, web-server docs and troubleshooting are supporting documentation. They may describe current code but do not override the architecture hierarchy above.

Repository-root `AGENTS.md` directs automated agents to this hierarchy. Architecture/API changes must update the applicable authoritative specification in the same change.