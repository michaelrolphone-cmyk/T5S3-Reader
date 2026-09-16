# RiscRTE Stream and Pipe Architecture

## Status

The first byte-stream MVP is documented in [STREAM_PIPE_MVP.md](STREAM_PIPE_MVP.md).
It provides the runtime and adapters needed for later App Store and serial-monitor
migration. The broader requirements below remain the target architecture; this MVP
does not claim the full Section 78 hardware acceptance criteria.

Priority architecture specification for the RiscRTE data plane: typed streams, runtime-owned pipes, transforms, bounded buffering, backpressure, fan-out/fan-in, asynchronous scheduling, resource ownership, and safe communication across independently loadable ELF modules.

This specification is a foundational dependency for later device-registry unification. **Streams and Pipes are Priority 1 in the platform capability roadmap; the Unified Device Registry follows as Priority 2.**

> **Core invariant:** A producer and consumer exchange continuous data through RiscRTE-owned stream handles and pipes. They do not retain raw pointers or callbacks into one another, and neither endpoint owns the lifetime of the other.

---

## 1. Purpose

RiscRTE already contains or is gaining multiple systems that move continuous or potentially large data:

```text
UART
USB serial
USB storage
GNSS
Bluetooth LE
LoRa
files
network sockets
sensor measurements
firmware images
programming/debug protocols
recorders
```

Without a common data plane, each subsystem tends to create its own read/write callbacks, queues, buffering, ownership, cancellation, and error handling.

The Stream/Pipe subsystem SHALL provide one reusable mechanism for moving ordered data between runtime capabilities and independently loadable modules.

---

## 2. Architectural role

RiscRTE SHALL distinguish control-plane operations from data-plane operations.

```text
                    CONTROL PLANE

 Device Registry       Capability Resolver
 Package Manager       Services
 Jobs                  Security
 Resource Manager      Power Manager

                         |
                         | creates / binds / controls
                         v

                      DATA PLANE

 Sources -> Streams -> Pipes -> Transforms -> Streams -> Sinks
```

The control plane decides **what is connected and under what policy**.

The data plane moves **the actual bytes or records**.

---

## 3. Stream and Pipe definitions

### 3.1 Stream

A **Stream** is a runtime object representing an ordered source and/or sink of data.

A stream may be:

```text
READABLE
WRITABLE
BIDIRECTIONAL
```

Examples:

```text
UART RX/TX
USB CDC connection
FTDI serial endpoint
TCP connection
file
GNSS byte source
sensor measurement source
LoRa packet source
firmware input
```

### 3.2 Pipe

A **Pipe** is a RiscRTE-owned connection that moves data from one compatible stream endpoint to another according to explicit buffering, backpressure, transformation, ownership, and close/error policies.

```text
source stream
     |
     v
RiscRTE Pipe
     |
     v
destination stream
```

Neither endpoint needs implementation knowledge of the other.

---

## 4. Priority

This subsystem is **Priority 1** for the next platform capability layer.

The platform implementation order SHALL begin:

```text
Priority 1  Streams and Pipes
Priority 2  Unified Device Registry
Priority 3  Unified Package Manager
Priority 4  Generic Sensor + Recorder
Priority 5  Bus Managers
Priority 6  Jobs / Notifications / Intents
Priority 7  Vault / Crypto
Priority 8  Search / Automation / Communications
```

The reason for placing Streams/Pipes before the Device Registry is that existing UART, USB, files, GNSS, programmer/debugger, networking, and BLE work can immediately converge on the same data-plane contract. The Device Registry can then expose these already-defined stream capabilities instead of inventing transport-specific I/O contracts during registry implementation.

This ordering supersedes any earlier roadmap text that placed the Unified Device Registry before Streams/Pipes.

---

## 5. Goals

The subsystem SHALL:

1. provide one transport-independent data movement abstraction;
2. support byte streams and typed record streams;
3. support unidirectional and bidirectional endpoints;
4. permit runtime-owned connections between independently developed modules;
5. provide bounded buffering;
6. provide explicit backpressure and overflow policies;
7. support zero-copy or low-copy transfer where safe and practical;
8. support transforms between stream types;
9. support fan-out/tee and fan-in/merge topologies;
10. operate asynchronously without requiring one permanent FreeRTOS task per stream;
11. integrate with capability/resource ownership;
12. deterministically reclaim pipes and subscriptions when an ELF unloads;
13. support files, serial, USB, network, sensors, GNSS, BLE, LoRa, programmers, and future transports;
14. expose diagnostics and transfer counters;
15. preserve packet/record boundaries where those boundaries are semantically meaningful;
16. support cancellation, EOF, disconnect, and failure propagation;
17. permit applications to use convenient synchronous-style wrappers without making blocking loops the core runtime implementation.

---

## 6. Non-goals

The initial subsystem SHALL NOT:

- replace the event bus for low-volume state-change notifications;
- force all structured data into raw byte streams;
- promise lossless delivery when a pipe is explicitly configured with a dropping policy;
- permit unbounded queues;
- retain raw callback pointers into unloadable ELFs;
- make every transport seekable;
- hide transport failures behind an infinite retry loop;
- require all streams to be persisted;
- automatically connect arbitrary sources and sinks without capability/security validation.

---

## 7. Stream families

RiscRTE SHALL initially define two primary stream families:

```text
STREAM_BYTES
STREAM_RECORDS
```

Additional specialized stream families SHOULD only be introduced when these two cannot preserve required semantics.

---

## 8. Byte streams

A byte stream represents an ordered sequence of octets.

Appropriate examples:

```text
UART
USB CDC
FTDI/CP210x/CH340 serial
TCP
file contents
firmware images
NMEA source bytes
programmer protocol bytes
```

Conceptual ABI:

```c
typedef uint32_t riscrte_stream_t;

typedef enum {
    RISCRTE_STREAM_BYTES = 1,
    RISCRTE_STREAM_RECORDS = 2,
} riscrte_stream_kind_t;
```

The final ABI SHALL be versioned before implementation.

---

## 9. Record streams

A record stream preserves message boundaries and schema/type identity.

Examples:

```text
sensor.measurement.v1
location.fix.v1
network.datagram.v1
lora.packet.v1
bluetooth.notification.v1
log.record.v1
```

A record is delivered atomically from the stream's semantic perspective even if its physical transport requires multiple underlying buffers/transfers.

Record streams SHALL prevent consumers from accidentally treating structured records as an arbitrary byte sequence without an explicit transform.

---

## 10. Schema identity

Record streams SHALL expose a stable versioned schema identifier.

Examples:

```text
sensor.measurement.v1
location.fix.v1
network.udp.datagram.v1
lora.packet.v1
```

Pipe creation SHALL reject incompatible schemas unless an explicit compatible transform is inserted.

Example invalid connection:

```text
firmware.bytes
      X
sensor-recorder<sensor.measurement.v1>
```

Example valid connection:

```text
sensor.measurement.v1
        |
        v
Sensor Recorder
```

---

## 11. Stream metadata

Every stream SHOULD expose immutable or framework-controlled metadata including:

```text
stream ID
stream kind
direction
schema/type
source identity
owner execution context
readability/writability
seekability where applicable
maximum record size where applicable
preferred transfer size
buffer constraints
backpressure capabilities
close/disconnect semantics
security/capability requirements
```

Transport-specific metadata SHALL remain behind transport/provider APIs unless explicitly standardized.

---

## 12. Opaque handles

Applications, drivers, services, and providers SHALL use opaque generation-aware stream handles.

A stale handle SHALL fail after stream destruction/recreation.

No public ABI SHALL expose internal queue pointers, FreeRTOS handles, transport structures, or direct pointers to another ELF.

---

## 13. Capability integration

Streams SHOULD normally be obtained through a capability or resource operation.

Conceptually:

```text
resolve capability
      |
      v
open stream
      |
      v
opaque stream handle
```

Example:

```text
serial.host
   |
open bidirectional stream
   |
stream.bytes
```

The application does not need to know whether the implementation is:

```text
native UART
USB CDC
FTDI
CP210x
CH340
BLE serial provider
future transport
```

---

## 14. Serial example

```text
USB FTDI driver
       |
       v
 serial.host capability
       |
       v
 byte stream
       |
       +------------------> Terminal
       |
       +------------------> Logger
```

The FTDI driver knows nothing about Terminal or Logger.

---

## 15. Programmer example

Programming protocols SHOULD consume generic streams rather than concrete USB adapter implementations.

```text
FTDI / CP210x / CDC / UART
             |
      serial byte stream
             |
             v
     ESP ROM Provider
             |
             v
         target MCU
```

Where required, control-line operations such as DTR/RTS SHOULD be separate capabilities associated with the same logical serial endpoint rather than encoded as magic bytes in the stream.

This permits the ESP ROM protocol implementation to remain independent of FTDI/CP210x/CDC transport selection.

---

## 16. File streams

Files SHOULD be exposable as seekable byte streams.

Example:

```text
SD firmware file ----+
                     |
USB volume file -----+--> firmware byte stream --> programmer
                     |
network download ----+
```

The programmer SHOULD consume the same input contract regardless of firmware origin.

Large files SHALL be streamed in bounded windows rather than loaded entirely into RAM.

---

## 17. Network streams

Network mappings SHOULD preserve protocol semantics.

Recommended mapping:

```text
TCP          -> bidirectional byte stream
UDP          -> datagram record stream
WebSocket    -> message record stream or explicit byte mode
HTTP body    -> byte stream
```

Datagram boundaries SHALL not be destroyed merely to make UDP resemble TCP.

---

## 18. BLE streams

The BLE architecture may expose suitable data as record streams.

Examples:

```text
BLE notification -> bluetooth.notification.v1
BLE indication   -> bluetooth.notification.v1
semantic sensor  -> sensor.measurement.v1
```

Raw GATT procedures remain controlled by the Bluetooth Manager; providers may transform BLE records into semantic sensor records.

---

## 19. LoRa streams

LoRa packet reception SHOULD normally be represented as a record stream because packet boundaries and metadata are meaningful.

A record may include:

```text
payload
RSSI
SNR
frequency/channel metadata
timestamp
source identity where protocol supplies it
```

A decoder provider can transform this into a higher-level telemetry schema.

---

## 20. GNSS streams

GNSS demonstrates the relationship between byte and record streams:

```text
UART / USB GNSS
       |
       v
    bytes
       |
       v
 NMEA Decoder
       |
       v
location.fix.v1 records
       |
       +--> Location Service
       +--> Recorder
       +--> Application
```

The GNSS transport driver and location consumers do not depend directly on one another.

---

## 21. Pipe creation

Pipe creation SHALL be a framework operation.

Conceptually:

```c
riscrte_err_t riscrte_pipe_connect(
    riscrte_stream_t source,
    riscrte_stream_t destination,
    const riscrte_pipe_options_v1 *options,
    riscrte_pipe_t *out_pipe);
```

The runtime SHALL validate:

```text
source readability
destination writability
stream kind/schema compatibility
permissions/capabilities
ownership policy
buffer requirements
backpressure compatibility
resource availability
```

before activating the pipe.

---

## 22. Pipe ownership

Every pipe SHALL have a framework-visible owner.

Possible owners include:

```text
application execution context
service logical instance
job
system subsystem
```

When the owner terminates, RiscRTE SHALL apply the pipe's lifecycle policy and reclaim resources deterministically.

---

## 23. Endpoint ownership

Pipe ownership does not imply ownership of both endpoint streams.

Example:

```text
USB serial stream
  owned by USB/serial provider

Terminal pipe
  owned by Terminal application

Terminal sink
  owned by Terminal application
```

When Terminal exits, its pipe/sink disappear while the serial device may remain available for another consumer.

---

## 24. Asynchronous scheduler model

The core implementation SHOULD be event-driven.

```text
producer signals data/space/event
          |
          v
RiscRTE stream scheduler
          |
          v
move bounded work unit
          |
          v
wake/schedule destination only as needed
```

The runtime SHALL avoid requiring one permanent FreeRTOS task per stream or pipe.

Dedicated tasks MAY exist for hardware/stack reasons but are not part of the public stream contract.

---

## 25. Bounded work

A stream scheduling turn SHALL have explicit bounds such as:

```text
maximum bytes
maximum records
maximum wall/CPU time
maximum downstream operations
```

This prevents one high-throughput stream from monopolizing the runtime.

---

## 26. Buffer model

Streams and pipes SHALL use bounded buffers.

A buffer configuration SHOULD include:

```text
capacity
preferred chunk size
maximum record size
watermarks
allocation tier
backpressure policy
```

No stream may grow an unbounded vector/queue in SRAM or PSRAM.

---

## 27. Memory tiers

Buffer placement SHALL integrate with `MEMORY_ARCHITECTURE.md`.

Typical policy:

```text
small latency-sensitive descriptors -> internal SRAM
larger transient buffers            -> PSRAM where suitable
cold/large persistent data          -> SD/storage-backed path
```

DMA requirements may require specific memory capabilities and SHALL be represented by the endpoint/provider.

---

## 28. Zero-copy and low-copy transfer

The implementation SHOULD minimize copying where ownership and memory constraints allow.

A producer may loan a framework-owned buffer segment to a downstream pipe.

Conceptually:

```text
producer fills buffer
       |
       v
framework transfers ownership/reference
       |
       v
consumer processes
       |
       v
buffer returned to pool
```

A zero-copy optimization SHALL NOT expose arbitrary pointers across unloadable ELF lifetimes without framework ownership and validity rules.

Correct lifecycle semantics take priority over eliminating one copy.

---

## 29. Firmware streaming example

Large firmware SHALL not be loaded as one allocation.

```text
SD file stream
      |
 32 KiB bounded window
      |
      v
ESP protocol transform
      |
 4 KiB protocol frames
      |
      v
serial stream
```

The exact sizes are provider/policy choices, not ABI guarantees.

---

## 30. Backpressure

Backpressure is mandatory.

Each pipe SHALL define behavior when the destination cannot accept data.

Initial policies SHOULD include:

```text
BLOCK_PRODUCER
DROP_OLDEST
DROP_NEWEST
KEEP_LATEST
FAIL_PIPE
```

Additional policy types MAY be introduced for record aggregation or transport-specific flow control.

---

## 31. Lossless data

Lossless workflows such as firmware programming and file copying SHALL normally use a blocking/flow-controlled policy.

```text
firmware file -> programmer
policy: BLOCK_PRODUCER
```

Dropping data in such a pipe would invalidate the operation and SHALL not occur silently.

---

## 32. Loss-tolerant data

Some real-time measurements may explicitly permit dropping.

Example:

```text
temperature display
policy: KEEP_LATEST
```

If the display cannot keep up, rendering the latest measurement may be more useful than replaying stale values.

Dropped data SHALL remain observable through counters/diagnostics.

---

## 33. Flow control

When the underlying transport supports flow control, the stream provider SHOULD expose/use it through the framework.

Examples include:

```text
UART RTS/CTS
TCP receive window
protocol-specific pause/resume
subscription rate controls where supported
```

Backpressure SHOULD propagate toward the producer rather than merely accumulating buffers when possible.

---

## 34. Watermarks

Buffers SHOULD support low/high watermark notifications internally.

These can be used to:

```text
resume producer
pause producer
schedule consumer
flush recorder batch
emit diagnostic pressure event
```

Watermarks SHALL not create unbounded callback chains into ELF code.

---

## 35. Transforms

A transform consumes one stream and produces another.

```text
input stream
     |
     v
 Transform
     |
     v
output stream
```

Transforms may be core components or independently installable providers.

Examples:

```text
NMEA decoder
SLIP encoder/decoder
packet parser
compression/decompression
checksum/hash
text line splitter
JSON/CBOR decoder where appropriate
rate limiter
filter
protocol framing
sensor decoder
```

---

## 36. Transform typing

Transforms SHALL declare accepted input and produced output types without loading executable code where possible.

Example manifest metadata:

```text
consumes: stream.bytes
provides: location.fix.v1
```

or:

```text
consumes: lora.packet.v1
provides: telemetry.vendor_x.v1
```

This allows the capability/package system to resolve valid pipelines declaratively.

---

## 37. Transform lifecycle

A transform ELF may be unloaded when no active pipe requires it.

Before unload, RiscRTE SHALL:

```text
stop scheduling transform work
resolve/cancel in-flight operations
close/rebind dependent pipes according to policy
reclaim framework buffers
remove subscriptions
release capability/resource leases
```

No stream scheduler entry may call code in an unloaded transform.

---

## 38. Tee / fan-out

RiscRTE SHOULD support one source feeding multiple destinations.

```text
                    +--> Terminal
Serial RX --> Tee --+--> Logger
                    +--> Analyzer
```

Each branch SHALL have independent buffering/backpressure policy.

A slow optional logger SHALL not necessarily stall an interactive Terminal branch unless the topology explicitly requires coupled lossless delivery.

---

## 39. Fan-out policy

A tee SHALL define whether branches are:

```text
COUPLED
INDEPENDENT
```

`COUPLED` means source progress waits for all lossless branches.

`INDEPENDENT` permits each branch to apply its own overflow policy.

The default SHALL depend on stream semantics rather than silently assuming one behavior.

---

## 40. Merge / fan-in

RiscRTE SHOULD support compatible record streams feeding one destination.

```text
Temperature --+
Humidity ------+--> Sensor Recorder
Pressure ------+
```

Merged records SHALL retain source identity and ordering metadata.

A merge SHALL define ordering semantics explicitly.

---

## 41. Ordering

A single stream SHALL preserve source order unless a transform explicitly declares otherwise.

Fan-in across multiple sources cannot guarantee a physically meaningful global order without timestamps/sequence metadata.

The merge layer SHOULD preserve:

```text
per-source order
arrival order
source timestamp where present
runtime acquisition timestamp
```

Consumers can then apply stronger temporal ordering when required.

---

## 42. Bidirectional streams

Some resources naturally require bidirectional communication.

Examples:

```text
UART
USB CDC
TCP
programming/debug protocols
```

A bidirectional stream SHALL conceptually contain independent RX and TX flow-control paths even when exposed under one logical handle.

A stalled TX path SHALL not corrupt RX buffering semantics.

---

## 43. Duplex pipe topology

A bidirectional connection may be modeled internally as two linked directional pipes:

```text
Endpoint A TX -----> Endpoint B RX
Endpoint A RX <----- Endpoint B TX
```

This permits independent buffer and backpressure behavior in each direction while exposing a convenient duplex handle to clients.

---

## 44. Seekable streams

File-like streams MAY support:

```text
seek
tell
size
range/substream
```

Serial/network/live sensor streams generally SHALL NOT.

Consumers SHALL query stream capabilities rather than assume seekability.

Firmware programmers that require retries SHOULD use seekable input or an explicitly replayable buffering layer.

---

## 45. Substreams and ranges

Seekable byte streams SHOULD support bounded range views where useful.

Example:

```text
firmware.bin
   |
range(offset, length)
   |
partition image stream
```

A substream SHALL not permit access outside its declared range.

This is useful for multi-image firmware containers and bounded parser access.

---

## 46. EOF and close semantics

Streams SHALL distinguish:

```text
clean EOF
local close
remote disconnect
cancel
provider removal
I/O failure
protocol failure
permission revocation
```

Consumers SHALL not be forced to infer all termination conditions from a zero-byte read.

---

## 47. Error propagation

A pipe SHALL define how source/destination failures propagate.

Possible policies:

```text
close entire pipe
close failed branch only
pause and await reconnect
fail owning job
notify owner and retain source
```

Transport-specific automatic reconnect belongs to the transport/provider policy, not generic pipe magic.

---

## 48. Cancellation

Long-running streams/pipes SHALL be cancellable through framework ownership.

Cancellation SHALL:

```text
stop new scheduling
signal endpoints as appropriate
resolve in-flight bounded work
release buffers
release resource leases
emit final state
```

Cancellation SHALL not leave a hardware transfer callback targeting an unloaded ELF.

---

## 49. Pipe states

Suggested lifecycle:

```text
CREATED
VALIDATING
READY
RUNNING
BACKPRESSURED
DRAINING
PAUSED
CANCELLING
CLOSED
FAILED
```

Not every state needs to be public ABI, but diagnostics SHOULD expose equivalent information.

---

## 50. Stream states

Suggested stream states:

```text
OPENING
OPEN
EOF
DISCONNECTED
CLOSING
CLOSED
FAILED
```

Live/reconnectable providers MAY distinguish temporarily disconnected state from permanent closure.

---

## 51. Event bus relationship

Streams SHALL NOT replace the event bus.

Use events for low-volume state changes:

```text
device.connected
storage.removed
job.completed
sensor.available
```

Use streams for sustained/ordered data:

```text
serial bytes
file contents
sensor samples
network payloads
GNSS records
```

An event may announce that a stream became available, but should not carry megabytes of stream data.

---

## 52. Job relationship

Jobs control long-running operations; streams move their data.

Example:

```text
Firmware Programming Job
       |
       +--> input firmware stream
       +--> serial/programming stream
       +--> progress events
       +--> cancellation
```

The Job owns operation lifecycle while the streams remain generic reusable data endpoints.

---

## 53. Service relationship

A logical service may own persistent pipe configuration while its implementation ELF is loaded only when work is required.

Example:

```text
Sensor Recorder service
        |
subscription / pipe policy
        |
sensor.measurement stream
```

The runtime may wake/load the recorder implementation when records require processing and unload it when quiescent, subject to buffering/durability policy.

---

## 54. ELF safety

Direct cross-module callbacks SHALL NOT be the persistent data-plane mechanism.

Forbidden architecture:

```text
Driver stores pointer to App ELF callback
```

Required architecture:

```text
Driver
  |
RiscRTE Stream
  |
RiscRTE Pipe
  |
Application
```

All intermediary state required to prevent dangling calls SHALL be runtime-owned.

---

## 55. Module unload example

If Terminal consumes a USB serial stream and Terminal unloads:

```text
Terminal exits
     |
RiscRTE revokes Terminal-owned sink/pipe
     |
pending bounded work resolves/cancels
     |
buffers returned
     |
USB serial provider remains available
```

The USB provider SHALL NOT retain a Terminal callback.

---

## 56. Stream API shape

The final ABI should remain small and opaque.

Conceptually:

```c
riscrte_err_t riscrte_stream_open(...);
riscrte_err_t riscrte_stream_close(riscrte_stream_t stream);
riscrte_err_t riscrte_stream_get_info(riscrte_stream_t stream, ...);
riscrte_err_t riscrte_stream_read(...);
riscrte_err_t riscrte_stream_write(...);
riscrte_err_t riscrte_stream_seek(...);
riscrte_err_t riscrte_pipe_connect(...);
riscrte_err_t riscrte_pipe_pause(...);
riscrte_err_t riscrte_pipe_resume(...);
riscrte_err_t riscrte_pipe_cancel(...);
riscrte_err_t riscrte_pipe_get_stats(...);
```

These names are illustrative. ABI design SHALL be reviewed against existing compatibility conventions before implementation.

---

## 57. Synchronous convenience API

Applications MAY receive synchronous-style convenience operations such as bounded `read`/`write` calls.

These SHALL be implemented above the runtime's asynchronous ownership/scheduling model.

A blocking convenience call SHALL have cancellation/timeout semantics and SHALL NOT require a permanent application-owned polling task.

---

## 58. Stream scheduler

The runtime SHOULD maintain a central or coordinated stream scheduler responsible for runnable pipe work.

It SHOULD prioritize using factors such as:

```text
foreground latency
lossless backpressure
hardware FIFO pressure
job priority
service priority
power policy
buffer watermarks
fairness
```

Scheduling policy SHALL prevent starvation.

---

## 59. Interrupt and callback boundary

Hardware ISR/stack callbacks SHALL do the minimum necessary work and transfer ownership into runtime-managed buffers/queues.

Heavy transforms and ELF execution SHALL not occur directly in ISR context.

The scheduler SHALL provide the execution boundary between hardware completion and module processing.

---

## 60. Buffer pools

RiscRTE SHOULD use reusable bounded buffer pools for common stream transfer sizes where beneficial.

Pools SHOULD be categorized by memory requirements such as:

```text
internal SRAM
DMA-capable
PSRAM
small record descriptor
large transfer block
```

Pool exhaustion SHALL invoke backpressure/failure policy rather than an uncontrolled allocation spiral.

---

## 61. Security

Pipe creation is a privileged resource operation governed by capability authorization.

A signed module SHALL not automatically gain access to every stream.

Examples of sensitive streams:

```text
raw keyboard/HID input
programming/debug stream
credential-bearing network connection
private file
raw BLE device
system logs
```

The runtime SHALL validate access at stream open and pipe binding time.

---

## 62. Data provenance

Record streams SHOULD preserve source identity where meaningful.

This is particularly important for:

```text
sensor measurements
merged streams
LoRa packets
location observations
system logs
```

A transform SHOULD preserve or explicitly replace provenance metadata according to schema rules.

---

## 63. Diagnostics

RiscRTE diagnostics SHOULD expose for each stream:

```text
stream ID
kind/schema
owner
source/device/provider
state
readers/writers
buffer capacity/usage
bytes or records produced
bytes or records consumed
dropped bytes/records
high-water mark
stall/backpressure time
last activity
last error
```

For each pipe:

```text
pipe ID
owner
source stream
destination stream
transform chain
state
backpressure policy
buffer usage
throughput
drops
stall time
created time
last error
```

---

## 64. Topology diagnostics

The diagnostics UI SHOULD be able to render an active data path.

Example:

```text
USB FTDI
  stream #41 bytes
      |
      +--> pipe #17 --> Terminal
      |
      +--> pipe #18 --> Logger
```

Another:

```text
UART GNSS
  stream #52 bytes
      |
  NMEA transform
      |
  stream #53 location.fix.v1
      |
      +--> Location Service
      +--> Recorder
```

This should make ownership and bottlenecks directly inspectable.

---

## 65. Performance counters

The subsystem SHOULD expose aggregate counters including:

```text
active streams
active pipes
buffer memory by tier
scheduler work rate
bytes/sec
records/sec
dropped data
backpressured pipes
pool exhaustion
average/max scheduling latency
```

These counters are important for tuning ESP32-S3 SRAM/PSRAM usage and radio/storage workloads.

---

## 66. Persistence

Live stream handles SHALL generally not be persisted across reboot.

Persistent configuration SHOULD store logical intent instead:

```text
source capability/device identity
transform/provider identity
destination service/capability
pipe policy
```

At boot/service restoration, RiscRTE resolves fresh handles and reconstructs the topology.

Raw runtime handles SHALL never be treated as durable identity.

---

## 67. Reconnectable topology

A persistent logical pipeline MAY survive temporary endpoint disappearance.

Example:

```text
known BLE sensor
      |
 temporarily unavailable
      |
logical recording subscription remains
      |
sensor returns
      |
new stream handle resolved
      |
pipe rebuilt
```

The Device/Capability layer owns rediscovery. The Stream layer owns safe connection/reconnection of resolved endpoints.

---

## 68. Device Registry relationship

The Unified Device Registry SHALL build on the stream abstraction rather than define transport-specific I/O contracts.

A device may expose:

```text
capabilities:
  serial.host
  sensor.temperature
  debug.swd

streams:
  serial.rx_tx -> STREAM_BYTES
  measurements -> STREAM_RECORDS<sensor.measurement.v1>
```

This is why Streams/Pipes precede Device Registry implementation.

---

## 69. Package Manager relationship

Packages may declare stream capabilities and transform compatibility in manifests.

Example:

```text
module: nmea-decoder
kind: provider
consumes:
  stream.bytes
provides:
  stream.records: location.fix.v1
```

Package resolution can therefore identify providers without executing every installed ELF.

---

## 70. Example: serial terminal

```text
USB device discovered
       |
serial provider binds
       |
stream.bytes created
       |
Terminal requests serial capability
       |
RiscRTE creates pipe
       |
Terminal renders bytes
```

Terminal exit destroys its pipe but does not necessarily disconnect/unload the serial provider.

---

## 71. Example: serial logger and terminal

```text
                 +--> Terminal [KEEP_LATEST/display policy]
Serial RX -> Tee |
                 +--> File Logger [BLOCK or bounded-loss policy]
```

The branch policies SHALL be explicit.

---

## 72. Example: GNSS

```text
UART driver
   |
bytes
   |
NMEA provider ELF
   |
location.fix.v1
   |
   +--> Location Service
   +--> Track Recorder
   +--> Navigation App
```

No consumer receives a pointer into the NMEA provider ELF.

---

## 73. Example: firmware programmer

```text
SD firmware file
       |
byte stream
       |
ESP image parser/segment stream
       |
ESP ROM protocol provider
       |
serial byte stream
       |
USB serial provider
       |
target MCU
```

Progress belongs to the programming Job; payload movement belongs to Streams/Pipes.

---

## 74. Example: BLE sensor

```text
BLE Manager
   |
notification records
   |
vendor sensor provider ELF
   |
sensor.measurement.v1
   |
   +--> Recorder Service
   +--> Sensors App
```

The provider can unload when no matching device/pipeline requires it.

---

## 75. Example: network download to programmer

```text
HTTPS response body
       |
byte stream
       |
optional hash transform
       |
verified firmware stream/file
       |
programmer
```

The programmer does not need an HTTP client implementation.

---

## 76. Testing requirements

At minimum, implementation tests SHALL demonstrate:

1. creation/destruction of byte streams;
2. creation/destruction of typed record streams;
3. incompatible stream types are rejected;
4. a pipe transfers bounded data correctly;
5. byte ordering is preserved;
6. record boundaries are preserved;
7. all configured backpressure policies behave as specified;
8. lossless pipes do not silently drop data;
9. dropping pipes expose drop counters;
10. buffer memory remains bounded under stalled consumers;
11. producer removal safely closes/fails dependent pipes;
12. consumer ELF unload leaves no dangling callbacks;
13. provider/transform ELF unload leaves no scheduler references;
14. tee supports independent branches;
15. coupled tee preserves required lossless behavior;
16. merge preserves per-source ordering and source identity;
17. duplex RX/TX can progress independently;
18. file streams support bounded sequential reads;
19. seekable streams correctly enforce ranges;
20. non-seekable streams reject seek operations;
21. cancellation releases buffers/resources;
22. timeout/error state is observable;
23. stream scheduler remains fair under multiple active streams;
24. high-rate source cannot cause unbounded memory growth;
25. buffer pool exhaustion produces controlled backpressure/failure;
26. USB serial can pipe to Terminal;
27. USB serial can tee to Terminal and Logger;
28. GNSS bytes can transform into location records;
29. a file stream can feed the programmer without loading the complete image;
30. BLE sensor records can feed the Recorder Service;
31. diagnostics can enumerate active streams/pipes and ownership;
32. persistent logical pipe configuration reconstructs with fresh handles;
33. stale generation handles are rejected;
34. capability revocation closes affected streams safely;
35. stream security prevents unauthorized endpoint binding.

---

## 77. Initial implementation sequence

The implementation SHOULD proceed in this order:

1. define versioned opaque stream and pipe handles;
2. implement framework stream registry and ownership tracking;
3. implement byte-stream endpoints;
4. implement bounded ring/block buffer primitives;
5. implement asynchronous stream scheduler;
6. implement pipe connect/close/cancel;
7. implement `BLOCK_PRODUCER`, `DROP_OLDEST`, `DROP_NEWEST`, `KEEP_LATEST`, and `FAIL_PIPE`;
8. expose stream/pipe diagnostics and counters;
9. convert one existing serial path to a byte stream;
10. pipe serial input to the existing USB Serial/Terminal-style application;
11. implement file byte streams;
12. stream a firmware image from SD into the programmer path without full-image allocation;
13. implement record streams and schema identity;
14. implement one byte-to-record transform using GNSS/NMEA;
15. implement tee/fan-out;
16. implement merge/fan-in for compatible record streams;
17. implement duplex stream convenience APIs;
18. add capability-based stream open/binding;
19. convert BLE sensor measurement output to typed record streams;
20. integrate recorder consumption;
21. add persistent logical topology reconstruction;
22. integrate provider manifests for transform typing;
23. then begin Unified Device Registry implementation on top of the established stream/capability contracts.

---

## 78. Reference acceptance test

The Priority-1 Stream/Pipe implementation is considered established when the same runtime primitives can demonstrate all of the following on real hardware:

```text
USB serial -> Terminal
USB serial -> Terminal + Logger
SD firmware file -> Programmer
UART GNSS bytes -> NMEA transform -> location.fix records
BLE sensor records -> Recorder
```

while satisfying these conditions:

```text
bounded memory
explicit backpressure
no full firmware-image allocation
no dangling cross-ELF callbacks
safe application/provider unload
observable ownership
observable drop/stall counters
capability-authorized binding
```

At that point the Unified Device Registry SHOULD be implemented as Priority 2 and expose device data paths using these established stream contracts.

---

## 79. Normative rules

1. **Streams are runtime-owned opaque data endpoints.**
2. **Pipes are runtime-owned connections between compatible endpoints.**
3. **Streams/Pipes are Priority 1, ahead of the Unified Device Registry.**
4. **Byte and typed record streams are distinct first-class stream families.**
5. **Record boundaries and schemas are preserved unless an explicit transform changes them.**
6. **All buffering is bounded.**
7. **Every pipe has an explicit backpressure/overflow policy.**
8. **Lossless pipes never silently discard data.**
9. **Dropped data is observable.**
10. **The runtime owns cross-ELF lifecycle boundaries; persistent raw callbacks between ELFs are prohibited.**
11. **Streams and pipes have explicit owners and deterministic reclamation.**
12. **The core implementation is asynchronous/event-driven; permanent per-stream polling tasks are not the architectural model.**
13. **Zero-copy optimization never overrides memory/lifecycle safety.**
14. **Transforms are typed and independently loadable where appropriate.**
15. **Tee/fan-out and merge/fan-in preserve explicit branch/order semantics.**
16. **Events carry state changes; streams carry sustained ordered data.**
17. **Jobs own long-running operation lifecycle; streams move job payloads.**
18. **Files, serial, networking, BLE, GNSS, LoRa, sensors, and programmers converge on the same data-plane primitives where their semantics are compatible.**
19. **Persistent configuration stores logical topology, never raw runtime handles.**
20. **The Device Registry is built on top of stream/capability contracts rather than defining its own transport-specific I/O APIs.**
