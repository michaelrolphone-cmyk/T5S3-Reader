# RiscRTE Application Execution Context Architecture

## Status and authority

This specification is authoritative for RiscRTE application execution, application-owned resources, application storage, trusted system UI mediation, and application manifest resource declarations.

It is subordinate to [RISCRTE_PLATFORM_SPEC.md](RISCRTE_PLATFORM_SPEC.md) and [PLATFORM_CAPABILITY_ROADMAP.md](PLATFORM_CAPABILITY_ROADMAP.md). Current ELF ABI details remain documented in [NATIVE_APPS.md](NATIVE_APPS.md).

## Priority

**Trusted system UI mediation and private application storage are high-priority requirements for all new application work.**

New applications MUST NOT introduce a new unrestricted file browser, credential picker, device picker, permission prompt, Wi-Fi picker, or equivalent security-sensitive selection UI when the operation can be mediated by RiscRTE. New applications SHOULD store application-private state in an application-private namespace rather than inventing paths in shared SD storage.

Where the required trusted picker or private-storage API is not implemented yet, new app work SHOULD add the smallest reusable platform primitive needed rather than embedding an app-private substitute. Existing applications may retain legacy behavior until migrated, but that behavior is not precedent for new work.

## 1. Execution context

Every launched RiscRTE application SHALL have a first-class **execution context** distinct from the ELF image itself.

The execution context is the runtime identity to which resources are charged and from which they are reclaimed. It SHOULD contain or reference:

```text
package/application identity
invocation identity
authorized capability leases
object/handle table
stream and pipe ownership
device/resource leases
memory accounting and quotas
private storage namespace
jobs/workers
UI/session ownership
IPC/event subscriptions
lifecycle state
```

Resources belong to the execution context, not merely to the code that requested them.

### 1.1 Lifecycle

At minimum:

```text
CREATED -> STARTING -> RUNNING -> STOPPING -> TERMINATED
```

Termination SHALL deterministically revoke/release context-owned resources. Conceptually:

```text
revoke capability leases
cancel jobs/workers
close pipes and streams
release devices and bus/resource leases
close files and IPC endpoints
remove event subscriptions
release UI/session ownership
reclaim tracked memory/mappings
unload ELF
```

Cleanup MUST be idempotent where practical. Partial startup and abnormal termination MUST use the same ownership model.

No persistent runtime state may retain a raw pointer into an unloadable application ELF.

## 2. Universal opaque object handles

RiscRTE SHOULD converge on a common opaque-handle model for resources crossing the application/runtime boundary.

The common model SHOULD support:

```text
slot/object identity
generation
object type
owner execution context
rights/access mask
implementation reference
destructor/revocation behavior
```

A stale generation MUST fail rather than resolving to a newly allocated object in a reused slot. An application MUST NOT be able to operate on another execution context's object unless an explicit sharing/transfer mechanism grants that authority.

Streams, devices, files/resources, jobs, services, timers, IPC endpoints, credentials, UI grants, and future platform objects SHOULD reuse this lifecycle machinery rather than implementing unrelated ownership schemes.

Existing subsystem-specific handles remain valid current implementation until migrated.

## 3. Trusted system UI mediation — HIGH PRIORITY

Security-sensitive resource selection SHOULD be performed by RiscRTE-owned UI, not by arbitrary application UI with broad underlying access.

Canonical examples:

```text
request file open/save
request directory/resource access
request permission/capability grant
request device selection
request Wi-Fi/network selection
request credential use
request application/open-with target
```

The application requests an operation and constraints; RiscRTE presents trusted UI and returns only the scoped result/handle the user authorized.

Example:

```text
Application
    |
    | request open(image/*)
    v
RiscRTE trusted picker
    |
    | authorized resource handle
    v
Application
```

An image application therefore SHOULD NOT require unrestricted filesystem enumeration merely to let a user choose one image.

### 3.1 Trusted UI invariants

- RiscRTE owns the visual identity and interaction for security-sensitive prompts/pickers.
- Applications cannot forge a platform permission grant by drawing a similar screen.
- The returned authority is scoped to the selected operation/resource wherever practical.
- Cancellation returns no authority.
- Grants/handles belong to the requesting execution context and are reclaimed with it unless explicitly persistent.
- Large resources are passed by handle/reference/stream, not copied through UI messages.
- Picker implementations use intents, content types, Device Registry, Credential Vault, and capability resolution as those facilities become available.

### 3.2 New-app rule

For new application development, trusted system pickers and mediated resource access take precedence over adding broad directory, credential, network, or device-enumeration APIs to the app ABI.

If a picker is missing, implement the reusable picker/intent/platform API first when feasible.

## 4. Private application storage — HIGH PRIORITY

Each installed application SHOULD have a runtime-defined private persistent storage namespace keyed by stable package identity.

Applications SHOULD address this through a logical namespace/API such as:

```text
app://data/...
```

The physical backing path is an implementation detail and MUST NOT be treated as part of the application ABI. A current implementation may map package-private storage under SD/internal storage, for example an `AppData/<package-id>/` hierarchy, while preserving the logical API.

### 4.1 Storage classes

RiscRTE SHOULD distinguish:

```text
private persistent application data
private cache/temporary data
shared/user-selected resources
system/platform data
removable/external volumes
```

Private application data is available to its owning package without granting arbitrary shared-volume traversal. Shared resources SHOULD normally enter the application through a trusted picker, intent, explicit share, or scoped resource handle.

### 4.2 Storage invariants

- Package identity, not display name or ELF filename alone, determines the private namespace.
- Applications cannot traverse into another package's private namespace through the public application API.
- Uninstall/update policy MUST define whether private data is preserved, migrated, or removed.
- Atomic replacement and streaming APIs SHOULD be available without requiring whole-file RAM buffering.
- Storage use SHOULD participate in quotas/accounting.
- Removable-volume loss must invalidate or fail affected handles cleanly.

### 4.3 Migration from current `/sd` access

Current RiscRTE APIs expose broad `/sd` paths and directory enumeration. These remain documented compatibility behavior. They SHOULD NOT be expanded as the default model for new applications.

Migration direction:

```text
app-owned state        -> private application storage
user-selected content  -> trusted picker + scoped handle
large sequential data  -> stream
shared/exported data   -> explicit share/save intent
platform/system data   -> platform service/capability
```

## 5. Memory quotas and resource accounting

Every application execution context SHOULD have observable resource accounting. Memory accounting SHOULD distinguish scarce internal SRAM from PSRAM and other mapped/storage-backed classes where meaningful.

The runtime SHOULD be able to report per-context usage such as:

```text
SRAM / quota
PSRAM / quota
mapped/executable memory
streams/pipes
open resources/files
devices/capability leases
jobs/workers
subscriptions
```

Application manifests SHOULD evolve to declare resource requirements or limits. The loader/resolver may reject or defer launch when minimum requirements cannot be met safely.

Quotas are a reliability boundary even where the current ESP32-S3 cannot provide full process-style hardware isolation.

## 6. Manifest-assisted requirement derivation

RiscRTE build tooling SHOULD derive or validate application manifest requirements from SDK/API use wherever practical.

Examples include:

```text
capabilities used
permissions/scoped authorities required
host API/ABI requirements
minimum resource requirements
registered intents/content handlers
```

A build SHOULD fail or warn when application code uses a platform facility that is absent from its declared requirements, according to the facility's policy.

Generated metadata MUST remain reviewable and deterministic. Tooling inference supplements the manifest contract; it does not silently grant runtime authority.

## 7. Relationship to capability resolution

A capability describes **what functionality is available**. An execution context describes **who owns a running invocation and its resources**. A returned lease/handle describes **the particular authority/resource acquired by that context**.

These concepts MUST remain separate enough that future permission/security policy can constrain capability acquisition without changing capability discovery semantics.

## 8. Current implementation and migration

Current RiscRTE ELF applications are trusted ESP32-S3 machine code and are not isolated processes. `runNativeApp()`, `launch_elf_app()`, current `T5*` host API tables, broad `/sd` storage calls, and subsystem-specific owner/handle schemes are current compatibility implementation.

Useful existing foundations include one-ELF-at-a-time lifecycle, versioned host APIs, system UI handoff/resume, stream owner IDs/generation-safe handles, manifest validation, and host-owned services.

Migration SHOULD proceed incrementally:

1. create a runtime execution-context identity around every app invocation;
2. route new resource acquisition through that context;
3. generalize generation-safe opaque handles/object ownership;
4. prioritize trusted file/resource pickers and private application storage for new apps;
5. add per-context memory/resource accounting and quotas;
6. extend build tooling to validate/derive manifest requirements;
7. migrate legacy broad `/sd` and app-private selection flows without breaking deployed apps.

## 9. Required tests

The platform test suite SHOULD include adversarial lifecycle cases:

```text
use a handle after application unload
reuse a stale generation after slot reuse
attempt cross-context handle access
double release
terminate during active stream/network/USB/storage operation
terminate during trusted picker handoff
cancel a picker and verify no authority is granted
remove storage while scoped/private handles are active
exceed SRAM/PSRAM/resource quota
unload with jobs/subscriptions pending
partial launch followed by cleanup
```

These tests validate ownership and lifecycle invariants even without hardware-enforced process isolation.