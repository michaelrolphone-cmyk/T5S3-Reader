# U1 nested resource engine

## October 1 correction: actual ZIP staging integration

Earlier nested checks established directory intake, declared-tree helpers and
producer/parser fixtures. They did **not** establish nested extraction through
PackageOrdinarySdZipAdapter.cpp: that adapter retained a flat-only Stage and
inventory through bd9b09a7. Both offline and online ZIP installs use it.
The current resource-only slice connects that adapter to the already-verified
OrdinarySdTreeOps parent creation, exact inventory and manifest-last cleanup.
`package_sd_zip_stage_test.py` extracts and compiles the actual production Stage
with the existing faultable SD fake. At bd9b09a7 it fails on nested entry open;
with this correction it passes nested creation/readback/seal, failed parent
creation, unknown-data preservation and directory-iterator failure.
The earlier evidence below remains valid within its narrower scope. Matching
target checks for this integration correction are pending.


## Existing engine, versioned format

Ordinary schema 1 retains its flat-entry semantics. Schema 2 has the same
identity, version, executable, dependency and SHA fields, with nested declared
resource names. Executables remain canonical root basenames; resources cannot
hide another `.elf`. No new installer or transaction journal is introduced.

Each path is lowercase ASCII, at most 127 bytes and eight components. Every
component starts/ends alphanumeric and may contain internal `.`, `_` or `-`;
`..`, empty components, absolute paths, escapes, case aliases and file/directory
prefix conflicts are rejected. Sixteen declared files and existing per-file,
manifest and total ZIP bounds remain unchanged. Parent directories are implicit,
not extra archive entries. Their maximum is 16 × 7, represented as offsets into
the validated plan rather than a large path array.

Host packer, generic exporter, release record/index and runtime manifest/catalog
parsers share these semantics. The packer visits only declared parent paths,
rejects links and unknown files/directories, and emits deterministic stored ZIPs.
Existing app/driver builders still emit flat schema 1 until a package actually
requires resources; adding this engine capability does not change their bytes.

## Transaction and recovery

The SD source, stage, installed-directory verifier and tombstone cleanup use one
shared declared-tree inventory. It never traverses an unknown directory.
Parent creation is limited to a validated relative resource path beneath the
exclusively owned stage. Full validation requires all declared files and parents;
partial cleanup permits missing declared entries but refuses unknown data.

Cleanup validates before deleting, removes declared leaves, removes parents
in deepest-first order, and removes metadata last. Interrupted deletion retains
enough metadata to retry. Manifest-free tombstones must be empty; unknown or
user-owned content is never recursively deleted. The existing transaction/use
pins and rollback continue to guard generation publication and uninstall.

Tree visits and mutations have item limits, a 10-second cooperative deadline,
and real scheduler yields between operations. These bounds do not turn a blocked
underlying SD call into cancellable I/O; lower adapter termination remains a
separate U1 gap. A locked `HalFile::getError()` exposes the existing SdFat status
so a directory read error cannot be interpreted as clean EOF in this inventory.
Upstream SdFat2.3.1 `FsBaseFile::getError` and `FatFile::openNext` were inspected.
Malformed directory metadata can also cause an invalid iterator without setting
that read-error bit; this change does not claim universal corruption diagnosis
or fix the separate legacy app uniqueness resolver issue.

## Evidence and remaining dependent work

- Schema-1 rejection and schema-2 acceptance; unsafe path/depth/role/prefix cases
- Four package kinds through the actual generic stager, integrity/readback and
  installed verification, using flat and nested resources
- Shared tree and production SD-tree operations against fault-injected storage:
  unknown data preserved, read/close/parent-create failures, handle cleanup,
  interrupted metadata-last purge and restart
- Real nested ZIP/record/index/runtime immutable URL round trip; deterministic
  output, missing/corrupt resources, unknown directories and parent symlinks
- Existing ordinary/release/legacy compatibility suites remain required; exact
  new-head firmware CI is reported separately and no device claim is made

The declared-tree transport/install/recovery checkpoint is **e377948b**.
Its PlatformIO/host **36809347714** and USB/ELF **36809347775** workflows passed. These results do not certify subsequent scoped-access code.

## Installed application resource streams

The additive `T5PackageResourceApi` v1 opens only a non-executable resource name
in the current loader-admitted canonical app identity. No caller supplies a
package ID, root or write mode. `NativeAppHost` binds the identity after taking
its existing mapping pin; legacy loose apps have no such authority. The original
stream v1/v2 ABI layouts are unchanged. The new loader export returns ordinary
owner/generation-checked byte streams, with read/seek/pipe behavior and automatic
invocation cleanup. File I/O and destruction remain outside the stream mutex.

Each resource stream takes an additional package-use pin through retirement.
The bounded ordinary manifest is re-parsed and compared to the admitted kind,
ID, version and executable; only declared non-executable entries of exact size
open. Context replacement during storage access denies publication and retires
the candidate. Failed underlying close conservatively retains the generation
pin rather than permitting unsafe replacement. This can require restart before
replacement after an I/O error; it is not a claim of recoverable SD termination.

This prevents manager-driven ELF/resource generation mixing. It does not detect
same-size out-of-band SD modification or replace verification receipts/media
invalidation. No full content hashing is added to a normal resource open.
Actual stream-bridge tests exercise traversal, undeclared/executable refusal,
wrong version/length, read-only rights, pin lifetime, context change during open,
cleanup and uncertain-close retention with storage lock assertions.

## Installed provider, driver and service resources

The optional `risc_stream_provider_resources_v1` host-table suffix preserves the
existing stream-provider ABI prefix and existing package payloads. Its presence
is negotiated by `struct_size`; existing drivers keep using the original table.
The installed manager copies its admitted ordinary identity through executor,
owned graph and module context before binding the table. The module cannot choose
another root. An absent package-use pin refuses binding; legacy/unscoped module
contexts retain their original table without resource authority.

The same resource preparation primitive and byte-stream registry serve all four
kinds. A provider context may open at most four private resource handles. The
existing close slot closes either owned queues or resources; public endpoint
grants do not accept resource handles. Resource reads/seeks use existing
in-flight direct-I/O tickets, preserving revoke-during-I/O behavior. Revoke
retires file adapters outside the mutex and before normal module teardown;
failed quiescence still retains the ELF/dependency lifecycle as before.

Three-kind real bridge tests cover bounds, cross-context rejection, attempts to
publish resource rights, open/read revocation and pin retirement. The actual
loaded-ELF fixture exercises graph-owned identity copying, reading through the
extended table, failed-quiescence denial and retry. Original provider graph and
stream lifecycle suites remain required. No distributable driver source or old
SDK layout changed; no package version bump is needed for this runtime-only
addition. New consumers must negotiate the suffix before using it.

The app checkpoint **57c4b0a3** built both firmware boards and all app ZIPs and
passed USB/ELF **36810000332**. Its host job in **36810000245** found an old
source-shape test requiring an unconditional launcher statement. The new guarded
launch still runs stream/serial cleanup on both outcomes; that assertion is
updated to the guarded statement, and all seven USB-package tests pass locally.
The provider resource slice needs its own exact-head target verification.

Resource-only package semantics, archive service, CDC migration, verification
receipts/media invalidation and underlying I/O termination remain open. No new
package payload, version, live catalog, release or deployment is introduced.

**Implementation In Progress**
