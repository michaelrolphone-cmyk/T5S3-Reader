# U1 observed storage generations and retained inventory

## Connected boundary

HalStorage owns RAM-only, non-wrapping mount/mutation epochs under its existing
storage mutex. Every managed write/create/truncate/append/namespace attempt
invalidates previous stamps, including unsuccessful attempts. Tracking is
storage-wide rather than a lexical managed-root prefix: case aliases, relative
paths and an outside writable handle subsequently renamed into a managed root
cannot preserve a trusted stamp. Known board SD shutdown also invalidates it and marks storage unavailable.

A writable HalFile keeps stamps non-quiescent until close/destruction. Move
construction transfers that ownership; move assignment closes the old handle
under the same lock. Helper-returned handles are registered before unlocking;
assignment to a pre-existing destination occurs outside the non-recursive lock.
An explicitly retained handle may retry a failed close. If failed-close
ownership is discarded (destructor/raw helper, or a closed object despite
failure), uncertainty stays latched instead of allowing remount to discard
potentially unsynced cache state.
Remount is refused while any HalFile or uncontrolled compatibility session is
alive, and every attempt invalidates old stamps. Failed mount leaves storage
unavailable; counter exhaustion permanently refuses coherent stamps/remount.
The existing SdFat 2.3.1 source confirms volume initialization calls cache.init,
which invalidates sector/status state; this supports using successful remount,
not merely an epoch increment, after raw SDFS access (FatPartition.cpp::init,
common/FsCache.h::init).

The temporary native compatibility table remains available. Actual registered
symbol resolution, rather than merely registering the table, detects mutable
SD/FS authority. Exact names include writable-capable open/openNextFile,
namespace mutation, File close/flush/write, SD begin/end and File's vtable
(indirect mutators). Importing one makes storage uncertain through teardown.
Even successful unregister leaves stamps untrusted until a no-live-handle
remount refreshes the independent SdFat view after possible SDFS access.
Package Manager requests that reconciliation before its scan. Failed relocation
uses the same conservative rule; retained module, failed unload or
failed unregister leaves uncertainty sticky. Read-only normal `/sd` VFS imports
and ordinary scoped streams do not acquire this raw-compatibility window.
This does not sandbox malicious native code or prove absence of unknown aliases.

## First retained consumer

Package Manager's installed inventory is exposed only after a complete bounded
scan has closed with the same quiescent stamp. It rejects directory errors,
failed closes, concurrent observed mutation, a live writer/raw session, more
than 256 examined entries per root or more than 128 installed rows. Per-entry
scheduler checkpoints and a two-second per-root cooperative deadline bound
returned I/O work; they cannot terminate a lower SD call that never returns.
Count/get refuse stale rows after mutation, remount or compatibility access.
The normal invalid-package diagnostic row remains available when its bounded
metadata inspection fails without a whole-inventory I/O fault.

General installed capability queries now reuse an immutable metadata snapshot
only within the same observed quiescent generation. Capture and lookup validate
the epoch outside a short pointer-ownership mutex; filesystem work never occurs
under that mutex. A scan that starts quiescent but changes fails closed. Warm
queries do not re-enumerate files or hash installed ELF bytes. Mutation, remount,
failed remount and raw compatibility access invalidate coherent snapshots.

NativeCapabilityGate and InstalledProviderGraph also use metadata queries while
legacy raw-storage applications run. These non-quiescent callers retain fresh,
operation-local metadata queries, without entering the reusable cache; global
quiescence is not a new prerequisite for working provider acquisition. Neither
mode attests executable contents or replaces independent runtime authorization.
Candidate metadata refusals can also be transient allocation failures without
a storage mutation. Such scans remain operation-local and are never retained,
so a later same-generation query can recover the missing provider. Directory
errors and overflow still refuse partial inventories. The local
production-HalStorage fixture verifies zero additional SD opens/reads on warm
capture, invalidation and interleaved-mutation refusal, as well as repeated
uncached raw-session queries. Target checks for this cache addition passed at029a58da (36836831826 /
36836831798), with later checkpoints retaining the coverage.

## Evidence and limits

`test/run_storage_generation_test.sh` compiles real HalStorage against fault
media, exercises the actual registered resolver/compatibility lifecycle and
production Package Manager inventory functions, and retains the legacy
capability-query path during a raw session. Coverage includes move/destructor
ownership, writable opens, unsuccessful mutations, failed remount, exhaustion,
interleaved capture, stale queries, iterator/open faults and item overflow.
Launcher tests separately cover failed relocation, failed unload and retained
module teardown. Tests are wired into the existing platform host workflow.

These are observed coherence epochs, **not** content hashes, durable verification
receipts, persistent boot generations, permissions or post-install SD tamper
proof. SdFat malformed LFN/checksum entries can be rejected without getError;
no universal media-corruption detector is claimed. Provider/app owned-buffer admission and graph-owned remap proof are now connected
and target-verified in their linked implementation notes. Staged receipt
serialization and cold boot trust are implemented; persisted bytes never seed
RAM proof. Install/update/recovery and explicit integrity checking retain SHA.

Explicit full verification now advances this existing non-wrapping observed
generation before canonical/pair checking. No media I/O, remount or handle/task
ownership change is performed by that invalidation. The actual pair/helper
fixture demonstrates the former failed-full-check memo reuse, its correction,
old/in-flight snapshot non-promotion, and later valid warm reuse. A live reader
remains owned and usable at the HalFile layer; prior observation-based consumers
must reacquire current evidence. This correction's target checks are pending.
The separate SD/SPI stop/quarantine contract remains an owner decision, not a
consequence of advancing a RAM observation counter.
Firmware final-version reconciliation remains required before a final candidate.

**Implementation In Progress**
