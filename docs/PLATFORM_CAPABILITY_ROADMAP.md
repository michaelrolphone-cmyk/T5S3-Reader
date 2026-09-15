# RiscRTE Platform Capability Roadmap

## Status

Architecture and implementation roadmap for expanding RiscRTE from a collection of runtime facilities into a general embedded runtime platform whose applications, services, drivers, protocols, devices, and data sources can be added independently of core firmware.

This document defines the major platform capabilities that should be built above the existing ELF application/driver/service architecture, capability resolver, scene runtime, memory architecture, security model, USB subsystem, networking, BLE sensor subsystem, and hardware abstractions.

> **Core invariant:** New platform functionality should normally appear as a reusable capability, provider, service, stream, device, job, or package—not as private infrastructure embedded inside one application.

---

## 1. Objective

RiscRTE should evolve toward this model:

```text
                         Applications
                              |
                 intents / jobs / capabilities
                              |
          +-------------------+-------------------+
          |                   |                   |
          v                   v                   v
      Devices              Services             Data
          |                   |                   |
          +-------------+-----+-------------------+
                        |
                  Streams / Events
                        |
                 Capability Resolver
                        |
          +-------------+-------------+
          |             |             |
        Drivers      Providers      Runtime
          |             |             |
          +-------------+-------------+
                        |
                     Hardware
```

Applications should increasingly describe **what they require** rather than **which hardware implementation they expect**.

---

## 2. Existing foundations

This roadmap assumes and builds on the architecture defined elsewhere in the repository, including:

```text
RUNTIME_DRIVER_ARCHITECTURE.md
RUNTIME_DRIVER_IMPLEMENTATION.md
SCENE_RUNTIME_ARCHITECTURE.md
SERVICE_RUNTIME_ARCHITECTURE.md
MEMORY_ARCHITECTURE.md
SECURITY_ARCHITECTURE.md
USB_OTG_HOST_ARCHITECTURE.md
PROGRAMMER_DEBUGGER_ARCHITECTURE.md
BLUETOOTH_SENSOR_ARCHITECTURE.md
PLATFORM_ABSTRACTION_ARCHITECTURE.md
```

The roadmap does not replace those specifications. It defines the higher-level platform capabilities that should connect them.

---

# Part I — Core Platform Unification

## 3. Unified Device and Peripheral Registry

RiscRTE SHOULD provide one system-owned registry for physical and logical devices regardless of transport.

Potential transports include:

```text
USB
Bluetooth LE
UART
I2C
SPI
GPIO
LoRa
Wi-Fi / IP
internal SoC peripherals
future transports
```

A device is not synonymous with a transport endpoint. One physical device may expose several semantic capabilities.

Example:

```text
Environmental Sensor A
  transport: BLE
  capabilities:
    sensor.temperature
    sensor.humidity
    sensor.battery

USB Debug Probe
  transport: USB
  capabilities:
    debug.swd
    program.stm32

GNSS Receiver
  transport: UART
  capabilities:
    location.position
    location.altitude
    location.time
```

### 3.1 Device record

A logical device record SHOULD support:

```text
runtime device ID
transport identity
stable identity evidence
friendly name
provider/driver binding
connection/presence state
capabilities
resource ownership
security/trust metadata
last seen
transport-specific metadata reference
```

Transport-specific implementation details SHOULD remain below the generic registry API.

### 3.2 Device lifecycle

Suggested states:

```text
DISCOVERED
IDENTIFIED
BOUND
AVAILABLE
BUSY
SUSPENDED
UNAVAILABLE
REMOVED
FAILED
```

### 3.3 Device events

```text
device.discovered
device.available
device.unavailable
device.capability_added
device.capability_removed
device.identity_changed
device.error
```

---

## 4. Capability Resolver Expansion

The capability resolver should become the primary indirection mechanism between applications/services and implementations.

Examples:

```text
location.position
sensor.temperature
serial.port
serial.host
storage.removable
input.keyboard
network.internet
debug.swd
program.target
notification.post
crypto.hash
credential.read
```

A capability request SHOULD be able to specify constraints without naming a driver ELF.

Conceptually:

```text
request:
  capability: sensor.temperature
  device: optional
  minimum_rate: optional
  preferred_source: optional
  exclusivity: shared
```

The resolver returns an opaque lease/handle whose implementation may come from hardware, a driver, a service, or another provider.

---

## 5. Unified Resource Ownership

All major platform capabilities SHOULD participate in the same ownership model.

Resources include:

```text
devices
capability leases
streams
files
volumes
network connections
BLE connections
USB interfaces
timers
jobs
power locks
UI surfaces
memory mappings
subscriptions
credentials
```

Every resource SHOULD have an owning execution context and deterministic reclamation behavior.

This is a prerequisite for reliable ELF unloading and future isolation.

---

# Part II — Hardware and Sensor Expansion

## 6. Generic Sensor Framework

The semantic sensor layer introduced by `BLUETOOTH_SENSOR_ARCHITECTURE.md` SHOULD become transport-independent.

Potential sources:

```text
BLE
USB
I2C
SPI
UART
LoRa
Wi-Fi
internal ADC/SoC sensors
GNSS-derived observations
```

All sources SHOULD be capable of publishing a common versioned measurement representation.

### 6.1 Common sensor capabilities

Examples:

```text
sensor.temperature
sensor.humidity
sensor.pressure
sensor.acceleration
sensor.angular_velocity
sensor.magnetic_field
sensor.light
sensor.distance
sensor.voltage
sensor.current
sensor.power
sensor.battery
sensor.air_quality
sensor.position
```

### 6.2 Sensor services

The framework SHOULD eventually support:

```text
calibration
unit conversion
sampling policy
channel metadata
latest-value cache
historical recording
quality flags
time synchronization
aggregation/downsampling
threshold events
```

Applications SHOULD not need transport-specific code to consume measurements.

---

## 7. I2C, SPI, and GPIO Provider Framework

Shared buses SHALL be runtime-owned resources rather than independently initialized by arbitrary drivers.

Architecture:

```text
Application
    |
semantic capability
    |
Device/Capability Resolver
    |
signed device driver ELF
    |
bounded bus API
    |
RiscRTE Bus Manager
    |
I2C / SPI / GPIO hardware
```

### 7.1 I2C manager

Should provide:

```text
bus enumeration/configuration
address ownership
bounded transactions
timeouts
shared-bus arbitration
recovery from stuck bus where hardware permits
power-domain integration
```

### 7.2 SPI manager

Should provide:

```text
bus ownership
chip-select identities
transaction settings
DMA-safe buffer allocation
shared-device arbitration
bounded transfer sizes
```

### 7.3 GPIO manager

Should provide:

```text
pin reservation
input/output modes
interrupt subscription
pull configuration
safe release state
ownership conflict detection
```

Drivers SHOULD receive only the pins/buses declared by their authorized hardware profile.

---

## 8. Location Framework

GNSS SHOULD be generalized into semantic location capabilities.

Examples:

```text
location.position
location.altitude
location.velocity
location.heading
location.time
location.accuracy
location.satellites
```

A location consumer SHOULD not need to know whether position came from:

```text
internal GNSS
UART GNSS
USB GNSS
BLE GNSS
future fused source
```

The framework SHOULD support source metadata and quality/accuracy indicators.

---

## 9. Observation and Sensor Fusion

Once multiple timestamped sensor/location sources exist, RiscRTE SHOULD provide a common observation timeline and optional fusion providers.

The initial goal is not a universal estimator. It is reliable temporal alignment.

```text
GNSS
IMU
BLE sensor
battery telemetry
LoRa observation
        |
        v
common timestamped observation model
        |
        +--> recorder
        +--> application
        +--> optional fusion provider
```

Fusion algorithms SHOULD be replaceable providers rather than hard-coded into the sensor core.

---

# Part III — Streams, IPC, and Data Movement

## 10. Stream and Pipe Abstraction

A generic stream abstraction is a high-priority platform primitive.

RiscRTE SHOULD support byte streams and record/message streams.

Examples:

```text
USB serial -> terminal
USB serial -> file
GNSS -> recorder
BLE sensor -> recorder
LoRa -> decoder
file -> firmware programmer
network -> updater
sensor stream -> graph
```

### 10.1 Stream properties

A stream SHOULD expose metadata such as:

```text
readable/writable
data type
seekability
bounded buffer policy
source/sink identity
backpressure support
EOF/disconnect semantics
```

### 10.2 Pipes

The runtime SHOULD permit compatible endpoints to be connected without either module knowing the other's implementation.

```text
source -> transform -> sink
```

Transforms may include:

```text
decoder
encoder
filter
compressor
checksum
logger
rate limiter
packetizer
```

### 10.3 Backpressure

All streams SHALL have bounded buffering and explicit overflow/backpressure semantics.

---

## 11. Inter-Module IPC

RiscRTE SHOULD replace arbitrary cross-ELF callback pointers with structured IPC.

Required communication forms should include:

```text
event publish/subscribe
request/reply
one-way message
stream/pipe
job progress/completion
capability invocation
```

Messages SHALL use versioned data structures or serialization formats safe across module lifetimes.

Persistent state SHALL never contain raw pointers into another ELF.

---

## 12. Unified Event Bus

The service event bus SHOULD expand into a platform-wide event fabric.

Namespaces might include:

```text
system.*
power.*
network.*
storage.*
device.*
sensor.*
location.*
usb.*
bluetooth.*
application.*
package.*
job.*
notification.*
```

Events SHALL have bounded delivery and durability semantics appropriate to their class.

---

# Part IV — Storage, Files, and Data

## 13. Unified Storage Provider Layer

SD and USB mass storage SHOULD expose the same logical volume abstraction.

Future providers may include network-backed storage.

A volume record SHOULD include:

```text
volume ID
provider/device ID
mount point
filesystem type
capacity/free space
read/write capability
removability
ownership state
health/error state
```

### 13.1 Storage events

```text
storage.volume_added
storage.mounted
storage.unmounting
storage.unmounted
storage.removed
storage.low_space
storage.error
```

### 13.2 Safe removal

Volume ownership SHALL coordinate:

```text
open files
storage-VM mappings
recorder buffers
jobs
USB MSC export
filesystem flush
```

---

## 14. Unified Data Recorder and Time-Series Store

The BLE Sensor Recorder SHOULD evolve into a general recorder for timestamped platform observations.

Sources may include:

```text
sensor measurements
GNSS/location
battery telemetry
LoRa packets
USB instrument measurements
system performance counters
network observations
application-defined channels
```

### 14.1 Recorder API

The recorder SHOULD support:

```text
channel registration
append measurement/event
batching
query by time range
latest value
aggregation/downsampling
export
retention policy
```

### 14.2 Storage principles

- append/batch rather than one filesystem transaction per sample;
- bounded in-memory buffers;
- recoverable format;
- versioned schema;
- explicit timestamp validity;
- optional indexes built asynchronously;
- storage quotas/retention policies.

---

## 15. Content Type and Intent Routing

File Browser and applications SHOULD not hard-code file-extension-to-app mappings.

Applications register intents/content handlers.

Examples:

```text
image/png -> Image Viewer
image/jpeg -> Image Viewer
application/epub+zip -> Reader
text/markdown -> Reader/Editor
application/x-riscrte-package -> Package Manager
application/x-firmware -> Programmer
```

Framework operations could include:

```text
open(resource)
open_with(resource)
share(resource)
inspect(resource)
install(resource)
program(resource, target)
```

Intent routing SHOULD use application manifests without loading candidate application ELFs.

---

## 16. Clipboard and Share Framework

RiscRTE SHOULD eventually provide a small system clipboard and resource-sharing model.

Clipboard payloads SHOULD be typed and bounded.

Potential types:

```text
text
URI/resource reference
file handle/reference
sensor value
location
small binary payload
```

Large files SHALL be shared by handle/reference, not copied into clipboard memory.

---

## 17. System Search and Indexing

Search SHOULD become a service/capability rather than an application-specific implementation.

Indexable sources may include:

```text
files
books/documents
applications
settings
sensor channels/history metadata
logs
documentation
```

Indexing SHALL be incremental, storage-aware, interruptible, and low-priority.

The index itself belongs on persistent storage with a bounded active cache.

---

# Part V — Networking and Communications

## 18. Networking Services Above Wi-Fi

Wi-Fi hardware access should be separated from common network services.

RiscRTE SHOULD provide reusable facilities for:

```text
DNS
HTTP/HTTPS
WebSocket
NTP/time synchronization
mDNS/service discovery
resumable downloads
connection-state events
TLS policy
future MQTT
```

Applications SHOULD not each independently implement connection lifecycle, TLS policy, retries, and background downloads.

---

## 19. Unified Communications and Message Transport

RiscRTE SHOULD eventually define a message/packet abstraction above transports such as:

```text
BLE
LoRa
Wi-Fi/IP
USB CDC
UART
```

Conceptually:

```text
application message
      |
endpoint/routing policy
      |
transport provider
      |
physical link
```

The initial goal SHOULD be common endpoint/message semantics, not automatic mesh routing.

Routing and multi-transport failover can be added later.

---

## 20. Device and Service Discovery

Discovery SHOULD unify observations from:

```text
BLE advertisements
USB enumeration
mDNS/network discovery
known UART/I2C devices
LoRa peer discovery where protocol supports it
```

The Device Registry consumes these discoveries and presents one coherent inventory.

Example:

```text
Weather Station
  BLE
  sensor.temperature
  sensor.humidity

Desktop PC
  Wi-Fi
  network.http
  debug.gdb

USB Probe
  USB
  debug.swd

GNSS Receiver
  UART
  location.position
```

---

# Part VI — System Services and User-Facing Platform Facilities

## 21. Notification Framework

Services and applications SHOULD post notifications through RiscRTE rather than owning global UI.

A notification may include:

```text
source
priority
title
message
icon
timestamp
persistence
optional actions
wake/display policy
```

Possible priorities:

```text
BACKGROUND
NORMAL
IMPORTANT
CRITICAL
```

Notification actions SHALL route back through intents/messages rather than raw function pointers.

---

## 22. Persistent Alarm and Deadline Framework

The scheduler concepts from `SERVICE_RUNTIME_ARCHITECTURE.md` SHOULD become a first-class platform API.

Required classes:

```text
EXACT
FLEXIBLE
PERIODIC
IDLE
MONOTONIC
LOCAL_CIVIL_TIME
```

Deadlines SHALL survive service unloading and, where configured, deep sleep/restart.

The runtime owns wake-source programming.

---

## 23. Automation and Rules Engine

Once events, sensors, notifications, jobs, and capabilities are available, RiscRTE SHOULD support declarative automation.

Example:

```text
WHEN sensor.temperature > 30 C
AND device.sensor_a is available
THEN
    recorder.mark_event("high-temperature")
    notification.post(...)
```

Another example:

```text
WHEN storage.volume_added
AND content_type == application/x-riscrte-package
THEN notification.post("Install package?")
```

Rules SHALL be event/scheduler driven and SHALL not require a resident foreground application.

### 23.1 Rule safety

Rules SHALL have:

```text
explicit permissions
bounded execution
rate limiting
loop detection
resource quotas
audit/logging
```

---

# Part VII — Security Platform Services

## 24. Credential and Secrets Vault

Credentials SHOULD be system-owned objects accessed through opaque handles.

Examples:

```text
Wi-Fi credentials
BLE bonds
API tokens
TLS private keys/certificates
service credentials
future SSH keys
```

Applications/providers SHOULD request authorized use of a credential rather than reading plaintext secrets whenever possible.

Conceptually:

```text
credential handle
      |
authorized operation
      |
network/crypto service
```

The vault SHALL integrate with `SECURITY_ARCHITECTURE.md`.

---

## 25. Cryptographic Service

Common cryptographic primitives SHOULD be exposed through versioned runtime APIs rather than statically duplicated throughout ELF modules where practical.

Capabilities may include:

```text
crypto.random
crypto.hash.sha256
crypto.hmac
crypto.signature.verify
crypto.signature.sign using protected key handle
crypto.certificate.verify
crypto.key.derive
```

Private keys SHOULD be represented by opaque handles when hardware/platform storage permits.

Crypto APIs SHALL distinguish authentication, integrity, encryption, and hashing rather than treating them as interchangeable.

---

# Part VIII — Package and Module Management

## 26. Unified RiscRTE Package Model

Applications, drivers, services, and providers SHOULD converge on one package envelope.

Conceptually:

```text
package.risc
  |
  +-- manifest
  +-- one or more ELF modules
  +-- resources
  +-- schemas/metadata
  +-- dependency declarations
  +-- capability declarations
  +-- architecture requirements
  +-- minimum/maximum compatible RiscRTE version
  +-- security version
  +-- signatures
```

Module type remains explicit:

```text
application
driver
service
provider
```

A package is a distribution/install unit; an ELF is an executable module within it.

---

## 27. Package Manager

A system Package Manager SHOULD own:

```text
install
validate
signature verification
dependency resolution
capability requirement checks
update
atomic replacement
rollback
uninstall
quarantine
inventory
```

Discovery SHOULD inspect package manifests without loading executable code.

### 27.1 Offline installation

Packages MUST remain installable from removable storage without requiring a desktop-side Python installer.

A user should be able to place/extract a package onto an SD card or install it through an on-device Package Manager according to package format policy.

### 27.2 Online installation

The same package format SHOULD be usable by the App Store/update services.

---

## 28. Dependency Model

Dependencies SHOULD normally target capabilities or ABI contracts rather than concrete module filenames.

Prefer:

```text
requires:
  bluetooth.gatt.client >= ABI 1
  sensor.framework >= ABI 1
```

rather than:

```text
requires:
  /Drivers/foo/foo.elf
```

Concrete provider dependencies may still be supported where technically unavoidable.

---

# Part IX — Jobs and Long-Running Work

## 29. Generic Job Framework

The job model from `PROGRAMMER_DEBUGGER_ARCHITECTURE.md` SHOULD become a reusable runtime facility.

Candidate jobs include:

```text
firmware programming
file copy/move
large download
package install/update
index rebuild
sensor export
book conversion/indexing
storage verification
sync operation
```

### 29.1 Job state

Generic states may include:

```text
QUEUED
WAITING
RUNNING
PAUSED
CANCELLING
COMPLETE
FAILED
CANCELLED
```

### 29.2 Job metadata

```text
job ID
owner
job type
progress
status text
start/end timestamps
resource leases
cancel capability
persistent/resumable flag
result/error
```

### 29.3 Job UI

The framework SHOULD be able to expose active jobs globally without each application implementing its own progress infrastructure.

---

# Part X — Diagnostics and Observability

## 30. Structured Logging

RiscRTE SHOULD standardize structured logs across core, apps, drivers, services, and providers.

A log record SHOULD include where available:

```text
timestamp
severity
module identity
execution context
event/code
message
structured fields
```

Logging SHALL be bounded and should support both serial diagnostics and persistent ring/batched storage.

---

## 31. Runtime Observability

A system diagnostics capability SHOULD expose:

```text
SRAM/PSRAM usage
storage-VM cache
loaded ELF modules
module memory usage
tasks/work items by owner
capability leases
power locks
open files
mounted volumes
USB topology
BLE devices/connections
network state
sensor registry
active streams
active jobs
service states
driver/provider states
watchdog/crash history
queue overflows
```

This should become the primary source of truth for debugging ownership/lifecycle problems.

---

## 32. Crash and Failure Records

When possible, crashes SHALL preserve enough metadata to identify:

```text
module/execution context
firmware/RiscRTE version
loaded module versions
fault address/reason
resource ownership snapshot
memory pressure state
recent structured log tail
```

A failing optional provider/service SHOULD be quarantinable without preventing the platform from booting.

---

# Part XI — Framework Integration

## 33. Application Model

Applications SHOULD increasingly consume platform facilities through:

```text
capabilities
intents
jobs
streams
events
resource handles
semantic devices/sensors
```

They SHOULD NOT directly own:

```text
USB controller
BLE stack
shared I2C/SPI buses
Wi-Fi hardware
filesystem global state
system scheduler
system power state
```

---

## 34. Service Model

Services perform persistent/background logical roles while the runtime owns execution lifetime.

Examples from this roadmap:

```text
Sensor Recorder
Search Indexer
Package Updater
Notification Manager
Time Synchronizer
Automation Engine
Data Export/Sync
```

A service's logical existence SHALL remain independent of whether its ELF is currently resident.

---

## 35. Driver Model

Drivers translate controlled hardware access into capabilities.

Examples:

```text
GPS UART driver -> location.*
USB FTDI driver -> serial.host
BLE radio stack/core -> bluetooth.*
I2C sensor driver -> sensor.*
USB MSC provider -> storage.removable
```

Drivers SHOULD not embed unrelated application workflows.

---

## 36. Provider Model

Providers implement protocol, decoding, transformation, or higher-level capability behavior without necessarily owning physical hardware.

Examples:

```text
BLE proprietary sensor decoder
Espressif programming protocol
STM32 SWD programming provider
sensor fusion provider
stream codec
file/content parser
```

This distinction allows hardware transport and semantic protocol behavior to evolve independently.

---

# Part XII — Priority and Implementation Order

## 37. Priority 1: Unified Device Registry

This should be implemented early because USB, BLE, serial, sensors, programmers, and future buses all need a common identity/capability model.

First acceptance test:

```text
USB device + BLE sensor + GNSS receiver
        |
        v
one Device Registry
        |
        v
three independently queryable logical devices
with semantic capabilities
```

---

## 38. Priority 2: Streams and Pipes

Streams should follow because they remove a large amount of application-specific data plumbing.

First acceptance tests:

```text
USB serial -> terminal
USB serial -> file
GNSS -> recorder
file -> programmer
```

All should use the same stream abstraction with bounded buffering.

---

## 39. Priority 3: Unified Package Manager

Package management should follow once module classes and capability contracts stabilize enough to distribute independently.

First acceptance test:

```text
one signed package format
      |
      +-- install app
      +-- install driver
      +-- install service
      +-- install provider
```

The Package Manager validates without executing package contents and supports atomic update/rollback.

---

## 40. Priority 4: Generic Sensor + Recorder

Generalize the BLE sensor work into transport-independent sensor channels and one historical recorder.

Acceptance test:

```text
BLE temperature
GNSS position
battery telemetry
        |
        v
same recorder API
        |
        v
query/export synchronized history
```

---

## 41. Priority 5: Bus Managers

Implement framework-owned I2C/SPI/GPIO access so external hardware can participate in the same driver/provider ecosystem without creating unmanaged bus ownership.

---

## 42. Priority 6: Jobs, Notifications, and Intents

These facilities make long-running work and cross-application workflows coherent.

Example:

```text
File Browser selects firmware
        |
        v
intent: program(resource)
        |
        v
Programmer creates Job
        |
        v
system job UI + notification on completion
```

---

## 43. Priority 7: Vault and Crypto Services

Centralize secrets and cryptographic operations before third-party/native packages become broadly installable.

---

## 44. Priority 8: Search, Automation, and Communications

These become much more valuable after devices, events, sensors, jobs, and intents are stable.

---

# Part XIII — Cross-Cutting Requirements

## 45. Bounded memory

Every new capability SHALL be designed for bounded SRAM/PSRAM consumption.

Large/cold state belongs in storage-backed structures according to `MEMORY_ARCHITECTURE.md`.

No registry, queue, stream, recorder, log, or job history may grow indefinitely in RAM.

---

## 46. ELF lifecycle safety

No platform facility may retain raw callback/function pointers into an ELF after that module becomes unloadable.

Cross-module communication SHALL use framework-owned handles, messages, events, jobs, or streams.

Unload SHALL deterministically revoke resources.

---

## 47. Security

Every capability SHALL have an authorization model compatible with `SECURITY_ARCHITECTURE.md`.

Sensitive examples include:

```text
HID injection
firmware programming/debugging
credential access
cryptographic signing
raw bus/GPIO access
network listening
filesystem writes
package installation
```

Authentication of a signed module does not imply authorization for every capability.

---

## 48. Power

Capabilities SHALL cooperate with centralized power policy.

Background work should prefer:

```text
events
deadlines
bounded jobs
duty cycling
wake sources
```

rather than permanent polling tasks.

---

## 49. Versioning

Public ABI structures, capability contracts, package schemas, persistent records, IPC messages, stream formats, and recorder formats SHALL be explicitly versioned.

Backward compatibility should be deliberate rather than accidental.

---

## 50. Hardware independence

`RiscRTE` is the runtime/platform name. Hardware names such as T5S3 are board identifiers only.

New architecture SHALL avoid using board names as generic runtime API concepts.

Board-specific implementation belongs behind platform abstraction and hardware profiles.

---

# Part XIV — Reference End State

## 51. Example integrated system

A mature RiscRTE installation might look like:

```text
                         RiscRTE
                            |
                    Device Registry
                            |
       +--------------------+--------------------+
       |                    |                    |
       v                    v                    v
 BLE weather sensor     USB debug probe      UART GNSS
       |                    |                    |
 sensor.temperature      debug.swd         location.position
 sensor.humidity         program.target    location.time
       |                    |                    |
       +--------------------+--------------------+
                            |
                    Capability Resolver
                            |
       +--------------------+--------------------+
       |                    |                    |
     Streams              Events               Jobs
       |                    |                    |
       +---------+----------+----------+---------+
                 |                     |
                 v                     v
          Data Recorder          Applications
                 |                     |
                 v                     +-- Reader
                SD                     +-- Sensors
                                       +-- Programmer
                                       +-- File Browser
                                       +-- Diagnostics
                                       +-- Package Manager
```

An application can disappear from memory while devices continue to be discovered, measurements continue to be recorded, alarms remain scheduled, jobs continue under system ownership, and services are loaded only when their logical work requires execution.

---

## 52. Platform acceptance criteria

The capability roadmap can be considered substantially realized when RiscRTE can demonstrate the following without rebuilding core firmware for each new implementation:

1. discover USB, BLE, and bus-attached devices into one registry;
2. bind signed drivers/providers dynamically;
3. resolve semantic capabilities independently of transport;
4. route bounded streams between independently developed modules;
5. record sensor/location/system data through one time-series service;
6. install/update/rollback applications, drivers, services, and providers through one package system;
7. execute long-running operations as framework-owned jobs;
8. route files/resources through intents rather than hard-coded app dependencies;
9. post system notifications from unloaded/background logical services;
10. retain alarms/deadlines across sleep and service unloading;
11. protect credentials through opaque vault handles;
12. perform common cryptographic operations through authorized runtime capabilities;
13. inspect resource ownership and subsystem state through unified diagnostics;
14. automatically reclaim all resources when an ELF exits, fails, or is unloaded;
15. add a new supported sensor, programming protocol, storage device, or application primarily by installing a package rather than modifying core firmware.

The architectural transition is complete when **RiscRTE core firmware primarily supplies policy, lifecycle, resource management, security, IPC, capability resolution, and hardware primitives, while most extensible behavior resides in independently installable applications, drivers, services, and providers.**
