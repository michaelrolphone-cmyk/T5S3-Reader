# U1 SD/shared-SPI retained-fault implementation

The owner approved a manual-reboot, fail-closed SD/shared-LoRa fault policy on
October 1. This implements that option in the existing module-store port; it
does not implement controller recovery, automatically reboot, or extract U3 bus
providers. Normal successful SD, raw SDFS/File and LoRa operation stays supported.

## Production boundaries

- `SdSpiFault.cpp` observes existing task ownership in64 fixed slots. Nested
  operations preserve their original30-second deadline. HalStorage operations,
  Arduino SD disk operations and global FSPI transactions share that observer;
  it is not another bus mutex or a capability/installer registry
- The pinned Arduino2.0.17 patch bounds all41 `cmd.usr`/`cmd.update` polls to a
  fixed1-second transfer wait, checks chunk loops, and guards FSPI entrypoints.
  SPI parameter and bus mutexes remain the actual owners. Acquisition uses at
  most300 cooperative100-ms attempts within one30-second deadline. Failure
  never gives an unowned mutex or proceeds into transfer/reset. FSPI startup's
  controller reset now occurs only after the existing bus mutex is acquired
- Checkpoints yield after8ms of work; slow controller polls also yield. Existing
  subscribed callers are fed while waiting for mutex acquisition. This is a
  real scheduler wait, not just a watchdog reset. Sector retries, nested SPI
  calls and transactions cannot refresh an enclosing operation's deadline
- On uncertainty the latch is permanent until manual reboot. The current task
  remains parked with its TCB, stack, held mutexes and live buffers intact. Its
  task-watchdog subscription is removed so parking itself does not request a
  reboot. If removal fails, the same retained frame instead feeds that watchdog
  with cooperative100-ms waits. Accidental task resume returns to the park loop. Deleting an active
  or retained port owner is refused by retaining the deleting caller too;
  neither owner reassignment nor destructive cancellation is performed
- HalStorage readiness/generation become unavailable without waiting for the
  retained mutex. New managed operations refuse; close/flush do not touch the
  media. HalFile move/destruction retain raw FsFile allocations rather than
  invoke SdFat's syncing destructor. Remount/reconciliation cannot clear fault
- Raw compatibility SDFS sector paths share the bounded operation observer.
  Buffered File/VFS entrypoints and destructors have the generic retention
  barrier before libc/file locks or free/close. Opaque raw VFS cleanup is
  conservatively stopped after the fault; no parallel volume registry or
  attempt to guess a returned File's ownership was introduced. Healthy unrelated
  VFS operations do not acquire the SD operation deadline
- LoRa reports `T5_LORA_ERROR_REBOOT_REQUIRED`, refuses new I/O and display
  handoff, and preserves its radio/pin/rail state. The LoRa app respects a refused
  display handoff. Actual SD/shared-LoRa GPIO and raw compatibility pin APIs
  cannot reset/reassign the protected pins after failure. Unrelated pins remain
  available. Board reinitialization and automatic sleep cannot bypass retention
- A hardware-neutral runtime disposal barrier protects app finalizers, execution
  context/heap cleanup, dlclose and provider graph/module disposal. It does not
  grant access or identify USB/LoRa in the generic loader. The existing provider
  registry, package engine and hardware ownership remain unchanged

The published port ABI and its actual callers are normal-task APIs. This is not
an isolation boundary against arbitrary privileged ELF MMIO, unsupported ISR
use of task-blocking APIs, or code deliberately freeing another task's live
objects. No universal raw-native sandbox or recovery from a stopped scheduler
is claimed. The existing main/native/storage/radio call paths are the scope.

## User-visible failure

A one-time console diagnostic says SD/SPI is unavailable and requires a manual
reboot. Managed app/package requests and the LoRa state API return explicit
unavailable/reboot information. If the failed owner is the UI task, its screen
may remain frozen; unsafe display/shared-pin cleanup is not used to redraw an
error. Use a manual reset/power cycle. No automated reset, retry-remount,
controller abort, pin/rail reset or forced task deletion clears this state.

## Source and version custody

`scripts/patch_sd_spi_fault.py` verifies exact upstream Git blobs and exact
already-patched contents. Drift fails the build. PlatformIO PRE middleware compiles four patched source copies and two private
headers solely inside that environment's build directory. Shared SDK sources
remain at their verified original hashes. A one-time compatibility path reverses
only this U1 patch's exact hash-verified older output if restored from cache; it
never deletes a whole SDK, toolchain or cache. The build validates all four
selected units and absence of private headers from the shared SDK. Lifetime
wrappers are enabled on both current board profiles. The relevant framework sources are Arduino2.0.17:

- `cores/esp32/esp32-hal-spi.c`:c635b836e8e77412f3fa5e7bf7196a477eb86d99
  (PlatformIO3.20017.241212+sha.dcc1105b packaged backport)
- `libraries/SPI/src/SPI.cpp`:3af07515c2be974168ddb683802f784f182b1769
- `libraries/SD/src/sd_diskio.cpp`:da967338589e71d18517d6137ecc8a573b438d13
- `libraries/FS/src/vfs_api.cpp`:1dd8da94ac93cc6bf5e4f5bcc6854ab4fdef5481

Firmware remains the cumulative unreleased1.3.50 candidate. LoRa remains its
cumulative U1 ZIP1.0.1, above source/published1.0.0; the new refusal behavior is
part of that same unreleased payload. The only observed LoRa release tag at
13:34 UTC was app-lora-v1.0.0. Text Editor342 is owner-merged in master4530c8b2;
U1 preserves that code and advances its ZIP identity0.2.2 →0.2.3.

### Packaged-source reconciliation

The first target build at782e0883 correctly refused the official2.0.17 HAL
source pin. The actual PlatformIO package contains the backported PR9333
configuration-update synchronization in spiTransaction, including one additional
cmd.update wait. The normal-build diagnostic at a88c3158 preserved that exact
input; artifact11168113207 ZIP SHA-256
1750c8399f798829ec9f47d7d518b4ea7c63d55abed5a9187b3ee1436f79251f
was downloaded and verified. The other three files match their original pins.
The correction preserves that hardware fix and bounds all41 waits rather than
removing it or accepting arbitrary source drift. All files validate before any
cached dependency file is modified.

### SDK cache isolation correction

The policy runtime passed both workflows at8cc11cad, but its original post-build
setup modified the shared PlatformIO framework package. That could contaminate
later local builds using that package. PRE middleware now selects per-build
copies instead. Host tests cover clean inputs, exact older-patch restoration,
unrelated source passthrough and source/object paths confined to the build tree.
No Mac SDK/configuration/toolchain was accessed by this cloud worker. The older
PR CI cache key is Linux-platformio-197500d0027cafa15431eaa41e9f15ab044ffabd0b588b4c661e37658fbf0d59;
its existence is recorded in the EPD job's cache-save log. This correction cleans
only the known patch when that package is restored; no cache entry deletion is
claimed. The new target run must verify original shared sources after build.

## Development evidence and remaining physical gates

`run_sd_spi_fault_test.sh` executes actual policy, HalStorage, LoRa and GPIO
adapter code. Cases cover normal ownership, busy transfer, failed acquisition,
whole-operation/nested-retry limits, tick wrap, capacity exhaustion, active task
deletion, failed retry, move/destructor retention and display-handoff refusal.
The pinned-source patch fixture executes the real transformed SPI wait and raw
VFS close bodies; it verifies pre-lock reset ordering, all guarded waits/entries,
idempotence and source-drift refusal. Binding guards cover module/task/board
teardown. Existing native-app, stream, storage-generation and provider-graph
aggregates passed locally. Target results remain pending until this exact
published source completes both normal workflows.

No device stall, card removal, manual reset, shared-LoRa electrical state or
physical restart/recovery result is claimed. Those belong to owner-directed
Release Qualification on the complete candidate, alongside the existing USB
cable/PHY and four-kind package checks. The separate b78955ac CAM composite
clock/archive witness passed twice; it is not full U1/T5S3 qualification. The
previously blocked independent receipt review remains explicitly incomplete.

**Implementation In Progress**
