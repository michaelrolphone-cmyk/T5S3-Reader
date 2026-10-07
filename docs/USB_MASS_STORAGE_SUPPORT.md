# RiscRTE USB Mass Storage and Generic Block/Volume Architecture

## Status and authority

**Normative USB mass-storage and removable-volume specification.** Read [RiscRTE Platform Specification](RISCRTE_PLATFORM_SPEC.md), [Hardware-Agnostic Runtime and Driver Ownership Contract](HARDWARE_AGNOSTIC_DRIVER_BOUNDARY.md), [USB OTG Host, Hub, and Device-Function Provider Architecture](USB_OTG_HOST_ARCHITECTURE.md), [USB Hub and Multi-Device Host Support](USB_HUB_SUPPORT.md), and [Stream and Pipe Architecture](STREAM_PIPE_ARCHITECTURE.md) first.

This document defines the provider boundaries, ABIs, lifecycle, safety rules, implementation sequence, diagnostics, and acceptance requirements for USB Mass Storage Class devices and the generic block/volume abstractions needed to expose removable filesystems to applications.

Adding this specification does not claim that USB MSC, generic block devices, generic volumes, writable removable filesystems, or hardware qualification are already implemented.

## 1. Objective

RiscRTE SHALL support USB Mass Storage Class devices without introducing USB, SCSI, block-device, FAT, or removable-media implementation into the generic runtime core.

The target composition is:

```text
USB flash drive / card reader / mass-storage device
                  |
              usb.host
                  |
           usb_msc.elf
                  |
            storage.block
                  |
        filesystem provider ELF
                  |
           storage.volume
                  |
      generic file/stream facilities
                  |
       File Browser / applications
```

The same `storage.block` and `storage.volume` abstractions SHALL be reusable by non-USB storage providers where semantically compatible.

## 2. Architectural invariant

The generic RiscRTE core MAY manage:

```text
opaque capability names
provider dependency resolution
execution contexts
generic software leases
generic streams
generic device/volume records
generic events
generic namespace handles
provider lifecycle
bounded diagnostics
```

The generic core MUST NOT:

```text
parse USB MSC descriptors
implement Bulk-Only Transport
generate or parse SCSI commands
own USB bulk endpoints
interpret SCSI sense data
read or write raw disk LBAs directly
parse FAT/exFAT structures as USB behavior
infer media presence from USB addresses
flush/eject USB hardware directly
own a USB mass-storage session
```

USB MSC behavior belongs in an installed USB class/provider ELF. Filesystem behavior belongs in a filesystem provider. Physical storage ownership remains in providers.

## 3. Required layer separation

USB mass-storage support SHALL be separated into at least these semantic layers:

### 3.1 USB transport layer

`usb.controller` and `usb.host` own:

- physical USB controller operation;
- device generations;
- descriptor access;
- interface claims;
- control requests;
- bulk transfer authorization;
- detach/reconnect;
- hub topology;
- transfer quiescence.

### 3.2 MSC class/provider layer

`usb_msc.elf` owns:

- USB MSC interface matching;
- Bulk-Only Transport;
- Command Block Wrapper generation;
- Command Status Wrapper validation;
- SCSI command encoding/decoding;
- logical-unit discovery;
- media readiness;
- capacity discovery;
- block read/write;
- stall/reset recovery;
- device-specific teardown.

It publishes a semantic block-storage capability such as `storage.block@1`.

### 3.3 Filesystem layer

A filesystem provider consumes `storage.block` and owns:

- partition recognition where applicable;
- filesystem recognition;
- filesystem metadata;
- mount/unmount;
- directory and file operations;
- filesystem-level buffering;
- metadata/data synchronization;
- dirty-state handling.

It publishes a semantic capability such as `storage.volume@1`.

### 3.4 Application/file layer

Applications consume mounted-volume/file interfaces, streams, or trusted file pickers. They do not issue SCSI or USB commands and should not need to know whether a volume came from USB, SD, eMMC, internal flash, or another provider.

## 4. Generic block-storage capability

RiscRTE SHALL define a versioned provider ABI for random-access logical blocks. The recommended semantic capability is:

```text
storage.block@1
```

The ABI SHALL use opaque generation-qualified handles.

A v1 interface SHOULD provide operations equivalent to:

```text
inventory/snapshot
open
get_info
read_blocks
write_blocks
flush
media_state
eject_or_prepare_removal
close
```

Exact symbol names may differ, but the semantics below are required.

## 5. Block-device information

A block-device information record SHALL contain bounded semantic fields sufficient for filesystem providers and diagnostics, including:

```text
generation-qualified device identity
logical block size
logical block count
capacity
read-only flag
removable flag
media-present state
ready/not-ready state
flush capability
optional discard/trim capability
optional stable provider label
```

It MUST NOT require consumers to understand:

```text
USB address
USB endpoint
hub port
CBW/CSW
SCSI opcode
USB interface number
```

Transport-specific data may appear in provider diagnostics but is not block capability authority.

## 6. Block I/O contract

Block reads and writes SHALL operate on complete logical blocks.

Every request SHALL specify:

```text
block handle
starting LBA
block count
destination/source buffer
buffer capacity
bounded timeout or provider-defined bounded policy
```

The provider SHALL reject:

- integer overflow;
- LBA range overflow;
- a byte count inconsistent with block size/count;
- stale generation handles;
- writes to read-only media;
- requests exceeding configured transfer bounds;
- operations after media loss.

No caller-provided pointer may be retained beyond the bounded call unless an explicit provider-owned asynchronous transfer ABI is used.

## 7. Bounded transfer behavior

Large storage requests SHALL be subdivided into bounded physical transfers.

For example:

```text
filesystem requests 128 KiB
        |
MSC provider performs bounded chunks
        |
yield/cancel checkpoint
        |
next bounded chunk
```

The exact transfer size is provider/controller policy, but implementations SHALL define:

- maximum blocks per command;
- maximum bytes per transfer;
- maximum command duration;
- maximum retry count;
- cancellation points;
- scheduler yield points for substantial repeated work;
- bounded DMA/buffer usage.

A multi-megabyte file operation MUST NOT become one uninterruptible USB provider call.

## 8. USB MSC class matching

The MSC provider SHALL perform its own descriptor/interface matching using `usb.host`.

The generic runtime MUST NOT contain class-code, subclass, protocol, VID/PID, or device-specific MSC selection.

The initial provider SHOULD support the common USB Mass Storage Class Bulk-Only Transport path.

Unsupported subclasses/protocols SHALL fail explicitly without claiming the interface.

The provider SHALL claim only the required interface(s) and associated bulk endpoints through the host capability.

## 9. Bulk-Only Transport state machine

USB MSC Bulk-Only Transport SHALL be implemented as an explicit bounded state machine.

Conceptually:

```text
IDLE
  |
SEND_CBW
  |
DATA_IN / DATA_OUT / NO_DATA
  |
READ_CSW
  |
COMPLETE
```

Failure/recovery states SHOULD include equivalents of:

```text
STALLED
REQUEST_SENSE
RESET_RECOVERY
DEVICE_GONE
FAILED
QUARANTINED
```

The provider MUST NOT accept a command as successful unless the entire transport result is coherent.

## 10. CBW/CSW validation

Every command SHALL use a valid Command Block Wrapper with a provider-controlled command tag.

The returned Command Status Wrapper SHALL be validated for at least:

- expected signature;
- matching command tag;
- valid status value;
- valid residue;
- transfer length consistency.

A mismatched tag, malformed signature, impossible residue, short/missing status wrapper, or incoherent transfer result SHALL fail the command and enter appropriate bounded recovery.

A successful bulk transfer alone is not proof that the SCSI command succeeded.

## 11. Minimum SCSI command set

The first useful read-only implementation SHALL support at least:

```text
INQUIRY
TEST UNIT READY
REQUEST SENSE
READ CAPACITY (10)
READ (10)
```

The writable implementation SHALL additionally support:

```text
WRITE (10)
SYNCHRONIZE CACHE where supported/required
```

The provider SHOULD add as needed:

```text
GET MAX LUN at USB class level
MODE SENSE
START STOP UNIT
PREVENT/ALLOW MEDIUM REMOVAL
READ CAPACITY (16)
READ (16)
WRITE (16)
```

Support for commands beyond the required set SHALL remain bounded and version-compatible.

## 12. Sense-data handling

When a command fails and sense information is available, the MSC provider SHALL perform bounded REQUEST SENSE handling.

Diagnostics SHOULD preserve:

```text
sense key
additional sense code
additional sense qualifier
failed operation
retryability classification
```

The provider SHALL distinguish at minimum:

- media not present;
- becoming ready/not ready;
- write protected;
- illegal request/unsupported command;
- medium error;
- hardware error;
- unit attention/media changed;
- transport failure.

Retry policy SHALL be explicit and bounded. A permanent or unsupported condition MUST NOT cause an infinite readiness loop.

## 13. Logical units

The initial implementation MAY support only LUN 0 if explicitly documented.

If GET MAX LUN is implemented and multiple LUNs are exposed, each logical unit SHALL receive an independent generation-qualified block-device identity or equivalent independent block session.

One failing LUN SHALL NOT silently alias another.

The maximum supported LUN count SHALL be bounded.

## 14. Capacity and large media

The initial implementation MAY target common flash media addressable through READ CAPACITY (10) and READ/WRITE (10).

Media requiring wider addressing SHALL fail explicitly until READ CAPACITY (16) and READ/WRITE (16) are supported.

Capacity arithmetic SHALL use sufficiently wide integer types to prevent overflow when calculating:

```text
block_count * block_size
LBA + count
byte offsets
filesystem ranges
```

Unsupported block sizes SHALL be rejected or handled through a documented compatibility policy.

## 15. Read-only first milestone

The first implementation milestone SHOULD be read-only.

Read-only acceptance includes:

```text
device attach
MSC probe
media readiness
capacity discovery
block reads
filesystem recognition
read-only mount
directory enumeration
file open/read
safe hot removal
```

Write support SHALL be a later stage after read-only transport, generation handling, removal, and filesystem behavior are stable.

## 16. Write support

Writable MSC support SHALL not be enabled merely because WRITE (10) works.

Write acceptance requires:

- block write correctness;
- filesystem metadata update correctness;
- bounded write buffering;
- explicit flush semantics;
- clean unmount/eject path;
- write-protect handling;
- surprise-removal behavior;
- no stale-handle writes after reconnect.

Applications SHALL not receive a successful close/sync result before required provider/filesystem flush work has completed or failed explicitly.

## 17. Flush semantics

Flush SHALL be layered.

Conceptually:

```text
application/file flush
        |
filesystem metadata/data flush
        |
storage.block flush
        |
MSC SYNCHRONIZE CACHE if applicable
        |
device completion
```

If a device does not support a cache-flush command, the provider SHALL expose that fact rather than inventing stronger durability than it can guarantee.

## 18. Generic filesystem provider

The USB MSC provider SHALL NOT directly implement FAT or expose file paths.

A filesystem provider consumes `storage.block`.

The first filesystem provider SHOULD support the format already most useful to RiscRTE removable storage, such as FAT16/FAT32. exFAT may be added separately if library/license/resource constraints are acceptable.

Filesystem implementation SHALL be independent of USB.

The same filesystem provider SHOULD be usable with another compatible block source without recompiling generic RiscRTE core.

## 19. Partition handling

Partition-table interpretation belongs above `storage.block` and below or within the filesystem-volume provider.

The implementation SHOULD support the layouts required by ordinary removable flash media, including as appropriate:

- superfloppy/no partition table;
- MBR;
- GPT later if needed.

Partition parsing SHALL be bounded and validate all offsets/sizes against the underlying block device.

Malformed partition metadata MUST NOT permit out-of-range block access.

## 20. Generic volume capability

Mounted filesystems SHALL publish a versioned semantic volume capability, recommended as:

```text
storage.volume@1
```

A volume record SHOULD expose semantic information such as:

```text
generation-qualified volume identity
backing block-device identity
display label
filesystem type
capacity
free space where available
read-only/read-write
removable
mounted/unmounted/error state
mount/namespace identifier
```

Applications SHALL use the volume identity or authorized file handles, not assume `/sd` is the only storage.

## 21. Namespace integration

RiscRTE SHOULD provide a generic namespace for mounted volumes.

A possible presentation is:

```text
/sd
/usb0
/usb1
```

but path names are presentation and MUST NOT be durable authority.

Internally, each mount SHALL be bound to a fresh generation-qualified volume.

If the same physical USB stick reconnects and receives the same visible mount name, all pre-removal file and volume handles remain stale and invalid.

## 22. Current SD implementation and migration

The current `HalStorage` implementation is SD-specific and wraps a global SdFat instance plus board-specific SD initialization.

It is a current implementation source, not the target abstraction for USB storage.

USB MSC support MUST NOT be implemented by inserting USB cases into `HalStorage`, pretending a USB drive is the existing global SD object, or introducing a second hard-coded global USB filesystem object into generic application infrastructure.

Migration SHOULD preserve required current SD functionality while introducing generic block/volume/file abstractions alongside it.

Longer term, SD may publish the same `storage.block` and/or `storage.volume` semantics, but converting SD hardware ownership is a separate scope unless explicitly scheduled.

## 23. File API integration

Current app-facing file APIs may be retained for compatibility while generic volume/file APIs are introduced.

New storage work SHOULD make file operations volume-aware and generation-safe.

File operations SHALL distinguish:

```text
not found
permission denied
read only
I/O error
media disconnected
stale volume generation
out of space
filesystem error
cancelled
timeout where applicable
```

A zero-byte read SHALL not be the sole representation of media removal or I/O failure.

## 24. Stream integration

A file on a mounted removable volume SHOULD be exposable through the existing generic byte-stream architecture.

For example:

```text
USB flash drive
   |
storage.block
   |
FAT volume
   |
file stream
   |
reader / copy job / programmer / parser
```

Large files SHALL be streamed rather than loaded in full.

Stream cancellation or application teardown SHALL close underlying file/volume references safely without requiring USB-specific logic in the generic stream scheduler.

## 25. File Browser integration

The File Browser SHOULD become volume-aware rather than USB-aware.

A root storage view may conceptually present:

```text
Storage
  SD Card
  USB Drive
  USB Drive 2
```

The File Browser SHALL consume generic mounted-volume/file APIs.

It MUST NOT branch on:

```text
USB VID/PID
USB class
hub path
MSC transport
SCSI commands
```

File Browser behavior for a USB volume should otherwise match compatible SD volume behavior.

## 26. Unified Device Registry publication

The MSC provider SHALL publish semantic physical-device capability information through the generic provider/device publication mechanism.

A USB mass-storage device may initially publish:

```text
storage.block
```

After successful filesystem recognition/mount, the filesystem provider may publish:

```text
storage.volume
```

A valid block device with an unknown or corrupt filesystem SHALL remain distinguishable from a physically absent device.

A mount failure MUST NOT be transformed into a false USB detach.

## 27. Device and volume identity

These identities are distinct:

```text
USB physical device generation
MSC logical-unit generation
block-device generation
volume generation
file handle generation
```

Implementations may collapse layers when safe, but stale identity MUST never gain authority over a replacement object.

Replugging a device creates a new physical generation and therefore invalidates every dependent block, volume, and file handle from the previous attachment.

## 28. Hot removal

Surprise removal is a normal removable-media event.

On physical USB detach, the provider chain SHALL:

1. stop new block admissions;
2. invalidate the physical/device generation;
3. cancel or fail outstanding MSC commands;
4. revoke block-device availability;
5. propagate volume unavailability;
6. fail open files/streams with a disconnect/stale-media condition;
7. stop future writes;
8. drain USB transfers/callbacks/DMA;
9. release interface claims;
10. close physical USB state when safe.

It SHALL NOT transparently bind an old volume/file handle to a reattached device.

## 29. Clean eject / prepare removal

A user-requested clean removal SHOULD follow:

```text
deny new mutable opens
        |
finish/close active writers
        |
flush filesystem data and metadata
        |
flush block device
        |
SYNCHRONIZE CACHE where supported
        |
unmount volume
        |
optional SCSI START STOP / eject semantics
        |
release MSC session
```

If any required step fails, the UI/API SHALL report that clean removal was not established.

A generic eject action may call provider-defined volume/block operations but the generic core does not issue USB or SCSI requests itself.

## 30. Dirty state and unsafe removal

If writable media disappears before a clean flush/unmount, the volume/provider SHOULD record an unsafe-removal/dirty condition for diagnostics.

RiscRTE SHALL NOT claim that data is durable merely because application buffers were released.

On reconnect, ordinary filesystem consistency behavior applies. Automatic repair, if ever supported, must be explicit and bounded rather than silently modifying media during detection.

## 31. Caching

Block/filesystem caches SHALL be bounded.

Correctness MUST NOT depend on large PSRAM caches.

Initial implementation SHOULD use a small deterministic cache appropriate to the filesystem library and workload.

Every dirty cache entry SHALL participate in flush/unmount semantics.

Cache memory SHALL be released or invalidated when its backing device generation disappears.

## 32. Cooperative operation

Long directory scans, large file copies, block reads/writes, formatting checks, and filesystem operations SHALL follow [Bounded Cooperative Operations](COOPERATIVE_BOUNDED_OPERATIONS.md).

Loops SHALL have:

- item/byte bounds;
- elapsed-time checkpoints;
- cancellation checks;
- real scheduler yields;
- bounded retries;
- no repeat full-volume scan on every UI frame/touch.

USB mass storage MUST NOT recreate the responsiveness failures caused by expensive work on interactive execution paths.

## 33. Hub compatibility

The MSC provider SHALL be hub-agnostic.

The same MSC class/provider implementation SHALL work for:

```text
root -> MSC device
```

and:

```text
root -> hub -> MSC device
```

without application or generic core changes.

Hub topology, port identity, host channels, and concurrent USB transfer scheduling are governed by [USB Hub and Multi-Device Host Support](USB_HUB_SUPPORT.md).

## 34. Multiple storage devices

The architecture SHALL support multiple simultaneous block devices where controller/provider limits permit.

Example:

```text
USB hub
  port 1 -> flash drive A -> storage.block A -> volume A
  port 2 -> flash drive B -> storage.block B -> volume B
  port 3 -> keyboard
  port 4 -> serial adapter
```

Each block device and volume SHALL have independent:

- identity/generation;
- media state;
- open handles;
- transfer state;
- cache state;
- errors;
- teardown.

Failure/removal of one drive SHALL NOT automatically unmount another healthy drive.

## 35. Transfer fairness with other USB classes

Sustained MSC traffic SHALL not monopolize USB host progress.

A large file copy or block read MUST coexist with latency-sensitive HID and other active classes according to the bounded host scheduling requirements in the hub specification.

MSC SHALL not rely on one permanent controller-wide blocking transfer that prevents unrelated interfaces from progressing.

## 36. Power

USB storage participates in normal provider-owned USB power budgeting.

The MSC provider may expose device power information for diagnostics but SHALL NOT directly manipulate board VBUS hardware.

A high-current drive or bus-powered hub configuration that exceeds the granted board power budget SHALL fail safely through USB/power-provider policy.

Storage mount success is not evidence that the electrical configuration is safe.

## 37. Authorization

Observing a removable storage device does not grant file or block access.

Generic execution-context rights SHOULD distinguish at least:

```text
read
write
configure/eject where applicable
```

An application with file-level read access SHALL NOT automatically receive raw block-write authority.

Raw `storage.block` write access is more privileged than ordinary file access and SHOULD normally be consumed by trusted filesystem/system providers rather than arbitrary applications.

## 38. Error model

MSC/block/volume layers SHALL preserve useful error distinctions.

### USB/MSC examples

```text
device detached
interface unavailable
bulk stall
BOT reset failed
CBW rejected
CSW malformed
CSW tag mismatch
command failed
timeout
quarantine/uncertain teardown
```

### SCSI/media examples

```text
not ready
no media
media changed
write protected
medium error
hardware error
illegal request
unsupported capacity/addressing
```

### block/volume examples

```text
stale generation
out-of-range LBA
read-only
filesystem unsupported
filesystem corrupt
mount failed
volume disconnected
out of space
flush failed
unsafe removal
```

Higher layers SHALL not collapse every failure to "file not found" or "drive missing."

## 39. Diagnostics

MSC diagnostics SHOULD expose bounded provider-owned information including:

```text
device generation
provider version
interface identity
LUN
vendor/product/revision from INQUIRY where available
block size
block count
capacity
read-only
media-ready state
last SCSI opcode
last sense key / ASC / ASCQ
bulk IN/OUT status
commands completed
bytes read
bytes written
stalls
BOT resets
timeouts
disconnect count
last error
```

Volume diagnostics SHOULD expose:

```text
volume generation
backing block generation
filesystem type
mount state
display label/mount name
capacity/free space
read-only/read-write
open handle count
dirty/unsafe-removal state
last filesystem error
```

Generic diagnostics may display copied records without implementing USB/SCSI/filesystem behavior.

## 40. Bounds

At minimum, providers SHALL define finite limits for:

- maximum MSC devices;
- maximum LUNs per device;
- maximum concurrent MSC commands;
- maximum blocks/bytes per command;
- maximum descriptor/control payload;
- maximum command timeout;
- maximum readiness retries;
- maximum recovery retries;
- maximum sense-data length;
- maximum filesystem-sector cache;
- maximum mounted removable volumes;
- maximum open files/streams;
- maximum directory entries processed per cooperative slice;
- maximum teardown duration before quarantine.

No malformed device or filesystem may cause unbounded allocation, recursion, retries, or blocking.

## 41. Provider unload

The MSC provider SHALL not unload while it has:

- a live interface claim;
- an active or unresolved USB transfer;
- a pending BOT command;
- provider-owned DMA/buffer state referenced by the controller;
- a mounted block consumer that has not released the provider dependency;
- uncertain teardown.

The filesystem provider SHALL not unload while it has live volume/file operations unless they are safely cancelled/revoked and drained.

Uncertain physical USB teardown SHALL pin/quarantine the required provider/dependency graph rather than unmap executable code that can still receive completion.

## 42. Package model

USB MSC and filesystem implementations SHALL be ordinary installable packages/providers under the unified package model.

A future MSC implementation update SHALL not require a firmware rebuild merely to change SCSI/BOT behavior.

A new filesystem provider SHALL be installable without modifying the generic USB provider.

Catalog metadata is discovery data, not hardware authority.

## 43. Current implementation gap

At introduction of this specification:

- the USB provider stack exposes descriptor, claim, control and bulk-transfer primitives sufficient to form the basis of an MSC class provider;
- generic streams already support bounded file-oriented byte movement;
- the File Browser exists as an application-facing file UI;
- `T5StorageApi` is file/path-oriented;
- `HalStorage` is SD/SdFat-specific and board-specific;
- there is no complete generic `storage.block@1` provider ABI;
- there is no complete generic `storage.volume@1` mounted-volume provider ABI;
- USB MSC BOT/SCSI is not yet a completed provider path.

This section records current state only. It does not weaken the target requirements above.

## 44. Implementation sequence

Implement USB mass storage in this order:

1. define the versioned generic `storage.block@1` ABI and generation/lifetime rules;
2. add simulated block-provider tests independent of USB;
3. implement a read-only `usb_msc` class/provider over the existing `usb.host` ABI;
4. implement BOT CBW/data/CSW state machine and bounded recovery;
5. implement INQUIRY, TEST UNIT READY, REQUEST SENSE, READ CAPACITY and READ;
6. publish block-device instances through generic provider/device publication;
7. define `storage.volume@1` and volume identity/lifecycle;
8. implement a filesystem provider over `storage.block`;
9. mount supported FAT media read-only;
10. integrate generic volume/file access with File Browser and byte streams;
11. validate direct-attached hot removal and generation invalidation;
12. add WRITE, filesystem mutation, flush and clean-eject semantics;
13. validate surprise removal during writes;
14. validate MSC behind a hub;
15. validate multiple simultaneous removable volumes;
16. perform owner hardware qualification.

## 45. Required simulated tests

Before hardware qualification, tests SHALL cover at minimum:

1. MSC class-match success;
2. non-MSC rejection without claiming;
3. interface/endpoint selection;
4. GET MAX LUN behavior;
5. INQUIRY success;
6. TEST UNIT READY ready/not-ready/no-media cases;
7. REQUEST SENSE parsing;
8. READ CAPACITY valid/invalid results;
9. READ success;
10. bounded multi-command read;
11. CBW tag progression;
12. CSW tag mismatch rejection;
13. invalid CSW signature;
14. invalid residue/status;
15. command failure followed by REQUEST SENSE;
16. bulk stall and bounded BOT reset recovery;
17. disconnect before CBW completion;
18. disconnect during data IN;
19. disconnect before CSW;
20. fresh generation after reconnect;
21. stale block handle rejection;
22. out-of-range LBA rejection;
23. read-only write rejection;
24. writable WRITE success when enabled;
25. flush success/failure;
26. block-provider removal revokes dependent volume;
27. filesystem mount from simulated block device;
28. unsupported/corrupt filesystem remains a valid block device;
29. read-only directory traversal and file reads;
30. writable create/write/rename/delete where enabled;
31. open files fail cleanly on media removal;
32. clean unmount/eject flush ordering;
33. second volume remains healthy when first is removed;
34. sustained MSC traffic does not indefinitely starve HID/serial;
35. provider unload leaves no live USB callback/DMA/claim;
36. bounded caches and retry loops remain within configured limits.

## 46. Hardware acceptance sequence

Hardware qualification SHOULD proceed in stages:

```text
Stage 1: direct USB flash drive, read-only
Stage 2: several flash drives with different controllers/capacities
Stage 3: directory browse and file read in File Browser
Stage 4: idle hot-remove/replug
Stage 5: hot-remove during sustained read
Stage 6: enable write path and verify create/copy/delete
Stage 7: clean flush/eject
Stage 8: hot-remove during write; verify controlled failure/dirty reporting
Stage 9: MSC device behind a supported powered hub
Stage 10: hub + storage + keyboard
Stage 11: hub + storage + keyboard + serial
Stage 12: two simultaneous mass-storage devices
```

Qualification SHALL record:

- firmware/provider versions;
- device make/model/controller where identifiable;
- reported logical block size/capacity;
- filesystem;
- direct vs hub topology;
- power arrangement;
- read/write results;
- disconnect behavior;
- resource counters;
- teardown/quarantine results.

## 47. Acceptance criteria

USB mass storage is implemented only when all applicable criteria are met:

1. an installed MSC provider discovers and operates a supported mass-storage device without compiled firmware MSC logic;
2. supported media publishes a generation-safe `storage.block` instance;
3. a filesystem provider mounts supported media without USB-specific filesystem code;
4. mounted media publishes a generation-safe `storage.volume` instance;
5. File Browser can browse/read supported removable media through generic volume/file APIs;
6. large file reads remain bounded and cooperative;
7. disconnect invalidates block, volume and file generations immediately;
8. reconnect does not revive stale handles;
9. unsupported/corrupt filesystems remain distinguishable from absent USB devices;
10. write support, when enabled, has real flush and clean-eject semantics;
11. surprise removal during write reports unsafe/dirty failure rather than false success;
12. MSC works direct-attached and behind a supported hub using the same class provider;
13. multiple supported storage devices can coexist where resources permit;
14. storage traffic does not indefinitely starve other USB classes;
15. provider unload proves claims/transfers/callbacks/DMA are quiescent;
16. generic RiscRTE core contains no new MSC/SCSI/USB-storage implementation;
17. removal of the MSC provider removes USB mass-storage functionality with no hidden firmware fallback;
18. owner hardware qualification succeeds on a documented device matrix.

## 48. Failure criteria

The implementation is noncompliant if any of the following occur:

- USB MSC or SCSI logic is implemented in generic compiled firmware;
- MSC mounts FAT directly inside a USB-specific core bridge;
- USB mass storage is wired into the existing global SD object as a special case;
- File Browser requires USB-class or VID/PID logic;
- an old block/volume/file handle accesses a reattached device;
- command success is inferred without valid CSW/SCSI status;
- long storage I/O monopolizes the USB/controller/UI execution path;
- write completion is reported before required flush semantics are satisfied;
- media removal silently redirects an old handle to new media;
- malformed capacity/partition/filesystem metadata permits out-of-range I/O;
- one failed drive unnecessarily tears down unrelated healthy drives;
- unloading a provider leaves USB callbacks, DMA, transfers, claims, files, or volume references live;
- removing the MSC provider leaves a hidden normal-runtime firmware mass-storage fallback.

## 49. Normative summary

1. **USB MSC is an installed class/provider, not a core subsystem.**
2. **MSC publishes generic block storage; it does not publish file paths directly.**
3. **Filesystem providers consume generic block storage and publish generic volumes.**
4. **Applications and File Browser consume generic volumes/files and remain USB-agnostic.**
5. **Every physical, block, volume, and file lifetime is generation-safe.**
6. **Read-only support precedes writable support.**
7. **BOT/SCSI commands are explicit, bounded, validated state machines.**
8. **Large I/O is chunked, cancellable, cooperative, and memory-bounded.**
9. **Flush/eject semantics are layered and never stronger than the hardware can establish.**
10. **Surprise removal is expected and safely invalidates all dependent handles.**
11. **Hub and direct-attached MSC use the same class provider.**
12. **Multiple storage devices are independent instances where resources permit.**
13. **Storage traffic must coexist fairly with HID/serial and other USB activity.**
14. **No USB mass-storage or filesystem fallback is permitted in compiled generic firmware.**
