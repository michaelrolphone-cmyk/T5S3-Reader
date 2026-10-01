# U1 local verification receipts and provider snapshot admission

## Current connected slice

The existing SD-directory and ZIP stages now create a bounded manager-owned
`.package.receipt` after payload SHA/readback and manifest persistence. Receipt
write, close and readback failure aborts staging before the old target moves;
uncertain close preserves the stage instead of attempting deletion. The receipt
travels inside the existing stage-to-target directory rename. There is no
second commit store, installer, provider registry, signature, key or NVS floor.
This is the existing filesystem's crash-recoverable logical commit, not a claim
of stronger physical power-loss durability than its close/rename semantics.

The fixed 328-byte format binds format version, canonical kind/ID/version,
artifact/payload mode, exact manifest SHA-256, executable size/digest when
applicable, and a fresh 16-byte opaque installation marker. Resource-only packs
explicitly carry zero executable size/digest. The manifest digest also binds
all resource/dependency/import declarations. RAM mount/mutation counters are
not persisted and the marker is not an authentication credential.

Source directories and ZIP archives retain exact transport inventories; receipt
bytes are generated locally, never imported as authority. Managed trees accept
only the one exact reserved regular filename. Purge validates the retained
manifest/expected identity and complete known inventory before deleting anything,
then removes the receipt, declared leaves/directories and finally the manifest.
A malformed/truncated receipt supplies no deletion instructions. Receipt-as-
directory, aliases, unknown siblings and manifest-free nonempty roots fail.
Invocation-owned partial stages retain the existing narrowly scoped cleanup
exception. Legacy online scratch recovery and ordinary uninstall use the same
receipt-aware boundaries. Older canonical packages without receipts remain valid.

## First executable consumer: installed provider graph

Installed provider registration now captures the storage stamp before reading
its admission metadata and ELF. It preflights that exact captured manifest
against current runtime policy, hashes the actual profile/import snapshots
against its declarations, and copies the manifest digest, declared ELF digest
and original-read stamp through the existing owned graph. ABI/import matching,
dependency selection, caller authorization, grants and use pins remain independent.

`ModuleV2::loadVerifiedBytes` calls the private admission helper with the actual
owned buffer immediately before relocation. Cold/uncertain admission hashes that
buffer, not a reopened pathname or every installed resource. Hash work uses
512-byte chunks, scheduler checkpoints and a 10-second cooperative deadline;
a lower call that never returns is still outside that deadline's guarantee.
The exact current manifest, identity, architecture/runtime policy, declared
size/digest and retained graph manifest digest must agree.

A bounded 32-entry RAM memo can avoid repeated ELF hashing only when the stamp
captured **before the original source read** still matches the quiescent current
stamp. Its key also includes root, manifest digest, receipt generation and ELF
size/digest. Lookup/insertion hold only a short metadata mutex; no file I/O,
hashing or ELF callback occurs under it. A newly sampled post-mutation stamp can
never promote an older snapshot. Receipt publication, backup cleanup and CDC
settlement themselves mutate epochs, so install-time proof is not restamped
afterward. The provider consumer establishes its own current-process evidence.

Editable SD receipt data never initializes trusted RAM proof at boot. A missing
receipt retains older canonical compatibility; an invalid receipt disables
reuse but cannot substitute for actual-byte validation. Mutable legacy raw-FS
sessions keep working through cold snapshot verification without admitting a
coherent memo. Full install/update/recovery/explicit content verification still
checks every declared file. Metadata menus/capability discovery remain separate
and never invoke this executable hashing path.

## Observed checks and remaining work

Local receipt tests cover all four kinds, explicit data-only encoding, binding,
truncation, reserved fields, malformed strings, SD write/readback/close faults,
unknown-entry preservation and receipt-first/manifest-last cleanup. The actual
ZIP Stage fixture exercises receipt failures and retained uncertain-close stages.
The production HalStorage/admission helper demonstrates cold SHA despite an
existing receipt, warm bounded metadata-only hashing, raw compatibility,
stale-source epoch refusal, wrong manifest/runtime policy and receipt absence.
The real ESP branch of ModuleV2 is compiled with the real admission helper and
faultable HalStorage; relocation receives its independent admitted snapshot,
changed bytes fail before relocation, allocation failure/fallback and exact
imports remain checked. Full ordinary-package, provider-graph and driver host
aggregates pass with LeakSanitizer disabled for executor ptrace limits.

Published acaac50a passed provider workflow36839401087 and host/headless jobs
in36839401286. Both full firmware builds rejected a1168-byte recursive provider
registration frame against the existing384-byte limit. The correction preserves
the guard, keeps new digest/stamp workspace in the existing per-depth heap frame,
and places complete policy preflight out of that recursive frame. Its actual
Module/admission tests pass; both target workflows36840317554 / 36840317634
subsequently passed corrected c33cb044. Independent final review did not
complete; no completed final review is claimed. Findings returned before that
interruption were addressed and corresponding executed tests are identified
above; this is not a substitute claim about unreturned review coverage.

This does **not** close the whole permanent-verification requirement:

- Canonical application owned-buffer/context admission is now connected locally
  (U1_APPLICATION_SNAPSHOT_ADMISSION.md), with target verification pending. Loose
  legacy input retains its existing compatibility contract
- A retained provider node whose source-read epoch became stale conservatively
  rehashes on later remapping until rebuilt. Do not restamp old source bytes to
  suppress that work; a further optimization needs explicit immutable graph-
  owned memory lifetime evidence
- Cold provider checks establish executable/admission integrity, not current
  integrity of every optional resource. Explicit full verification remains
  available; no receipt authenticates a publisher or arbitrary external SD edits
- Observed storage epochs retain their documented unobserved-media limits;
  durable boot trust is always rebuilt from actual bytes, never trusted from SD
- Final target checks, remaining lower-I/O termination, final version/master
  reconciliation and owner-controlled hardware qualification remain separate

**Implementation In Progress**
