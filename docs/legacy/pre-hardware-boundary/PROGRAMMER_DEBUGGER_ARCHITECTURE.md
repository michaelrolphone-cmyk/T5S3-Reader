# T5S3 Multi-Target Programmer and Debugger Architecture

## Status

Architecture specification for using T5S3 as a standalone firmware programmer/debugger controlling multiple simultaneously attached programming/debug interfaces through a USB hub.

This design intentionally separates **USB device transport**, **debug/programming transport**, **target protocol**, **firmware image handling**, and **job orchestration** so one programmer implementation can reuse multiple adapters and one adapter can support multiple target families where applicable.

> **Core invariant:** A USB adapter is not a target programmer. T5 discovers and binds transport providers, identifies target/protocol capabilities, and executes explicit programming/debug jobs against stable device handles. Multiple attached probes remain independently addressable throughout the workflow.

---

## 1. Target use case

A representative bench/field topology is:

```text
                         T5S3
                           |
                    USB OTG Host
                           |
                   Powered USB Hub
          +----------------+----------------+
          |                |                |
        Port 1           Port 2           Port 3
          |                |                |
      FTDI/UART          ST probe          MSP probe
          |                |                |
      ESP target        STM32 target       MSP target
```

All three programming devices are connected and enumerated at the same time.

T5 executes an ordered job such as:

```text
1. program ESP target through FTDI
2. verify ESP target
3. program/verify STM32 through ST transport
4. program/verify MSP target through MSP transport
5. produce aggregate result
```

The design SHALL also permit parallelism in the future where hardware, power, memory, USB bandwidth, and protocol implementations make it safe, but **sequential execution is the required initial orchestration model**.

---

## 2. Terminology

This specification uses:

```text
USB provider
    Driver/provider for an attached USB adapter/probe.

Transport
    Generic byte/debug/programming transport exposed above the USB provider.

Target protocol
    Family-specific protocol that understands how to identify, erase,
    program, verify, reset, and optionally debug a target MCU.

Programmer
    Higher-level component combining target protocol + transport.

Job
    One explicit operation against one selected target/probe.

Batch
    Ordered set of jobs, potentially against multiple simultaneously
    attached probes/targets.
```

FTDI refers to a USB-to-serial transport family, not an ESP32 programming protocol.

ST and MSP support may involve vendor-specific debug probes, bootloader transports, JTAG/SWD/SBW, or other mechanisms. Each concrete implementation SHALL declare what it actually supports rather than treating vendor name as a single protocol.

---

## 3. Layered architecture

```text
+------------------------------------------------------+
|               Programmer Application                 |
| device list / firmware selection / jobs / results    |
+--------------------------+---------------------------+
                           |
                           v
+------------------------------------------------------+
|             Programming Job Manager                  |
| discovery | target binding | queue | sequencing      |
| retries   | progress       | verify | cancellation   |
+--------------------------+---------------------------+
                           |
                           v
+------------------------------------------------------+
|              Target Protocol Providers               |
|                                                      |
| Espressif ROM | STM32 | MSP | future families        |
+--------------------------+---------------------------+
                           |
                           v
+------------------------------------------------------+
|               Transport Capabilities                 |
|                                                      |
| serial | SWD | JTAG | SBW | DFU | vendor debug      |
+--------------------------+---------------------------+
                           |
                           v
+------------------------------------------------------+
|                USB Host Providers                    |
|                                                      |
| FTDI | CP210x | CH34x | ST-LINK | MSP/FET | ...      |
+--------------------------+---------------------------+
                           |
                           v
+------------------------------------------------------+
|            T5 USB Core / Hub Topology                |
+--------------------------+---------------------------+
                           |
                           v
                      USB Hardware
```

Each layer SHALL depend on the abstraction immediately below it rather than hard-coded filesystem paths or implementation-specific pointers.

---

## 4. Relationship to USB architecture

This specification depends on `USB_OTG_HOST_ARCHITECTURE.md`.

The programmer SHALL NOT implement its own USB enumeration, hub handling, endpoint ownership, or hot-plug model.

The USB subsystem supplies stable opaque handles and bound capabilities for all simultaneously attached programming devices.

---

## 5. Simultaneous device discovery

With multiple devices attached, discovery SHALL return a collection rather than a singleton.

Example logical inventory:

```text
Device A
  hub path: 1.1
  VID/PID: FTDI
  serial: FT9ABC123
  provider: usb-host-ftdi
  capabilities:
      serial.host
      serial.control_lines

Device B
  hub path: 1.2
  VID/PID: ST probe
  serial: 066D...
  provider: usb-host-stlink
  capabilities:
      debug.swd
      program.stm32

Device C
  hub path: 1.3
  VID/PID: MSP probe
  serial: ...
  provider: usb-host-msp-debug
  capabilities:
      debug.msp
      program.msp
```

Jobs SHALL bind to a specific opaque device/transport handle, not "the USB serial device" or "the debugger."

---

## 6. Device identity and binding

A programming configuration SHOULD identify a probe using the strongest available identity:

1. USB serial number;
2. explicit persistent user alias mapped to serial number;
3. VID/PID + topology path as a weaker fallback;
4. explicit interactive selection when identity is ambiguous.

USB address SHALL NOT be used as durable identity because addresses change across enumeration.

Example aliases:

```text
ESP_PROGRAMMER   -> FT9ABC123
STM32_PROGRAMMER -> 066DFF...
MSP_PROGRAMMER   -> MSPFET...
```

---

## 7. Transport capability model

Target programmers SHALL consume generic transports.

Candidate capabilities:

```text
serial.host
serial.control_lines
usb.dfu
debug.swd
debug.jtag
debug.sbw
debug.vendor.stlink
debug.vendor.msp
program.transport
```

A transport handle SHALL be bound to exactly one physical interface/probe for a job.

The Job Manager SHALL enforce exclusive ownership where a programming/debug protocol cannot safely be shared.

---

## 8. FTDI / ESP programming path

A common ESP target path is:

```text
T5 USB host
     |
FTDI provider
     |
serial.host + RTS/DTR control
     |
Espressif target protocol
     |
ROM bootloader
     |
ESP target flash
```

The Espressif programmer provider is responsible for target semantics such as:

- bootloader synchronization;
- chip identification;
- boot-mode/reset sequencing when supported;
- flash geometry/parameters;
- erase;
- write;
- verify;
- reset/run;
- optional higher-speed/stub behavior if implemented.

The FTDI provider is responsible only for the USB serial/control-line transport.

The same Espressif protocol SHOULD be reusable with CP210x/CH34x/native UART when equivalent transport capabilities exist.

---

## 9. ST target programming paths

ST/STM32 support SHALL be represented as concrete transports/protocols, for example:

```text
STM32 ROM UART bootloader
STM32 USB DFU
ST-LINK SWD
ST-LINK JTAG where supported
```

A target protocol provider SHALL declare which target families/operations it supports.

The architecture SHALL NOT assume that all STM32 parts expose the same bootloader transport or memory layout.

Target identification SHALL precede destructive programming unless the user explicitly supplies a verified fixed target profile.

---

## 10. MSP target programming paths

MSP support SHALL similarly distinguish concrete families/transports, for example MSP430/MSP432-class targets and the actual attached debug/programming probe.

Potential transport capabilities may include:

```text
debug.sbw
debug.jtag
debug.vendor.msp
```

The provider SHALL encapsulate target-family-specific erase/program/verify/debug sequences.

Vendor/family support SHALL be declared precisely in metadata and UI; `MSP` SHALL NOT be treated as one universal programming protocol.

---

## 11. SWD, JTAG, and vendor probes

SWD/JTAG may be exposed by dedicated USB probes or future direct T5 hardware drivers.

The target layer SHOULD therefore depend on:

```text
debug.swd
```

rather than:

```text
ST-LINK USB implementation
```

when the generic abstraction is sufficient.

Vendor-specific extensions MAY remain available through narrower vendor capabilities when generic SWD/JTAG does not expose required functionality.

---

## 12. Programmer protocol-provider ELF model

Target programming logic SHOULD be independently loadable ELF modules/services/providers.

Examples:

```text
program-esp-rom.elf
program-stm32-rom-uart.elf
program-stm32-dfu.elf
program-stm32-swd.elf
program-msp.elf
```

The exact module classification may be a specialized service/provider class, but programming protocols SHALL NOT own the USB controller.

Provider metadata SHOULD declare:

- supported target families;
- required transport capabilities;
- supported image formats;
- supported operations;
- ABI version;
- resource requirements;
- signer/authorization information.

---

## 13. Programming Job Manager

The Job Manager SHALL be a framework/service-level orchestration component responsible for:

```text
inventory snapshot
probe selection
transport lease acquisition
target identification
firmware validation
job ordering
progress reporting
retry policy
verification
cancellation
cleanup
aggregate batch status
```

Target protocol ELFs execute operations; the Job Manager controls the workflow.

---

## 14. Job state machine

A programming job SHOULD use explicit states:

```text
QUEUED
WAITING_FOR_DEVICE
ACQUIRING
IDENTIFYING
VALIDATING_IMAGE
ENTERING_PROGRAM_MODE
ERASING
PROGRAMMING
VERIFYING
RESETTING
COMPLETE
FAILED
CANCELLED
DEVICE_LOST
```

A job SHALL NOT report success merely because data transfer completed. Required verification policy determines completion.

---

## 15. Batch sequencing

A batch is an ordered list of jobs.

Example:

```text
Batch: production-set-001

Job 1
  probe: ESP_PROGRAMMER
  target: ESP32-S3
  image: /Firmware/esp/firmware.bin
  verify: required

Job 2
  probe: STM32_PROGRAMMER
  target: STM32...
  image: /Firmware/st/firmware.bin
  verify: required

Job 3
  probe: MSP_PROGRAMMER
  target: MSP...
  image: /Firmware/msp/firmware...
  verify: required
```

Initial implementation SHALL execute one destructive programming job at a time while all probes remain connected and enumerated.

After each job releases its transport lease, the next job begins without requiring hub reconnection.

---

## 16. Why sequential first

Sequential execution minimizes:

- PSRAM pressure from multiple protocol ELFs;
- USB bandwidth contention;
- hub power/inrush problems;
- target power interactions;
- complex simultaneous timeout handling;
- flash-image buffer duplication;
- user confusion during initial implementation.

The hub still provides the key operational benefit: all targets remain physically connected and independently addressable.

Parallel programming MAY be added later under an explicit scheduler/resource model.

---

## 17. Future parallel execution

Parallel execution SHALL require independent transport leases and sufficient declared resources.

The scheduler SHALL consider:

```text
USB bandwidth
hub/target power
PSRAM
internal SRAM transfer buffers
CPU
protocol timing sensitivity
storage read bandwidth
probe independence
```

No target protocol may assume it owns global USB state.

---

## 18. Firmware image abstraction

The Job Manager SHALL represent firmware as an image/package abstraction rather than a raw pointer to a fully resident file.

Image sources may include:

```text
SD card
USB thumb drive
internal storage
network-downloaded/staged package
future application bundle
```

Large images SHALL be streamed through bounded buffers according to `MEMORY_ARCHITECTURE.md`.

---

## 19. Image formats

Target providers SHALL explicitly declare supported formats, potentially including:

```text
raw binary
Intel HEX
ELF
UF2
vendor-specific package
partitioned/multi-image manifest
```

The framework MAY provide common parsers, but target-specific address/layout interpretation remains the responsibility of the appropriate provider/profile.

No image SHALL be flashed until its format and address ranges are validated against the selected target/programming profile.

---

## 20. Multi-image programming plans

A target may require multiple images.

Example conceptual ESP plan:

```text
bootloader -> address A
partition table -> address B
application -> address C
```

The job model SHALL therefore support an ordered list of image segments with explicit addresses/regions rather than assuming one file equals one whole flash device.

---

## 21. Target profiles

The platform SHOULD support versioned target profiles containing non-secret declarative metadata such as:

```text
target family
expected identity
flash geometry
allowed address ranges
boot/reset method
required transport
image mapping
verify policy
post-program reset behavior
```

Target profiles SHALL NOT override security policy or permit arbitrary memory operations merely because an SD file requests them.

---

## 22. Target identification

Before erase/program operations, the protocol provider SHOULD identify the connected target and compare it to the job/profile.

Mismatch SHALL stop the job unless explicit expert policy permits otherwise.

The UI/log SHOULD distinguish:

```text
expected target
observed target
selected probe
selected firmware
```

This is essential when three different targets are physically connected simultaneously.

---

## 23. Verification

Verification SHOULD be required by default for programming jobs.

Provider-supported methods may include:

```text
readback compare
hash/checksum from target/probe
protocol-native verify
flash digest
```

The result SHALL state what verification method was actually used.

A successful write without verification SHOULD be reported distinctly when verification is disabled or unavailable.

---

## 24. Debugger architecture

Programming and debugging share transport discovery but have different lifetimes.

```text
Programming
    bounded exclusive job

Debugging
    longer-lived session
```

A debug session SHALL acquire an exclusive lease on its probe/target transport until the session ends.

Potential operations include:

```text
halt
run
reset
read/write memory
read/write registers
breakpoints
watchpoints where supported
flash programming
single step
```

Exact support depends on target/probe provider.

---

## 25. Debug session handles

The framework SHALL represent a debug session using an opaque handle bound to:

```text
probe identity
target identity
transport lease
protocol provider
connection generation
```

If the probe disconnects, the session becomes invalid and operations SHALL fail cleanly.

---

## 26. GDB/server compatibility direction

A future service MAY expose a remote-debug protocol such as a GDB-compatible server over Wi-Fi or USB CDC while T5 owns the physical SWD/JTAG probe.

Conceptually:

```text
Development PC
      |
 Wi-Fi / USB CDC
      |
T5 debug-server service
      |
debug.swd / debug.jtag
      |
USB probe
      |
target MCU
```

This is optional future functionality and SHALL not be required for the initial standalone programmer.

---

## 27. Control-line boot sequencing

Serial programmers may require control lines such as RTS/DTR to enter target bootloader mode.

The serial transport capability SHALL advertise available modem/control lines.

Target protocols SHALL request semantic operations through the transport API rather than manipulating USB adapter internals.

If automatic boot control is unavailable, the Job Manager SHALL support an explicit manual boot/reset prompt/state rather than silently failing synchronization.

---

## 28. Target power

Programming transport and target power are separate resources.

The platform SHALL NOT assume that every USB adapter safely powers its target.

The UI/profile SHOULD indicate whether target power is:

```text
externally supplied
supplied by programming probe
supplied by USB/hub
unknown
```

Where controllable, target power SHALL be a framework-owned capability/lease.

Multi-target operation SHOULD use a powered hub and independently safe target power arrangement.

---

## 29. Hot-unplug during programming

Probe/target loss SHALL transition the active job to `DEVICE_LOST` or `FAILED` and release all remaining framework resources.

The framework SHALL NOT automatically resume a destructive operation on a newly attached device merely because it matches VID/PID.

Resume requires strong identity validation and explicit protocol support.

Other probes attached to the hub SHALL remain usable.

---

## 30. Failure isolation

Failure of one job SHALL not invalidate the entire USB topology.

Example:

```text
ESP job fails synchronization
        |
        +--> FTDI lease released
        +--> failure logged
        +--> batch policy decides stop/continue

ST and MSP probes remain enumerated
```

Batch policy SHALL explicitly define whether later jobs continue after a failure.

---

## 31. Batch failure policies

Supported policies SHOULD include:

```text
STOP_ON_FAILURE
CONTINUE_INDEPENDENT
RETRY_THEN_STOP
RETRY_THEN_CONTINUE
```

Retries SHALL be bounded.

A retry SHALL revalidate that the same intended probe/target is still attached.

---

## 32. Logging and audit

Every programming job SHOULD generate a structured result containing:

```text
timestamp
job/batch ID
probe identity and USB serial
hub path at execution time
transport provider/version
target protocol provider/version
observed target identity
firmware package/image identity
image hash
operation requested
bytes written
verify method/result
start/end duration
warnings/errors
final status
```

Logs SHOULD be persistable to SD and exportable.

---

## 33. Firmware authenticity

The platform SHALL distinguish two security questions:

```text
Is the T5 programmer/provider trusted to execute?
Is the firmware image authorized/trusted for this target?
```

Provider ELF trust follows `SECURITY_ARCHITECTURE.md`.

Firmware-image signing/authorization MAY be policy-configurable. Production workflows SHOULD support signed firmware packages and target/profile restrictions.

Development mode MAY permit arbitrary user-selected images with explicit warnings.

---

## 34. Dangerous operations

Debug/programming capabilities can erase flash, alter option/configuration bytes, change security state, or render a target temporarily/unrecoverably inaccessible.

The API SHALL distinguish ordinary flash programming from higher-risk operations such as:

```text
security fuse/OTP writes
readout-protection changes
option-byte changes
boot configuration
mass erase of protected regions
lock/debug-disable operations
```

High-risk irreversible operations SHALL require separate explicit authorization and SHALL NOT be performed as incidental side effects of ordinary flashing.

---

## 35. Security boundaries

Applications SHOULD not receive unrestricted raw JTAG/SWD/USB control merely because a programmer UI exists.

Capabilities SHOULD be scoped, for example:

```text
program.target
program.esp
program.stm32
program.msp
debug.target
debug.swd
debug.jtag
```

Provider signatures establish code authenticity; system policy establishes authorization.

---

## 36. Resource model

Each active programming/debug job SHALL own:

```text
USB device/interface handle
transport lease
protocol provider instance
firmware image handles
storage-VM mappings/buffers
power locks
timeouts/timers
progress/event subscriptions
optional target-power lease
```

All resources SHALL be reclaimed on completion, cancellation, failure, app exit, or device loss.

---

## 37. Memory behavior

Programming SHALL be streaming-first.

Example:

```text
SD firmware image
       |
       v
storage VM / file stream
       |
       v
bounded image chunk
       |
       v
protocol framing/compression
       |
       v
USB transfer buffer
       |
       v
probe/target
```

The entire firmware image SHALL NOT need to fit in PSRAM.

Protocol ELFs may be unloaded between sequential jobs to free executable PSRAM.

---

## 38. Sequence example

With all three probes connected:

```text
DISCOVERY

Hub port 1 -> FTDI -> serial.host
Hub port 2 -> ST probe -> debug.swd/program.stm32
Hub port 3 -> MSP probe -> program.msp/debug.msp

JOB 1: ESP

acquire FTDI
identify ESP
validate ESP image
enter ROM bootloader
erase/write/verify
reset target
release FTDI
unload ESP protocol if desired

JOB 2: STM32

acquire ST probe
identify STM32
validate STM32 image
halt/erase/program/verify/reset
release ST probe
unload STM32 protocol if desired

JOB 3: MSP

acquire MSP probe
identify MSP target
validate MSP image
erase/program/verify/reset
release MSP probe
unload MSP protocol if desired

BATCH COMPLETE

show/export three independent results
```

At no point does completing Job 1 require disconnecting Jobs 2/3's physical probes.

---

## 39. Programmer application UI requirements

A future Programmer app SHOULD expose:

```text
Connected Probes
Target association
Firmware/image selection
Per-job configuration
Ordered batch queue
Current operation
Per-target progress
Verification state
Errors/warnings
Batch summary
Saved logs
```

The UI SHALL make physical identity clear enough that a user cannot easily flash the wrong target when multiple probes are present.

---

## 40. Provider/package distribution

Programming and transport providers SHOULD be independently updateable packages.

Conceptual namespaces may include:

```text
/Drivers/
    usb-host-ftdi/
    usb-host-stlink/
    usb-host-msp-debug/

/Services/ or /Providers/
    program-esp-rom/
    program-stm32-swd/
    program-msp/
```

The final package taxonomy SHOULD preserve the distinction between low-level hardware transport drivers and higher-level target programming protocols.

---

## 41. Diagnostics

Programmer diagnostics SHOULD expose the entire resolution chain:

```text
Hub path
 -> USB VID/PID/serial
 -> USB provider
 -> transport capability
 -> programmer/debug provider
 -> detected target
 -> selected image/profile
```

This is critical for debugging provider matching and multi-device ambiguity.

---

## 42. Testing requirements

At minimum, tests SHOULD prove:

1. FTDI, ST probe, and MSP probe can remain enumerated simultaneously through one powered hub;
2. each receives a distinct stable framework handle;
3. each binds to the correct provider;
4. sequential jobs acquire/release only their intended transport;
5. programming one target does not disconnect or corrupt unrelated probe state;
6. ESP protocol can reuse different serial transport providers where capabilities match;
7. target identity mismatch blocks destructive programming;
8. large firmware images stream without full PSRAM residency;
9. verification is performed/reported correctly;
10. unplugging the active probe fails only the active job and preserves other topology;
11. unplugging an inactive probe does not crash the active job;
12. retry validates device identity again;
13. batch STOP/CONTINUE policies behave correctly;
14. protocol ELF unload between jobs reclaims resources;
15. all handles/power locks/buffers are reclaimed on cancellation/failure;
16. unsigned/unauthorized providers are rejected under production policy;
17. high-risk irreversible target operations require separate authorization;
18. image/profile address validation prevents obvious wrong-target writes;
19. audit logs identify probe, target, provider, image hash, and verification result;
20. hub power failure is surfaced distinctly from protocol failure;
21. a long-lived debug session exclusively owns its selected probe without blocking unrelated probes;
22. stale handles after re-enumeration are rejected.

---

## 43. Normative rules

1. **Multiple attached programming devices are first-class; no singleton programmer assumption is permitted.**
2. **USB adapter/provider, transport, target protocol, and job orchestration are separate layers.**
3. **FTDI is a serial transport provider, not an ESP programming implementation.**
4. **Target protocols consume generic transports whenever possible.**
5. **Jobs bind to explicit stable device handles/identities.**
6. **Sequential multi-target programming is the initial required execution model.**
7. **All probes may remain connected/enumerated while jobs execute in sequence.**
8. **Each destructive job identifies/validates its target before erase/write.**
9. **Verification is required by default.**
10. **Firmware images are streamed through bounded memory.**
11. **Probe disconnect invalidates only work bound to that probe.**
12. **Programming/debug resources are leased and reclaimed by the framework.**
13. **Raw debug/programming privilege is authorization-controlled.**
14. **Irreversible security/fuse operations are distinct from ordinary flashing.**
15. **Provider and firmware-image trust are separate policy decisions.**
16. **Parallel programming is future functionality and requires explicit resource scheduling.**

---

## 44. Implementation sequence

1. complete USB host/hub topology foundation from `USB_OTG_HOST_ARCHITECTURE.md`;
2. implement stable multi-device handles and USB diagnostics;
3. implement FTDI host provider exposing `serial.host` and control lines;
4. implement a transport-lease abstraction;
5. implement Espressif ROM programmer provider as the first target protocol;
6. build the Programmer app around one explicit job and streamed image;
7. add target identification, verification, logs, and cancellation;
8. implement ST probe/transport support and one concrete STM32 programming path;
9. implement MSP probe/transport support and one concrete MSP programming path;
10. implement the ordered Batch Job Manager;
11. validate FTDI + ST + MSP simultaneous enumeration on a powered hub;
12. execute ESP -> STM32 -> MSP sequentially without disconnecting any probe;
13. add retry/failure policies and robust hot-unplug handling;
14. integrate signed provider packages and firmware-image policy;
15. add long-lived debug-session abstraction;
16. add additional UART/DFU/SWD/JTAG providers and target families;
17. only then evaluate controlled parallel programming based on measured USB, power, memory, and CPU budgets.

The reference acceptance test for the architecture is deliberately concrete: **with an FTDI-based ESP programmer, an ST programming/debug probe, and an MSP programming/debug probe simultaneously attached to one powered USB hub, T5 shall identify all three, bind each to the correct transport/protocol stack, program and verify each target in a configured sequence, preserve the other two connections while each job runs, and produce three independent auditable results.**
