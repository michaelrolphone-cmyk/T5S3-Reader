# U1 nested resource engine

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

This closes the declared-tree transport/install/recovery portion only. Scoped,
generation-pinned installed resource access, resource-only package semantics,
archive service, CDC migration and verification receipts remain open. No new
package payload, version, live catalog, release or deployment is introduced.

**Implementation In Progress**
