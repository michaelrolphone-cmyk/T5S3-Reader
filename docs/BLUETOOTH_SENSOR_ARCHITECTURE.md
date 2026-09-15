# RiscRTE Bluetooth LE Sensor Discovery and Recording Architecture

## Status

Architecture specification for Bluetooth Low Energy discovery, automatic sensor classification, connection management, GATT access, semantic sensor providers, and persistent measurement recording in RiscRTE.

This specification treats Bluetooth as a framework-owned shared radio subsystem. Applications and sensor providers do not own the ESP32-S3 Bluetooth controller directly.

> **Core invariant:** RiscRTE owns BLE scanning, connections, GATT resources, security state, and radio policy. Pluggable providers interpret device-specific data and expose semantic sensor capabilities. Applications consume sensor measurements rather than Bluetooth implementation details.

---

## 1. Goals

The Bluetooth sensor architecture SHALL:

1. continuously or periodically discover nearby BLE devices according to power policy;
2. record useful measurements directly from advertisements when connection is unnecessary;
3. classify known standard Bluetooth sensor services automatically;
4. connect to eligible unknown/known devices when GATT discovery is required;
5. enumerate services, characteristics, descriptors, and characteristic properties;
6. bind standard or pluggable ELF sensor providers to recognized devices;
7. subscribe to notifications/indications where appropriate;
8. support bounded polling/read workflows for sensors that do not notify;
9. maintain stable logical sensor identities across disconnect/reconnect where possible;
10. expose normalized semantic capabilities such as temperature, humidity, pressure, acceleration, battery state, and other measurements;
11. record timestamped sensor data to persistent storage without requiring the foreground application to remain resident;
12. support multiple sensors concurrently within measured controller/memory/power limits;
13. integrate pairing/bonding and authorization without attempting to bypass device security;
14. isolate BLE transport ownership from sensor decoding;
15. support independently installable signed providers for proprietary sensor protocols;
16. integrate with RiscRTE services, events, memory management, power management, and security architecture.

---

## 2. Non-goals

The initial implementation SHALL NOT claim to:

- infer the physical meaning of arbitrary proprietary GATT bytes without a known schema/provider;
- bypass pairing, authentication, encryption, authorization, PINs, passkeys, or vendor security;
- maintain connections to every connectable advertisement indiscriminately;
- guarantee stable identity from a randomized BLE address alone;
- expose unrestricted raw controller access to ordinary applications;
- keep every discovered device permanently connected;
- treat RSSI as a precise ranging measurement;
- require all measurements to pass through a live GATT connection when advertisements already contain the data.

---

## 3. Architecture

```text
                         Applications
                              |
                     semantic sensor APIs
                              |
                              v
+------------------------------------------------------------+
|                  RiscRTE Sensor Layer                      |
| registry | subscriptions | normalized measurements         |
+-----------------------------+------------------------------+
                              |
              +---------------+----------------+
              |                                |
              v                                v
+---------------------------+       +-------------------------+
| Sensor Provider Resolver  |       | Sensor Recorder Service |
| standard + ELF providers  |       | batching / SD / export  |
+-------------+-------------+       +------------+------------+
              |                                  ^
              v                                  |
+------------------------------------------------------------+
|                 RiscRTE Bluetooth Manager                  |
| scan | advertisements | connections | GATT | security      |
| identity | subscriptions | radio/power/resource policy     |
+-----------------------------+------------------------------+
                              |
                              v
                       ESP-IDF / NimBLE
                              |
                              v
                      ESP32-S3 BLE radio
```

---

## 4. Framework ownership

The RiscRTE Bluetooth Manager SHALL own:

- Bluetooth controller initialization/shutdown;
- host stack initialization;
- scan configuration;
- advertisement reception;
- connection establishment/termination;
- connection handles;
- GATT client procedures;
- service/characteristic discovery;
- notification/indication subscriptions;
- pairing/bonding integration;
- connection parameter policy;
- radio coexistence policy;
- resource accounting;
- reconnect scheduling;
- device identity registry;
- raw BLE event translation into RiscRTE events.

Applications SHALL NOT initialize a competing BLE stack.

---

## 5. BLE implementation direction

For BLE-only RiscRTE functionality, the implementation SHOULD prefer the ESP-IDF NimBLE host unless measurements or required functionality establish a reason to use a different stack.

The RiscRTE public ABI SHALL remain independent of NimBLE-specific structures so the underlying stack can change without invalidating application/provider contracts.

No NimBLE pointer or callback object SHALL become a persistent application ABI object.

---

## 6. Discovery modes

The Bluetooth Manager SHOULD support policy-controlled scan modes such as:

```text
BLE_SCAN_OFF
BLE_SCAN_PASSIVE
BLE_SCAN_ACTIVE
BLE_SCAN_DISCOVERY_BURST
BLE_SCAN_BACKGROUND
```

Exact scan intervals/windows SHALL be implementation policy rather than application-controlled radio registers.

Applications/services may request discovery intent, but RiscRTE owns the final radio schedule.

---

## 7. Advertisement registry

Each observed device SHOULD produce/update a registry entry containing available information such as:

```text
logical device ID
current address/address type
identity address when resolved
advertised name
RSSI
last seen timestamp
first seen timestamp
advertised service UUIDs
manufacturer data
service data
flags
connectability
bond/security state
known provider/classification
active connection state
observation count
```

The registry SHALL distinguish transient radio addresses from logical sensor identity.

---

## 8. Advertisement-first sensing

RiscRTE SHALL prefer connectionless measurement collection when a supported sensor publishes sufficient data in advertisements or service/manufacturer data.

```text
advertisement
     |
     v
provider match/decode
     |
     v
sensor.measurement
     |
     v
recorder/subscribers
```

This reduces connection count, memory usage, latency, radio occupancy, and sensor battery consumption.

A device SHALL NOT be connected merely because it is connectable.

---

## 9. Connection decision policy

A connection MAY be attempted when one or more of the following apply:

- a known provider requires GATT access;
- advertised standard services require characteristic discovery/read/notify;
- an authorized discovery policy permits temporary classification of an unknown device;
- a user explicitly requests inspection/connection;
- a previously configured sensor requires reconnection;
- a service requires a measurement unavailable from advertisements.

Connection attempts SHALL be bounded and rate-limited.

Unknown devices that repeatedly reject connections SHALL enter backoff rather than consuming continuous radio/power resources.

---

## 10. GATT discovery

For an eligible connected device, the Bluetooth Manager SHALL be capable of discovering:

```text
services
  |
  +-- characteristics
          |
          +-- properties
          |     READ
          |     WRITE
          |     WRITE WITHOUT RESPONSE
          |     NOTIFY
          |     INDICATE
          |
          +-- descriptors
```

Discovery results SHALL be represented using RiscRTE-owned immutable/opaque metadata rather than exposing stack internals.

---

## 11. GATT handles

Public APIs SHALL use opaque framework handles.

Conceptually:

```c
typedef uint32_t riscrte_ble_device_t;
typedef uint32_t riscrte_ble_service_t;
typedef uint32_t riscrte_ble_characteristic_t;
typedef uint32_t riscrte_ble_subscription_t;
```

Handles SHALL be generation-aware and SHALL fail cleanly after disconnect/re-enumeration.

Providers SHALL NOT persist raw ATT/GATT handles as durable identity without appropriate rediscovery/version validation.

---

## 12. Standard sensor profiles

RiscRTE SHOULD contain or package decoders for useful standardized Bluetooth SIG services/profiles where applicable.

A standard decoder converts protocol-specific values into semantic RiscRTE measurements.

Conceptually:

```text
standard GATT characteristic
          |
          v
standard profile decoder
          |
          v
sensor.temperature
sensor.humidity
sensor.pressure
sensor.heart_rate
sensor.battery
...
```

The exact initial profile set SHALL be selected from real target hardware/use cases rather than attempting to implement every Bluetooth SIG profile at once.

---

## 13. Proprietary sensor providers

Unknown vendor-specific UUIDs and manufacturer payloads require explicit interpretation.

RiscRTE SHALL support pluggable sensor-provider ELFs.

Conceptual packages:

```text
/Drivers/ or /Providers/
    ble-sensor-vendor-a/
        provider.elf
        manifest.json
        signature.bin

    ble-sensor-vendor-b/
        provider.elf
        manifest.json
        signature.bin
```

The final package taxonomy SHOULD distinguish radio/hardware drivers from protocol/data providers while preserving independent installation.

---

## 14. Provider matching

A provider manifest MAY match using combinations of:

```text
advertised service UUID
GATT service UUID
characteristic UUID set
manufacturer ID/data pattern
service-data pattern
device name pattern
VID/vendor metadata where applicable
known device/product identifier
```

Matching rules SHALL be declarative metadata where possible so RiscRTE can decide whether loading a provider ELF is worthwhile.

The system SHOULD avoid loading every installed provider merely to inspect every advertisement.

---

## 15. Provider capability model

A BLE sensor provider consumes bounded Bluetooth capabilities and emits semantic sensor capabilities/events.

Example:

```text
ble-sensor-environment-x.elf

consumes:
    bluetooth.advertisement
    bluetooth.gatt.client

provides:
    sensor.temperature
    sensor.humidity
    sensor.pressure
```

A provider SHALL NOT own the Bluetooth controller.

---

## 16. Semantic measurement model

RiscRTE SHALL normalize measurements into a transport-independent representation.

Conceptually:

```c
typedef struct {
    uint64_t timestamp;
    uint32_t sensor_id;
    uint32_t channel_id;
    uint32_t quantity;
    double value;
    uint32_t unit;
    uint32_t flags;
    int16_t rssi;
} riscrte_sensor_measurement_v1;
```

The exact ABI and numeric representation SHALL be versioned before implementation.

Measurement metadata SHOULD support:

- source sensor;
- logical channel;
- physical quantity;
- value;
- unit;
- timestamp;
- validity/quality flags;
- source RSSI when relevant;
- sequence/sample identity when available;
- provider identity/version when needed for audit/debugging.

---

## 17. Sensor identity

A logical sensor SHALL have a RiscRTE identity independent of the current BLE connection handle.

Identity evidence MAY include:

1. bonded identity/address resolution;
2. stable device-provided unique identifier;
3. manufacturer/product serial information;
4. stable advertised identity data;
5. user-assigned alias bound to known identity evidence;
6. address only as a weaker fallback when no stronger identity exists.

RiscRTE SHALL not silently merge two physical devices merely because they expose identical service UUIDs.

---

## 18. Sensor registry

The persistent Sensor Registry SHOULD store known sensors and their configuration.

Example logical record:

```text
sensor_id
friendly name
identity evidence
provider ID/version
known service/characteristic schema
bond/security reference
recording enabled
connection policy
poll interval
last seen
last measurement
channels
user metadata
```

Sensitive bonding keys SHALL remain in the platform security/storage mechanism rather than ordinary provider data.

---

## 19. Connection manager

The Bluetooth Manager SHALL centrally arbitrate connections.

It SHALL account for:

```text
controller connection limits
host-stack memory
PSRAM/internal SRAM
radio airtime
connection intervals
Wi-Fi coexistence
sensor battery cost
foreground priority
background recording requirements
```

The architecture SHALL not expose a promise of unlimited simultaneous BLE connections.

Measured platform limits SHALL determine policy.

---

## 20. Connection states

A managed sensor connection SHOULD use explicit states:

```text
DISCOVERED
CLASSIFIED
WAITING
CONNECTING
CONNECTED
DISCOVERING_GATT
AUTHENTICATING
READY
SUBSCRIBED
BACKOFF
DISCONNECTING
DISCONNECTED
FAILED
```

The state machine SHALL tolerate disconnect at every asynchronous stage.

---

## 21. Notification/indication subscriptions

For characteristics supporting NOTIFY or INDICATE, providers SHOULD prefer subscriptions over frequent polling when appropriate.

The Bluetooth Manager owns the underlying subscription and dispatches decoded/bounded events to the provider.

Provider unload, device disconnect, service shutdown, or capability revocation SHALL reclaim subscriptions.

No callback into an unloaded ELF may remain registered.

---

## 22. Polling sensors

Some sensors require characteristic reads.

Polling SHALL be scheduler-owned rather than implemented as an unbounded provider task.

```text
provider requests sample every N seconds
        |
        v
RiscRTE scheduler
        |
        v
connection/read opportunity
        |
        v
provider decode
        |
        v
measurement event
```

Polling frequency SHALL respect global power/radio policy and provider/device constraints.

---

## 23. Pairing, bonding, and security

RiscRTE SHALL support legitimate BLE security procedures required by sensors.

Possible flows include:

```text
unencrypted connection
paired connection
bonded reconnect
authenticated pairing
passkey/user confirmation when required
```

The framework SHALL NOT attempt to bypass device access control.

When user interaction is required, the Bluetooth Manager SHALL surface an explicit pairing request through framework UI/events.

Providers SHALL not receive long-term security keys unless their privilege explicitly requires it.

---

## 24. Unknown-device inspection

RiscRTE MAY support a BLE Inspector application that displays:

```text
address/identity
name
RSSI
advertisement fields
service UUIDs
manufacturer data
connectability
GATT services
characteristics
properties
descriptors
raw values where authorized
matched provider
```

This is useful for developing new provider modules.

Inspection SHALL remain distinct from claiming that unknown bytes have known sensor semantics.

---

## 25. Sensor event bus

Normalized measurements SHALL be publishable through the RiscRTE event/capability system.

Conceptual events:

```text
sensor.discovered
sensor.available
sensor.unavailable
sensor.measurement
sensor.battery
sensor.error
sensor.identity_changed
bluetooth.pairing_required
```

Consumers SHOULD subscribe by sensor ID, quantity/capability, provider, or broader policy without requiring direct GATT access.

---

## 26. Sensor Recorder Service

Persistent logging SHALL be a RiscRTE service independent of the foreground scanner UI.

```text
BLE / sensor provider
        |
        v
sensor.measurement
        |
        v
Sensor Recorder Service
        |
        +-- batch
        +-- timestamp
        +-- validate
        +-- persist
        v
       SD
```

The recorder SHALL be able to continue operating while the BLE Scanner application is not resident.

---

## 27. Recording policy

Recording configuration SHOULD be per sensor/channel and support policies such as:

```text
OFF
ALL_SAMPLES
CHANGE_ONLY
INTERVAL_DOWNSAMPLE
EVENT_ONLY
```

The recorder MAY support thresholds/deadbands to reduce unnecessary writes for high-frequency slowly changing sensors.

Raw high-rate data and normalized historical data MAY require different storage policies.

---

## 28. Storage format

The first recorder implementation SHOULD use a compact append/batched format appropriate to SD rather than one JSON file write per sample.

A record SHOULD minimally preserve:

```text
timestamp
sensor ID
channel/quantity
value
unit
flags
```

Optional fields may include RSSI, provider version, sequence number, and raw payload reference.

The format SHALL be versioned and recoverable after interrupted writes.

A later indexed database layer MAY be added for historical queries without changing the sensor event ABI.

---

## 29. SD endurance

The recorder SHALL integrate with `MEMORY_ARCHITECTURE.md` and avoid pathological SD write amplification.

Strategies SHOULD include:

- bounded RAM/PSRAM batching;
- sequential append;
- periodic flush;
- configurable durability interval;
- downsampling/change-only recording;
- index updates less frequently than individual samples;
- clean shutdown/sleep flush.

Critical data may request stronger durability at higher write cost.

---

## 30. Time semantics

Sensor records require explicit time semantics.

The recorder SHOULD retain:

- monotonic acquisition ordering;
- system/UTC wall-clock timestamp when valid;
- indication of whether wall-clock time was synchronized;
- optional sensor-origin timestamp when supplied by the sensor.

A later wall-clock correction SHALL not destroy acquisition ordering.

This SHALL integrate with `SERVICE_RUNTIME_ARCHITECTURE.md` time synchronization concepts.

---

## 31. Power management

BLE scanning and connections are system power consumers.

RiscRTE SHALL support policies such as:

```text
foreground scanner: high discovery responsiveness
background recorder: duty-cycled scan where acceptable
known connected sensor: maintain only when recording requires it
battery low: reduce scan duty/frequency according to policy
USB/external power: permit more aggressive discovery
```

A provider SHALL not indefinitely acquire a global power lock merely to wait for a sensor.

---

## 32. Wi-Fi coexistence

ESP32-S3 Wi-Fi and Bluetooth share radio resources.

The Bluetooth Manager SHALL coordinate with framework network policy rather than assuming uninterrupted BLE airtime.

High-throughput Wi-Fi operations and latency-sensitive BLE sampling MAY require explicit priority/coexistence policy.

Sensor recording SHALL tolerate temporary radio scheduling delays without corrupting identity or logs.

---

## 33. Memory behavior

BLE data paths SHALL be bounded.

Advertisement payloads, GATT values, notification queues, and recorder batches SHALL have explicit maximum sizes/counts.

Large histories SHALL reside on SD/storage VM, not in an indefinitely growing in-memory vector.

Provider ELFs may be unloaded when no matching sensor requires them, subject to lifecycle policy.

---

## 34. Backpressure

A high-rate sensor SHALL not be allowed to exhaust memory if storage or consumers fall behind.

The event/recorder path SHALL define bounded queues and an overflow policy.

Possible policies include:

```text
preserve all until bounded buffer full then report loss
keep latest
aggregate/downsample
drop low-priority samples
provider-specific flow control when protocol supports it
```

Dropped samples SHALL be observable through diagnostics/counters rather than silently disappearing.

---

## 35. Raw data retention

Providers MAY optionally request raw advertisement/GATT payload retention for diagnostics or future re-decoding.

Raw retention SHALL be separate from normalized sensor recording and disabled/bounded by default because it can substantially increase storage usage and may contain device-specific identifiers.

---

## 36. Privacy

BLE scanning can observe devices belonging to other people nearby.

RiscRTE SHOULD minimize unnecessary persistent retention of unrelated device identifiers.

Default persistent recording SHOULD focus on configured/recognized sensors rather than permanently logging every transient BLE address encountered.

Diagnostic discovery logs SHOULD have explicit retention policy.

---

## 37. Security and provider trust

Production sensor-provider ELFs SHALL follow `SECURITY_ARCHITECTURE.md`.

A signed provider proves authorized code identity; it does not automatically grant unrestricted Bluetooth or filesystem access.

Provider capabilities SHOULD be narrowly scoped, for example:

```text
bluetooth.advertisement.read
bluetooth.gatt.client
sensor.publish.temperature
sensor.publish.humidity
```

The Bluetooth Manager SHALL validate provider requests against the device/provider binding and current ownership.

---

## 38. Malformed device defense

Nearby BLE devices are untrusted external inputs.

The Bluetooth/GATT/provider path SHALL validate:

- advertisement lengths;
- UUID encodings;
- GATT value lengths;
- integer arithmetic;
- provider-declared maximum payloads;
- characteristic expectations;
- string termination/encoding;
- notification rates;
- descriptor metadata;
- state transitions.

Provider parsers SHOULD be fuzz-tested using malformed and truncated payloads.

---

## 39. Diagnostics

RiscRTE BLE diagnostics SHOULD expose:

```text
Bluetooth stack/controller state
scan mode/window/interval policy
advertisements received
unique devices observed
known/unknown devices
RSSI/last seen
active connections
connection failures/backoff
GATT discovery status
matched provider
active subscriptions
notification/read counts
pairing/bond status without exposing secrets
memory usage
queue depth/drops
radio/coexistence state
recorder queue/batch state
SD write errors
measurements recorded
```

Per-device diagnostics SHOULD expose the complete resolution chain:

```text
advertisement
 -> logical BLE identity
 -> service/GATT schema
 -> matched provider
 -> semantic sensor ID/channels
 -> recorder/subscribers
```

---

## 40. Scanner application

A foreground BLE Scanner/Sensors application SHOULD be a consumer of the framework rather than the owner of scanning.

A useful view could present:

```text
Nearby Sensors

Environment A
  -64 dBm
  Temperature 21.4 C
  Humidity 43.1 %
  Recording

Sensor B
  -72 dBm
  Pressure 101.2 kPa
  Recording

Unknown Device
  -81 dBm
  1 advertised service
  Connectable
  No installed provider
```

The app MAY request inspection, pairing, aliasing, recording configuration, and provider installation workflows.

Closing the app SHALL not stop background recording when system policy says recording remains active.

---

## 41. Service integration

The Bluetooth subsystem SHOULD integrate with `SERVICE_RUNTIME_ARCHITECTURE.md`.

Potential services include:

```text
Sensor Recorder
Known Sensor Reconnector
Scheduled Sensor Poller
Sensor Export/Sync
Provider Update Checker
```

These services SHALL use framework BLE/sensor capabilities and SHALL not create independent Bluetooth stacks.

---

## 42. Provider discovery lifecycle

A recommended provider flow is:

```text
advertisement arrives
       |
registry update
       |
cheap manifest match
       |
known advertisement decoder?
   | yes                 | no
   v                     v
decode              connection warranted?
   |                     |
   v                     v
measurement           connect/GATT discover
                         |
                    manifest match
                         |
                    load provider ELF
                         |
                    bind to sensor
                         |
                 read/subscribe/decode
                         |
                     measurement
```

Provider ELFs SHOULD only become resident when needed.

---

## 43. Reconnection

Configured sensors MAY request automatic reconnection.

Reconnection SHALL use bounded exponential or policy-driven backoff and strong identity checks where available.

A sensor that disappears SHALL not cause continuous rapid connection attempts.

If a sensor reappears through advertisement-only measurements, the system SHOULD avoid reconnecting unless GATT access is actually required.

---

## 44. Multiple sensors

The architecture SHALL support many discovered sensors and a bounded subset of active connections.

Conceptually:

```text
50 devices observed
15 recognized sensors
8 configured for recording
3 require persistent connection
5 advertisement-only
```

Discovery count SHALL therefore be independent of active connection count.

When active-connection demand exceeds measured limits, the Connection Manager MAY schedule connection windows/polling rather than failing the entire sensor subsystem.

---

## 45. Sensor capability discovery

Applications SHOULD be able to query semantic capabilities such as:

```text
all sensor.temperature sources
all sensor.humidity sources
measurements from sensor_id X
latest value for channel Y
recording status
sensor availability
```

An application SHALL not need to know whether the value came from:

```text
BLE advertisement
BLE GATT notification
BLE GATT read
future USB sensor
future local hardware sensor
```

This semantic abstraction SHOULD eventually permit a common RiscRTE sensor framework across transports.

---

## 46. Future transport independence

The normalized sensor layer SHOULD be designed so BLE is one transport rather than the permanent definition of a sensor.

Future sources may include:

```text
BLE
USB
I2C
SPI
UART
LoRa
Wi-Fi/network
internal hardware
```

All may eventually publish the same semantic `sensor.measurement` model.

---

## 47. Testing requirements

At minimum, tests SHOULD prove:

1. passive and active scanning discover nearby test devices;
2. advertisement registry updates without unbounded growth;
3. advertisement-only sensor data can be decoded/recorded without connecting;
4. a connectable sensor can be connected and its GATT database discovered;
5. standard characteristics decode correctly;
6. a proprietary provider binds only to its declared match criteria;
7. provider ELF unload/reload does not leave callbacks/subscriptions alive;
8. multiple sensors can be observed simultaneously;
9. multiple supported connections operate within measured resource limits;
10. excess connection demand is scheduled/rejected cleanly;
11. notification data reaches the semantic event bus;
12. read/poll sensors are scheduler-driven rather than busy-looped;
13. disconnect during discovery/read/notification is safe;
14. stale GATT handles are rejected after reconnect;
15. reconnect uses backoff;
16. pairing-required devices surface legitimate user interaction;
17. unauthorized providers cannot access unrelated devices/capabilities;
18. malformed advertisements/GATT values cannot overflow provider/framework buffers;
19. recorder continues after foreground Scanner app exits;
20. recorder batching avoids one SD write per sample;
21. recorder recovers from interrupted writes;
22. SD removal/write failure is surfaced without crashing BLE;
23. queue overflow/backpressure produces explicit drop diagnostics;
24. Wi-Fi coexistence does not corrupt BLE state;
25. sleep/power transitions reclaim or restore BLE resources according to policy;
26. logical sensor identity survives ordinary reconnect where sufficient identity evidence exists;
27. randomized addresses are not incorrectly treated as guaranteed stable identity;
28. unknown proprietary data remains labeled unknown until a provider/schema exists;
29. background recording does not require a resident Scanner application ELF;
30. signed-provider enforcement works under production security policy.

---

## 48. Normative rules

1. **RiscRTE owns the Bluetooth controller, host stack, scan policy, connections, GATT procedures, and security state.**
2. **Applications and provider ELFs do not create independent BLE stacks.**
3. **Advertisement-only sensing is preferred when connection is unnecessary.**
4. **Connection attempts are policy-controlled, bounded, and rate-limited.**
5. **Unknown proprietary bytes are not assigned invented semantics.**
6. **Standard and proprietary decoders produce transport-independent semantic measurements.**
7. **Providers consume bounded BLE capabilities rather than controller internals.**
8. **Opaque generation-aware handles are used across the public ABI.**
9. **Logical sensor identity is distinct from transient BLE connection/address state.**
10. **Pairing/authentication requirements are respected, not bypassed.**
11. **Notifications/subscriptions are reclaimed before provider unload.**
12. **Polling is scheduler-owned and power-aware.**
13. **Sensor recording is a background RiscRTE service independent of foreground UI residency.**
14. **Persistent logging is batched/bounded to protect memory and SD endurance.**
15. **All external BLE payloads are treated as untrusted input.**
16. **Production provider ELFs are authenticated and authorized before execution.**
17. **Discovery population and active connection population are separate concepts.**
18. **The semantic sensor model is designed for future non-BLE transports.**

---

## 49. Implementation sequence

1. introduce a framework-owned Bluetooth Manager using the selected ESP-IDF BLE host stack;
2. implement scan lifecycle and advertisement parsing;
3. implement bounded in-memory device registry and BLE diagnostics;
4. define opaque BLE device/GATT handles;
5. implement connection manager and service/characteristic discovery;
6. implement GATT read and notification/indication subscription primitives;
7. define normalized `sensor.measurement` ABI/events;
8. implement one advertisement-only standard/test sensor decoder;
9. implement one connected GATT sensor decoder;
10. create persistent Sensor Registry with aliases/configuration;
11. implement Sensor Recorder Service with batched append storage;
12. ensure recorder survives Scanner app unload;
13. add scheduler-driven polling and reconnect/backoff;
14. add pairing/bonding UI/event integration;
15. define sensor-provider ELF ABI and declarative manifest matching;
16. implement one proprietary provider as the reference ELF sensor provider;
17. integrate signed-provider authorization;
18. add memory/backpressure/SD-failure instrumentation;
19. validate BLE/Wi-Fi coexistence and power behavior;
20. add BLE Scanner/Inspector application;
21. expand standard sensor profile coverage based on real hardware;
22. generalize the semantic sensor registry so future USB/I2C/UART/LoRa providers can publish through the same sensor layer.

The reference acceptance test is: **RiscRTE scans the local BLE environment, identifies at least one advertisement-only sensor and one connection-oriented GATT sensor, automatically binds the appropriate standard or installed provider, records normalized timestamped measurements to SD while the foreground Scanner application is not resident, reconnects configured sensors after ordinary loss using bounded backoff, and leaves unsupported proprietary devices explicitly classified as unknown rather than inventing their data semantics.**
