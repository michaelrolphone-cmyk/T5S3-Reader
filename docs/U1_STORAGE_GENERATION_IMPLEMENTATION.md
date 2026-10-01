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
Remount is refused while any HalFile or uncontrolled compatibility session is
alive, and every attempt invalidates old stamps. Failed mount leaves storage
unavailable; counter exhaustion permanently refuses coherent stamps/remount.

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

General installed capability queries deliberately retain their operation-local
metadata semantics. NativeCapabilityGate and InstalledProviderGraph use them
while legacy raw-storage applications run. Requiring global quiescence there
would disable otherwise working provider acquisition. Their directory scan now
refuses observed iterator failures and overflow instead of returning partial
candidates, but they are **not** trusted generation-bound reusable snapshots.
A compatible broader inventory policy remains open.

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
no universal media-corruption detector is claimed. Generic capability/provider,
app and file-association caches still need their respective coherent policies.
Install/update/recovery retain SHA verification; durable receipt serialization,
boot/recovery trust and explicit verification invalidation remain U1 work.
Firmware final-version reconciliation remains required before a final candidate.

**Implementation In Progress**
