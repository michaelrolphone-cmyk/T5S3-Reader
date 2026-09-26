# RiscRTE System Storage Layout

## Status and authority

This specification is normative for RiscRTE-owned persistent and semi-persistent
storage on the SD filesystem. It defines the meaning, ownership and allowed
contents of the top-level `/System` directory.

It is subordinate only to [RISCRTE_PLATFORM_SPEC.md](RISCRTE_PLATFORM_SPEC.md)
and explicit owner direction. Application-private storage requirements remain
governed by
[APPLICATION_EXECUTION_CONTEXT_ARCHITECTURE.md](APPLICATION_EXECUTION_CONTEXT_ARCHITECTURE.md).
Installed package layout remains governed by
[RISC_PACKAGE_FORMAT.md](RISC_PACKAGE_FORMAT.md) and
[BUNDLED_PACKAGE_ARCHIVE_AND_INSTALL_LAYOUT.md](BUNDLED_PACKAGE_ARCHIVE_AND_INSTALL_LAYOUT.md).

Historic `/.crosspoint` paths are legacy compatibility state and are not valid
precedent for new RiscRTE storage.

## 1. Root invariant

`/System` is the **RiscRTE-owned system namespace**.

It is for runtime/platform metadata and state, not user documents, package
payloads or arbitrary application-created folders.

The following top-level responsibilities are distinct:

```text
/Apps/          installed application package generations
/Drivers/       installed driver package generations
/Services/      installed service package generations
/Providers/     installed provider package generations
/System/        RiscRTE-owned runtime/system metadata and state
<user folders>  user-owned/shared content such as documents, books, images, etc.
```

Consequences:

- Package install roots MUST NOT be moved beneath `/System`.
- User content MUST NOT be stored beneath `/System`.
- An app MUST NOT invent a new direct child of `/System`.
- New system data MUST use one of the defined semantic subtrees below.
- Unknown files under `/System` MUST NOT be deleted merely because the current
  firmware does not recognize them.
- A generated file under `/System` is never authority merely because it is in
  the system namespace. The owning subsystem defines its source of truth and
  validation rules.

## 2. Canonical hierarchy

The reserved hierarchy is:

```text
/System/
  Registry/
  State/
    Core/
    Applications/
    Services/
    Providers/
    Drivers/
  Config/
  Cache/
  Logs/
  Recovery/
```

A subtree may be absent until first use. Code SHOULD create only the directories
it actually needs.

Names beneath owner-class directories SHOULD use the stable package/component ID,
not display names. IDs remain stable across version updates.

### 2.1 `/System/Registry/`

Purpose: **generated lookup/index material derived from authoritative installed
or runtime state**.

Properties:

- RiscRTE owns creation and mutation.
- Files MUST be reproducible from an identified source of truth.
- A registry MUST NOT become the only copy of user data, package identity,
  permissions or durable configuration.
- Writers SHOULD use atomic temporary/rename publication.
- On interrupted/corrupt/stale publication, the owning subsystem SHOULD rebuild
  rather than trusting stale content.
- If a rebuild cannot safely publish a fresh registry, code SHOULD prefer no
  registry over a knowingly stale registry when stale entries could advertise
  unavailable or incorrect resources.

Current file:

```text
/System/Registry/FileAssociations.json
```

Its source of truth is verified installed application metadata plus built-in
RiscRTE handlers. Install/update/uninstall changes rebuild it. The file is a
derived acceleration/introspection artifact, not package metadata and not an
authorization grant.

### 2.2 `/System/State/`

Purpose: **runtime/component state that may survive process restart or reboot but
is not user-authored content and is not package payload**.

State is namespaced first by owner class, then stable owner ID:

```text
/System/State/Core/<component-id>/
/System/State/Applications/<app-id>/
/System/State/Services/<service-id>/
/System/State/Providers/<provider-id>/
/System/State/Drivers/<driver-id>/
```

Examples:

```text
/System/State/Applications/file_browser/Session.txt
/System/State/Core/<component-id>/...
```

Rules:

- State for one owner MUST NOT be written into another owner's directory.
- Display names MUST NOT be used as persistent identity.
- State MAY be durable across reboot when needed, but deleting it must not
  destroy user documents or installed package generations.
- Session/handoff/restart-resume data belongs here.
- State that is only a performance optimization belongs in `Cache`, not
  `State`.
- Security credentials/secrets MUST NOT be placed here merely because the path
  is system-owned; use the platform's credential/secret mechanism when one is
  defined.
- Future application-private storage APIs MAY map suitable private state beneath
  this hierarchy, but applications SHOULD receive scoped storage access rather
  than broad `/System` filesystem authority.

### 2.3 `/System/Config/`

Purpose: **RiscRTE-owned durable system configuration** whose loss changes
platform behavior rather than merely forcing regeneration.

Rules:

- Configuration must have an explicit schema/version or a bounded parser with
  defined defaults.
- Updates SHOULD be atomic.
- Package manifests MUST NOT write directly into this subtree.
- Application preferences belong in application-private storage unless they are
  explicitly platform-owned settings.
- Credentials and cryptographic secrets require their own secure storage policy;
  `Config` is not automatically a secret store.

This subtree is reserved even where current settings still use legacy storage.

### 2.4 `/System/Cache/`

Purpose: **fully disposable derived data** used only to improve performance.

Rules:

- Deleting cache MUST preserve correctness.
- Cache MUST be reconstructible.
- Cache entries SHOULD be bounded or quota-capable.
- Cache MUST NOT be consulted as authority when its source of truth is absent or
  invalid.

Where ownership matters, use:

```text
/System/Cache/Applications/<app-id>/
/System/Cache/Core/<component-id>/
```

### 2.5 `/System/Logs/`

Purpose: **diagnostic records** produced by RiscRTE or installed components under
platform policy.

Rules:

- Logs are diagnostic, not configuration or package state.
- Logging SHOULD be bounded by retention/rotation policy.
- Sensitive material SHOULD be redacted or excluded.
- Per-owner logs SHOULD use stable IDs.

This subtree does not grant arbitrary applications permission to log unlimited
data.

### 2.6 `/System/Recovery/`

Purpose: **platform-owned recovery journals/checkpoints that coordinate
multi-step system mutations** when that state cannot safely live beside the
transaction target.

Rules:

- Recovery data has a defined owner and transaction/version schema.
- Recovery processing MUST be idempotent.
- Recovery files MUST NOT be treated as installed package inventory by
  themselves.
- Successful completion SHOULD remove obsolete recovery state.
- Package-local stage/backup paths that are already transactionally bound to a
  package root may remain beside that package; they do not need to be moved here
  solely for directory aesthetics.

## 3. Path and naming rules

New `/System` paths SHALL:

- use forward slashes;
- contain no `.` or `..` path components;
- use stable machine identity for owner directories;
- keep display/presentation labels out of persistent path identity;
- use a bounded path length suitable for the platform storage API;
- avoid case-only aliases;
- define whether a file is authoritative, derived, transient or disposable.

Generated registry/config/state filenames SHOULD be descriptive and stable.
Temporary and backup files SHOULD remain in the same owning directory when
atomic rename semantics depend on that locality.

## 4. Ownership and API boundary

`/System` being visible on the SD card does **not** mean every ELF receives
unrestricted write authority.

Target architecture:

- core/runtime code writes core-owned system files;
- package/runtime managers update their own generated metadata;
- applications/services/providers/drivers receive scoped access to their own
  state/private storage when such APIs are available;
- trusted RiscRTE UI/services mediate shared file selection and handoff;
- direct broad filesystem access is legacy compatibility, not the design target.

A component MUST NOT edit another component's system state to communicate.
Cross-component communication uses defined APIs, intents, streams, events,
handles or registries.

## 5. Lifecycle semantics

### Install

Installing an application MAY cause RiscRTE to derive or refresh system
registries from its verified manifest. Installation MUST NOT copy package
payloads into `/System` as an alternate install root.

### Update

Updating a package preserves stable owner identity. Derived registries MUST
reflect the newly committed generation. Owner state SHOULD survive an update
unless its own schema migration explicitly changes it.

### Uninstall

Uninstall MUST remove the package generation and update derived registries so
they no longer advertise that package.

Application-private state removal is a separate policy decision. It MUST NOT be
silently conflated with uninstall unless the user or governing package/storage
policy explicitly requests data removal.

### Boot and recovery

RiscRTE MAY lazily rebuild derived registries from authoritative installed state.
Stale/corrupt generated registries SHOULD be replaceable without damaging
packages or user content.

## 6. Legacy migration

`/.crosspoint` is obsolete.

New code MUST NOT create new `/.crosspoint` paths.

When legacy state remains on deployed cards:

- migrate only known, validated files with an explicit destination;
- make migration idempotent;
- do not recursively delete unknown legacy content;
- prefer copy/validate/publish/remove sequencing where data loss is possible;
- leave unknown legacy files untouched and diagnosable.

Current File Browser state and file-association registry use only the RiscRTE
`/System` hierarchy.

## 7. Current allocations

| Path | Class | Owner | Semantics |
| --- | --- | --- | --- |
| `/System/Registry/FileAssociations.json` | Registry | RiscRTE file-open subsystem | Derived mapping from normalized file type to available handler; regenerable |
| `/System/State/Applications/file_browser/Session.txt` | State | File Browser app/runtime handoff | Transient resume/delete/handoff state; not user content |

New allocations SHOULD be added to this table or to a child specification that
links back here.
