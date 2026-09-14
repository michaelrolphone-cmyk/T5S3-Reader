# Runtime-Loaded Driver and Capability Architecture

## Status

**Design specification.** This document defines the target architecture for evolving T5S3-Reader from a board-specific firmware with loadable applications into a portable runtime that loads both applications and hardware/service providers as ELF modules.

The design is intentionally capability-oriented. Applications depend on abstract capabilities, drivers provide capabilities, and drivers may themselves depend on capabilities supplied by other drivers. The runtime resolves this dependency graph, binds logical roles such as `display.primary` and `network.default`, mounts storage providers into a common root filesystem, and prevents applications from launching when mandatory requirements cannot be satisfied.

The runtime must also remain recoverable when no modular drivers are installed or when the installed driver set is broken. On ESP32 targets, a minimal compiled-in recovery substrate therefore remains available for serial administration, standard ESP32 Wi-Fi networking, package download, and driver installation.

---

## 1. Goals

The architecture SHALL support the following goals:

1. Applications SHALL be portable across supported devices without embedding board-specific hardware knowledge.
2. Hardware implementations SHALL be replaceable at runtime through driver ELF modules.
3. The presence of a driver file SHALL NOT imply that the driver is active, selected, or even compatible with currently attached hardware.
4. Applications SHALL declare mandatory and optional capabilities in their manifests.
5. The runtime SHALL refuse to launch an application when any mandatory capability cannot be resolved to a usable provider.
6. Drivers SHALL be able to depend on abstract capabilities in the same way applications do.
7. Composite or fusion drivers SHALL be able to consume multiple lower-level capabilities and provide a higher-level capability.
8. Storage SHALL be exposed through a filesystem abstraction rather than through SD-card-specific APIs.
9. The system SHALL expose a root filesystem with mounted filesystems, including local removable media, internal flash, USB storage, and network filesystems.
10. The runtime SHALL provide a recovery path when no normal hardware drivers are installed.
11. On ESP32 builds, recovery SHALL include serial commands and baseline ESP32 Wi-Fi sufficient to configure networking and download/install drivers.
12. Capability APIs SHALL be independently versioned.
13. Driver installation, hardware detection, logical binding, and runtime activation SHALL be separate concepts.
14. Shared hardware resources SHALL be arbitrated by the runtime rather than accessed blindly by modules.
15. The compiled firmware SHALL progressively shrink toward a portable execution kernel, recovery substrate, capability manager, VFS, package manager, and ELF loader.

---

## 2. Non-goals

This specification does not require:

- Full Linux Device Tree compatibility.
- POSIX process isolation between native ELF modules.
- Moving the ESP-IDF TCP/IP stack out of the runtime.
- Making every chip on a board a separate ELF module.
- Making the normal graphical display mandatory for recovery.
- Treating installed drivers as automatically active.

---

## 3. Architectural model

The target runtime is layered as follows:

```text
Application ELFs
    |
    | require abstract capabilities
    v
Capability APIs / Logical Bindings
    |
    v
Capability Registry + Dependency Resolver
    |
    v
Driver / Provider ELFs
    |
    | may require other capabilities
    | may use privileged kernel primitives
    v
Runtime Kernel APIs
    |
    v
MCU / Hardware
```

Storage adds a parallel filesystem stack:

```text
Physical storage or remote transport
    v
Storage / network driver
    v
Filesystem provider
    v
Mount manager
    v
Root VFS namespace
    v
Applications
```

A composite provider is modeled identically to a hardware provider:

```text
GNSS -------------------+
Accelerometer ----------+--> absolute-position-fusion.elf --> position.absolute
Gyroscope --------------+
```

The critical invariant is:

> **Applications depend on capabilities. Drivers provide capabilities. Drivers may also consume capabilities. The runtime resolves the graph.**

---

## 4. Module classes

### 4.1 Application ELF

Application ELFs implement user-facing features and consume capability APIs.

Examples:

- `reader.elf`
- `file_browser.elf`
- `image_viewer.elf`
- `serial_monitor.elf`
- `gps_viewer.elf`
- `settings.elf`

Applications SHALL NOT directly depend on board names, GPIO numbers, ESP-IDF device drivers, chip-specific display libraries, charger ICs, or other physical implementation details.

### 4.2 Driver/provider ELF

Driver ELFs provide one or more capabilities. A provider may represent physical hardware, a virtual service, an aggregation layer, or a fusion layer.

Examples:

- `epdiy_display.elf`
- `st7789_display.elf`
- `sx1262_radio.elf`
- `ublox_gnss.elf`
- `sdmmc_storage.elf`
- `webdav_fs.elf`
- `absolute_position_fusion.elf`

A driver may both consume and provide capabilities.

### 4.3 Kernel/runtime component

The compiled runtime provides the minimum substrate required to load and supervise modules. It owns privileged resources and exposes stable primitive APIs to driver modules.

---

## 5. Capability model

Capabilities are stable, implementation-independent contracts.

Example capability families:

```text
display
ui
input.pointer
input.keyboard
storage.block
storage.filesystem
filesystem.mountable
network.interface
network.socket
serial.stream
position.gnss
position.absolute
orientation.absolute
motion.accelerometer
motion.gyroscope
radio.lora
audio.output
battery.status
power.control
rtc
```

Applications SHALL request abstract capability names rather than driver identities.

Correct:

```text
requires display.primary
requires position.absolute
requires network.default
```

Incorrect:

```text
requires EPDiy
requires u-blox
requires ESP32 Wi-Fi driver
```

### 5.1 Capability API versions

Each capability family SHALL have an independently versioned API.

Example:

```text
driver ABI             1
display API            2
filesystem API         1
network.interface API  2
position.gnss API      1
position.absolute API  1
```

A firmware version may remain in manifests for coarse compatibility, but capability/API compatibility SHOULD become authoritative.

---

## 6. Application manifests

Every application manifest SHALL enumerate mandatory capabilities and MAY enumerate optional capabilities.

Example:

```json
{
  "type": "app",
  "id": "maps",
  "version": "1.4.0",
  "runtime_abi": 2,
  "display_name": "Maps",
  "file_name": "maps.elf",
  "requires": [
    { "capability": "display.primary", "api": ">=2" },
    { "capability": "input.primary", "api": ">=1" },
    { "capability": "position.absolute", "api": ">=1" },
    { "capability": "storage.filesystem", "api": ">=1" }
  ],
  "optional": [
    { "capability": "network.default", "api": ">=1" }
  ]
}
```

### 6.1 Launch gating

Before calling any application entry point, the application manager SHALL:

1. Read and validate the manifest.
2. Validate the runtime ABI.
3. Resolve every mandatory capability.
4. Verify API-version compatibility.
5. Verify that each provider is present and usable or can be activated.
6. Acquire required capability handles.
7. Only then load/start the application.

If any mandatory capability cannot be resolved, the application SHALL NOT be started.

Example user-facing state:

```text
Maps cannot run on this device.

Missing capability:
  position.absolute >= 1
```

The Apps launcher SHOULD be able to evaluate compatibility before the user launches the application so incompatible apps can be shown as unavailable rather than crashing after selection.

### 6.2 Optional capabilities

Optional capabilities SHALL NOT block launch. Applications MAY alter behavior when an optional capability is available.

Future manifest versions MAY support dependency groups such as `any_of`, allowing an app to accept one of several alternative capabilities.

---

## 7. Driver manifests

Drivers SHALL describe both what they require and what they provide.

Example physical GNSS driver:

```json
{
  "type": "driver",
  "id": "ublox-gnss",
  "version": "1.0.0",
  "driver_abi": 1,
  "requires": [
    { "capability": "kernel.uart", "api": ">=1" }
  ],
  "provides": [
    { "capability": "position.gnss", "api": 1 }
  ]
}
```

Example composite driver:

```json
{
  "type": "driver",
  "id": "absolute-position-fusion",
  "version": "1.0.0",
  "driver_abi": 1,
  "requires": [
    { "capability": "position.gnss", "api": ">=1" },
    { "capability": "motion.accelerometer", "api": ">=1" },
    { "capability": "motion.gyroscope", "api": ">=1" }
  ],
  "provides": [
    { "capability": "position.absolute", "api": 1 },
    { "capability": "orientation.absolute", "api": 1 }
  ]
}
```

Drivers therefore participate in the same capability dependency model as applications.

---

## 8. Driver ABI

All driver ELFs SHOULD expose one stable root symbol rather than a large set of unrelated exported functions.

Conceptual interface:

```c
typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;

    const char *driver_id;
    const char *driver_name;

    bool (*probe)(const t5_device_context_t *context);
    bool (*start)(void);
    void (*stop)(void);
} t5_driver_v1;

const t5_driver_v1 *t5_driver_get(uint32_t requested_abi);
```

The actual implementation may evolve, but the ABI SHALL provide a stable mechanism to:

- identify the module,
- negotiate ABI version,
- probe candidate hardware,
- start and stop the provider,
- register provided capabilities,
- unregister capabilities during shutdown or failure.

---

## 9. Installed drivers are not active drivers

The runtime SHALL distinguish at least the following states:

```text
INSTALLED
LOADED
PROBED
BOUND
ACTIVE
SUSPENDED
FAILED
ABSENT
```

Definitions:

- **INSTALLED**: driver package exists in persistent storage.
- **LOADED**: ELF has been loaded and validated.
- **PROBED**: driver has examined a candidate device/configuration.
- **BOUND**: driver has been associated with a concrete hardware/device instance.
- **ACTIVE**: provider is currently started and serving one or more consumers.
- **SUSPENDED**: bound provider is intentionally inactive.
- **FAILED**: load, probe, activation, or runtime operation failed.
- **ABSENT**: compatible hardware was not detected.

Multiple display drivers may therefore be installed simultaneously without any implication that all are active.

Example:

```text
epdiy_display.elf    INSTALLED -> BOUND -> ACTIVE
st7789_display.elf   INSTALLED -> BOUND -> SUSPENDED
ili9488_display.elf  INSTALLED -> ABSENT
```

---

## 10. Concrete devices and logical bindings

The runtime SHALL distinguish concrete device instances from logical roles.

Concrete instances:

```text
display.epd0
display.lcd0
network.wifi0
network.eth0
storage.sd0
storage.flash0
```

Logical aliases:

```text
display.primary
display.secondary
input.primary
network.default
storage.system
storage.removable
```

Applications SHOULD normally consume logical aliases.

Example binding:

```text
display.primary -> display.epd0
```

After a hardware change:

```text
display.primary -> display.lcd0
```

Applications remain unchanged.

Binding selection priority SHOULD initially be:

1. explicit saved user/device binding,
2. device profile recommendation,
3. provider priority.

Future policy engines may additionally account for power, quality, latency, accuracy, or other metadata.

---

## 11. Device description

The runtime SHOULD use a lightweight device-description format to describe buses, pins, addresses, interrupts, rails, aliases, and compatible devices.

Example:

```json
{
  "board": "t5s3-pro",
  "devices": [
    {
      "id": "display0",
      "compatible": ["gooddisplay,h752"],
      "bus": "spi2",
      "cs": 10,
      "busy": 12,
      "reset": 13
    },
    {
      "id": "charger0",
      "compatible": ["ti,bq25896"],
      "bus": "i2c0",
      "address": 107
    }
  ]
}
```

This is intentionally smaller than Linux Device Tree. The format only needs enough information to describe hardware resources and allow driver matching.

---

## 12. Dependency graph and resolver

The runtime SHALL maintain a directed dependency graph.

Example:

```text
kernel.uart --> gps.driver ----------------------+ 
                                                 |
kernel.i2c --> accelerometer.driver -------------+--> position-fusion.driver
                                                 |         |
kernel.spi --> gyroscope.driver -----------------+         +--> position.absolute
```

A network filesystem may resolve as:

```text
network.default
    -> webdav.driver
    -> filesystem.mountable
    -> mount manager
    -> /mnt/cloud
```

### 12.1 Resolution process

The resolver SHOULD:

1. Read installed manifests.
2. Build the dependency graph.
3. Validate capability/version constraints.
4. Detect cycles.
5. Identify providers satisfiable directly by kernel capabilities.
6. Load and probe those providers.
7. Register successfully provided capabilities.
8. Re-evaluate drivers whose dependencies have now become satisfiable.
9. Repeat until no additional providers can be resolved.
10. Build logical bindings from the resulting provider set.

The resolver SHALL reject dependency cycles and report the dependency path rather than attempting to load them recursively forever.

---

## 13. Composite and virtual providers

Not every provider corresponds directly to one physical chip.

Valid providers include:

- GNSS + IMU sensor fusion,
- network aggregation,
- filesystem overlays,
- WebDAV/SMB/NFS filesystems,
- virtual keyboards,
- audio mixers,
- image decoders,
- sensor filtering,
- screen transforms.

A driver should therefore be understood as a **capability provider module**, with hardware drivers being one provider class.

Example absolute-position stack:

```text
ublox_gnss.elf          -> position.gnss
bmi270.elf              -> motion.accelerometer
icm42688.elf            -> motion.gyroscope

absolute_position_fusion.elf
    requires position.gnss
    requires motion.accelerometer
    requires motion.gyroscope
    provides position.absolute
    provides orientation.absolute
```

The fusion driver SHALL consume abstract APIs rather than concrete driver identities.

---

## 14. Kernel primitive APIs

Driver ELFs require privileged low-level services that normal applications SHOULD NOT receive directly.

The runtime kernel SHOULD expose stable abstractions for:

```text
GPIO
SPI
I2C
UART
interrupts
timers
DMA
tasks
mutexes
queues/events
heap/PSRAM
cache/MMU support
logging
watchdog
resource ownership
```

Driver code SHOULD use runtime abstractions instead of directly calling ESP-IDF wherever practical.

Example portability boundary:

```text
Driver ELF
    -> T5KernelSpiApi
        -> ESP-IDF SPI backend on ESP32
```

A future platform may implement the same `T5KernelSpiApi` over another MCU HAL.

---

## 15. Privilege separation

The SDK SHOULD be divided into at least three layers:

```text
sdk/common/
    capability IDs
    error codes
    ABI/version definitions

sdk/app/
    UI
    filesystem
    network
    position
    high-level system services

sdk/driver/
    driver ABI
    kernel GPIO/SPI/I2C/UART
    IRQ/DMA
    resource manager
```

Application ELFs SHOULD NOT be able to accidentally depend on driver-only kernel headers.

Driver/provider ELFs are inherently more privileged because they may access low-level resources. Driver packages SHOULD therefore eventually use stronger signing/trust requirements than ordinary application packages.

---

## 16. Capability registry

The runtime SHALL maintain a registry containing all currently usable capability providers.

Conceptual entry:

```c
typedef struct {
    const char *capability_id;
    uint32_t api_version;
    const void *api;
    const char *provider_id;
    const char *device_instance;
    uint32_t state;
} t5_capability_entry_t;
```

Driver installation alone SHALL NOT create a usable capability entry. Registration occurs only when the driver is successfully resolved and bound to an available provider/device instance.

The registry SHOULD support provider metadata such as:

- quality/accuracy,
- latency,
- update rate,
- power cost,
- removable/persistent state,
- display geometry/pixel format,
- network link type,
- storage capacity and writability.

---

## 17. Capability handles and lifecycle

Applications and composite drivers SHOULD acquire capability handles instead of retaining untracked global API pointers.

Conceptual usage:

```c
t5_capability_handle_t h =
    t5_capability_acquire("position.absolute", 1);

/* use capability */

t5_capability_release(h);
```

The runtime can then reference-count provider use.

Example:

```text
Maps acquires position.absolute
    -> fusion provider activates
        -> acquires GNSS
        -> acquires accelerometer
        -> acquires gyroscope

Maps releases position.absolute
    -> fusion refcount reaches zero
        -> lower-level capabilities released
        -> hardware may suspend/power down
```

This mechanism SHOULD form the basis of runtime power management.

---

## 18. Resource manager

Hardware ownership SHALL remain with the runtime.

Drivers SHALL claim resources through a resource manager rather than blindly configuring shared buses and pins.

Resources include:

```text
SPI buses
I2C buses
UARTs
GPIOs
interrupts
DMA channels
power rails
clock sources
USB controller
memory pools
```

The resource model SHALL support both exclusive resources and shareable resources.

Examples:

- GPIO output: normally exclusive.
- UART endpoint: normally exclusive.
- DMA channel: normally exclusive.
- I2C bus: shareable with serialized transactions.
- SPI bus: shareable with per-device ownership and arbitration.
- Power rail: shared/refcounted.

This is necessary for runtime-loadable modules to coexist safely.

---

## 19. Storage abstraction and root filesystem

Application-facing storage APIs SHALL NOT expose SD-card-specific assumptions.

Applications shall see a filesystem namespace rather than physical media.

Example root layout:

```text
/
|-- system/
|-- apps/
|-- drivers/
|-- config/
|-- home/
|-- tmp/
`-- mnt/
    |-- sd/
    |-- flash/
    |-- usb/
    `-- network/
```

The backing store for `/` is platform-specific and may be internal flash, eMMC, an overlay, or another persistent filesystem.

### 19.1 Storage layers

Storage SHALL distinguish:

```text
physical/block storage
    -> filesystem implementation
        -> mount
            -> root VFS namespace
```

Examples:

```text
SDMMC driver -> block.storage -> FAT -> /mnt/sd
SPI flash    -> block.storage -> LittleFS -> /system
USB MSC      -> block.storage -> filesystem -> /mnt/usb
network      -> WebDAV/SMB/NFS provider -> /mnt/network
```

The file browser, reader, image viewer, and other applications SHALL operate on the common filesystem API and SHALL NOT require SD-specific code.

### 19.2 Filesystem API

The application-facing API SHOULD provide operations equivalent to:

```text
open
read
write
seek
close
stat
mkdir
remove
rename
opendir
readdir
closedir
```

Additional features such as file watching, advisory locking, extended metadata, and asynchronous operations may be added as separately versioned capabilities.

### 19.3 Mount manager

The runtime SHALL own mount lifecycle, including:

- filesystem recognition,
- mount/unmount,
- mount-point assignment,
- hot-plug handling,
- writable/read-only state,
- capacity/free-space metadata,
- remote mount connectivity.

Removing an SD card or losing a network mount must therefore become a mount/capability state change rather than undefined app behavior.

---

## 20. Network abstraction

Networking SHALL be layered so applications do not depend directly on Wi-Fi.

```text
hardware network provider
    -> network.interface
        -> runtime IP services
            -> network.socket / HTTP / TLS
                -> applications
```

A Wi-Fi-specific provider may additionally expose optional Wi-Fi control capabilities such as scanning, RSSI, channel, and security metadata, while generic applications use `network.default` or socket/HTTP APIs.

The runtime may retain lwIP and common protocol services internally while the physical network-interface implementation moves into provider modules.

---

## 21. Bootstrap and recovery substrate

A completely modular system has a bootstrap problem: drivers cannot be downloaded if no network or storage drivers are available.

The runtime therefore SHALL retain a minimal platform recovery substrate compiled into firmware.

For ESP32 targets the guaranteed baseline SHALL include:

- serial recovery console,
- standard ESP32 Wi-Fi support sufficient for provisioning and Internet access,
- TCP/IP/DNS/DHCP and HTTPS required by package download,
- minimal persistent internal-flash storage,
- package download/verification/install functionality,
- reboot and recovery commands.

This fallback is not the normal device-driver architecture. It exists to make an empty or broken device recoverable.

### 21.1 Serial recovery console

The system SHALL be recoverable without a display driver.

Representative commands may include:

```text
help
status

drivers list
drivers probe
drivers install <package-or-url>
drivers remove <id>

devices list
capabilities list
bindings list

wifi scan
wifi connect <ssid>
wifi status

mounts
ls
cat
cp

download <url> <path>

reboot
recovery
```

Exact syntax is implementation-defined, but the baseline MUST permit configuring network access and installing a driver set without requiring graphical hardware.

### 21.2 ESP32 fallback Wi-Fi

On ESP32 builds, baseline ESP-IDF Wi-Fi SHALL remain available to recovery code even if the normal modular networking provider is absent or broken.

The fallback may optionally register as a very-low-priority `network.default` provider when no normal provider exists, but application code SHALL NOT depend on the fact that it is ESP32 Wi-Fi.

### 21.3 Driverless first boot

A device with no modular driver packages installed SHALL still reach a state equivalent to:

```text
runtime booted
serial recovery active
internal flash available
fallback ESP32 Wi-Fi available
package manager available
```

From serial, the operator must be able to configure Wi-Fi, reach a package source, download a device profile/driver set, validate it, install it, and reboot into normal operation.

---

## 22. Board/device profiles

Board profiles SHALL be configuration conveniences rather than hard-coded product firmware identities.

Example:

```json
{
  "id": "lilygo-t5s3-pro",
  "packages": [
    "epdiy-h752",
    "gt911",
    "bq25896",
    "sdmmc"
  ],
  "bindings": {
    "display.primary": "display.epd0",
    "input.primary": "touch.0",
    "storage.removable": "storage.sd0"
  }
}
```

Installing a device profile means installing and configuring a recommended set of providers. It SHOULD NOT require changing application code or rebuilding the runtime.

---

## 23. Package model

Driver/provider packages SHOULD contain at least:

```text
manifest.json
driver.elf
integrity/signature metadata
```

They MAY also contain:

- configuration schema,
- default bindings,
- device aliases,
- documentation,
- license metadata.

The package manager SHOULD eventually support:

```text
search
download
verify
install
upgrade
rollback
remove
```

Because provider ELFs may receive privileged kernel APIs, trusted signatures SHOULD become mandatory before privileged driver execution. Hash and manifest validation SHOULD be implemented from the beginning even before a full trust store exists.

---

## 24. Runtime capability loss

Manifest validation protects launch-time compatibility, but hardware can disappear or providers can fail after launch.

The runtime SHALL therefore publish capability lifecycle events.

Examples:

```text
SD removed -> mount disappears -> filesystem/storage event
GPS provider fails -> position.absolute becomes unavailable
Wi-Fi disconnects -> network state changes
USB device removed -> serial.stream disappears
```

Applications SHOULD receive structured capability-loss notifications rather than encountering invalid pointers.

A future manifest revision MAY classify required capabilities as:

- `mandatory`: loss requires app termination or suspension,
- `degradable`: app may continue with reduced functionality.

---

## 25. Display architecture

Display implementations are expected to become runtime providers only after the graphics/UI layers are sufficiently hardware-independent.

The preferred stack is:

```text
Application
    -> UI API
        -> graphics/rendering API
            -> display capability
                -> selected display provider ELF
```

The generic display API SHOULD expose hardware differences through capability metadata instead of pretending all displays behave identically.

Relevant properties include:

- width/height,
- pixel format,
- monochrome/grayscale/color,
- full vs partial refresh,
- update alignment constraints,
- asynchronous commit support,
- orientation,
- touch association,
- e-paper-specific refresh characteristics where necessary.

Multiple display providers may remain installed simultaneously. `display.primary` selects the active logical display for ordinary applications.

Recovery MUST NOT require the normal display provider.

---

## 26. Boot stages

The target boot process SHOULD be explicitly staged:

```text
Stage 0  ROM / MCU boot
Stage 1  Minimal compiled runtime
         memory, logging, serial recovery, bootstrap storage,
         fallback ESP32 Wi-Fi

Stage 2  Mount system root filesystem
Stage 3  Load device/runtime configuration
Stage 4  Discover installed provider manifests
Stage 5  Resolve and instantiate fundamental providers
Stage 6  Resolve composite/virtual providers
Stage 7  Construct logical bindings
Stage 8  Initialize high-level system services and VFS mounts
Stage 9  Evaluate and launch shell/home application
```

If later stages fail, the system SHALL be capable of returning to Stage-1 recovery without relying on normal application or display drivers.

---

## 27. Recommended migration plan

The architecture SHOULD be introduced incrementally rather than through a single rewrite.

### Phase A - Capability contracts

Implement:

- capability IDs,
- API version negotiation,
- capability registry,
- logical aliases,
- capability handles,
- application-manifest requirements.

Existing compiled-in hardware implementations register themselves as providers. Behavior remains functionally unchanged.

### Phase B - VFS and storage abstraction

Implement:

- generic filesystem API,
- mount manager,
- root filesystem,
- mount namespace,
- storage-provider abstraction.

Convert application-facing SD access to VFS paths and filesystem APIs.

### Phase C - Driver ABI and dependency resolver

Implement:

- `T5DriverApi`,
- provider manifests,
- `requires`/`provides`,
- provider priority,
- cycle detection,
- iterative dependency resolution,
- driver lifecycle states.

### Phase D - First dynamic physical providers

Move lower-risk, non-boot-critical hardware first, such as:

- GNSS,
- accelerometer,
- gyroscope,
- LoRa.

### Phase E - Composite provider proof

Implement a real capability-consuming provider such as:

```text
absolute_position_fusion.elf
```

It should depend on GNSS, accelerometer, and gyroscope capability APIs and provide `position.absolute`.

This phase proves that the dependency model works for higher-order virtual hardware.

### Phase F - Dynamic storage providers

Move removable/local storage implementations behind the storage/VFS model, including SDMMC and later USB mass storage.

### Phase G - Modular normal networking

Introduce modular network-interface providers while retaining compiled-in ESP32 Wi-Fi exclusively as a recovery/bootstrap guarantee.

Applications use `network.default`, sockets, or higher-level protocol APIs.

### Phase H - Display and input providers

After the capability model and UI/graphics abstraction are mature, move normal display and touch/input implementations into provider ELFs.

Serial recovery remains the hard recovery guarantee.

### Phase I - Runtime cleanup

Remove remaining product-specific hardware implementations from normal firmware where equivalent provider interfaces exist.

The compiled runtime should then primarily contain:

```text
boot
ELF loader
memory/runtime support
scheduler primitives
kernel APIs
resource manager
capability registry/resolver
VFS/mount manager
package manager
security/trust validation
serial recovery
fallback bootstrap networking
```

---

## 28. Proposed source-tree direction

A possible long-term repository structure is:

```text
runtime/
    boot/
    elf/
    kernel/
    resources/
    registry/
    filesystem/
    package_manager/
    recovery/

sdk/
    common/
    app/
    driver/

providers/
    platform/
        esp32s3/
    display/
    input/
    storage/
    network/
    radio/
    gnss/
    motion/
    power/
    composite/

devices/
    t5s3-pro.json
    lilygo-epd47.json

Apps/
    reader/
    file_browser/
    image_viewer/
    serial_monitor/
    settings/
```

The exact names may change; the important separation is between runtime, SDK contracts, providers, device configuration, and applications.

---

## 29. Required architectural invariants

The following rules should be treated as design constraints during future refactors:

1. **No required capability, no app launch.**
2. **Installed does not mean active.**
3. **Apps depend on capabilities, never concrete drivers.**
4. **Drivers may depend on capabilities and may provide higher-level capabilities.**
5. **Only privileged provider modules consume low-level kernel hardware APIs.**
6. **Physical storage is not the application filesystem API.**
7. **SD is one storage provider, not the definition of storage.**
8. **All mounted storage participates in one root filesystem namespace.**
9. **Network filesystems are ordinary mounts once their transport dependencies resolve.**
10. **Logical aliases such as `display.primary` and `network.default` are bindings, not drivers.**
11. **Driver presence never selects hardware by itself.**
12. **Recovery must work without normal display, input, storage, or network drivers.**
13. **ESP32 builds retain serial plus baseline ESP32 Wi-Fi as the recovery bootstrap path.**
14. **Shared buses, pins, interrupts, DMA, power rails, and controllers are runtime-managed resources.**
15. **Every API is independently versioned.**
16. **Capability loss after launch is an explicit runtime event, not undefined behavior.**
17. **The same dependency resolver model applies to apps, physical drivers, virtual drivers, fusion drivers, and network/storage providers.**

---

## 30. End-state portability model

A new hardware platform should eventually require only:

1. a port of the minimal kernel primitive APIs to the target MCU/platform,
2. a platform-appropriate bootstrap/recovery path,
3. a set of provider ELF modules for available hardware,
4. a device/profile description and default bindings.

Applications remain written against stable capability APIs.

Where CPU architecture and ELF ABI are compatible, the same application binary may run unchanged. Where the instruction set differs, the same application source and SDK contract should rebuild without device-specific changes.

The long-term identity of a supported device therefore becomes:

```text
minimal runtime
+
device description
+
installed provider set
+
logical bindings
```

rather than a monolithic board-specific firmware image.

---

## 31. Summary

T5S3-Reader should evolve from a firmware that knows about specific hardware into a runtime that knows about **capabilities, dependencies, resources, filesystems, providers, and bindings**.

Applications declare what they need. Providers declare what they need and what they supply. Physical drivers, virtual services, storage/filesystem providers, and composite sensor-fusion drivers all participate in the same dependency graph. The runtime resolves that graph, selects providers independently of mere driver installation, and refuses to launch applications whose mandatory capabilities cannot be satisfied.

At the storage layer, SD-specific access is replaced by a root VFS with mounted local and remote filesystems. At the recovery layer, ESP32 serial and baseline Wi-Fi remain compiled in so a completely driverless or broken installation can still connect to a network and download a working driver set.

This architecture makes changing displays, replacing storage media, introducing network drives, swapping GNSS hardware, adding IMU-assisted position fusion, or moving to another controller a provider/configuration problem rather than an application rewrite.