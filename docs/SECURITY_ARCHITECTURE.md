# RiscRTE integrity, authorization and native-code safety

## Scope and precedence

This document follows [Package Manager Scope](PACKAGE_MANAGER_SCOPE_CONTRACT.md),
[U1](NEXT_HARDWARE_TEST_MILESTONE.md), [Provider Admission and Lifetime](PROVIDER_GRAPH_ADMISSION_LIFETIME.md)
and the [Hardware Boundary](HARDWARE_AGNOSTIC_DRIVER_BOUNDARY.md).
The removed package-authentication experiment is not an optional execution mode,
release prerequisite or future implementation plan. Its history remains in git.
This change does not configure secure boot, eFuses, flash encryption, credentials,
network security settings or any production device.

## Untrusted storage and inputs

Removable media and network metadata may be changed together with executable
bytes. Treat manifests, resources, archives, external peripheral input and native
ELF files as hostile input. Presence beneath `/Apps` or `/Drivers` is not authority.
A self-declared SHA-256 detects inconsistency with that declaration; an attacker
who changes both bytes and the declaration is not thereby excluded. No publisher
identity or memory sandbox is implied by package integrity.

Retain bounded JSON/ZIP parsing, duplicate/alias/path rejection, size/count/depth
limits, overflow checks, CRC and content SHA-256, ELF architecture/format checks,
exact import declarations and independent runtime permission decisions. Reject
unknown required formats rather than guessing a compatible representation.
Legacy unsupported files and user-owned directories are preserved, not deleted
by a format rejection or a repository-source cleanup.

## Installation and execution are separate

1. Inspect source identity, numeric version, CPU/ABI/runtime and dependencies
2. Verify downloaded/staged bytes and every declared entry without executing it
3. Publish one recoverable per-ID directory generation under the use gate
4. At load, obtain manager-owned metadata and exact private candidate ELF bytes
5. Independently validate allowed ABI/imports, relocation and capability policy
6. Map/activate only after admission; preserve dependency and mapping pins
7. Revoke handles and quiesce physical callbacks/IRQ/DMA before release/unmap

No constructor, entry point or module-controlled callback runs during metadata
inspection. A valid manifest, digest or caller-filled `SpecV2` is not permission
to use privileged imports or hardware. Private manager admission remains separate
from ordinary caller-accessible graph registration and from consumer leases.

The immutable private executable image is the one passed to relocation. Do not
validate one SD pathname and later execute independently reopened, replaceable
bytes. The existing temporary installed-generation hashing bypass and permanent
receipt/invalidation requirements remain governed by
[ELF Verification Performance](U1_ELF_LOAD_VERIFICATION_PERFORMANCE.md); metadata-
only hot-path inspection must not be described as proof of unchanged content.
Install/update/recovery and explicit integrity checks continue to verify bytes.

## Capabilities, ownership and hardware boundaries

Applications consume versioned capabilities and scoped handles, not driver file
names, GPIO wiring or controller details. The core validates owner, rights,
generation, buffers and lifetime. Installed providers implement hardware behavior
and enforce physical suitability and resource ownership. The narrowly documented
bus-only transitional imports do not permit a peripheral proxy into firmware.

A successful package install does not start hardware. Dependencies do not confer
user consent. READ and WRITE rights remain independent; revocation stops scheduling
and invalidates downstream handles. Protected data cannot be copied into an
unprotected sink; unsupported retention/purge semantics fail closed.

Shared chip/register/rail ownership and exact failed-release grants remain pinned
until safe cleanup is established. Do not force-unload an uncertain physical
provider or grant a replacement while its predecessor may still own hardware.

## Native-code isolation limits

Native ESP ELF code executes within the firmware address-space constraints.
Capability tables are useful enforcement and architectural boundaries, but are
not hardware-enforced process isolation. A native memory-safety defect can corrupt
system state. Keep privileged code and injected host ABIs narrow, validate pointer/
buffer contracts, minimize firmware-internal exports, track resources, and retain
module code while any callback or I/O can still reference it. Fuzzing, static
analysis and production-facing failure tests remain useful independent safeguards.

## Availability, recovery and diagnostics

Operations need explicit memory/item/byte and elapsed-time bounds, finite retry,
real scheduler cooperation, cancellation and observable progress. Never hold a
global stream lock across blocking provider/file/network I/O. A timeout preserves
the last-good installed generation or a diagnosed recovery stage, not mixed files.

Distinguish corruption, incompatible format/CPU/ABI, dependency/version conflicts,
permission denial, malformed imports, loader failure, unavailable storage/network
and uncertain physical cleanup. Log bounded context without credentials or other
secrets. Recovery only cleans validated manager-owned paths; it never assumes
unknown user data can be removed.

## Networking, secrets and product security

TLS, certificate validation, general-purpose cryptographic capabilities and
OS/device credential protection are separate from the removed package subsystem.
They are retained; package hashes do not replace secure transport. Existing
network verification limitations must be reported honestly and addressed within
an appropriately scoped transport task.

Do not put secrets in ordinary SD files or logs. Hardware secure boot, flash
encryption, debug restrictions, eFuse policy and production manufacturing/service
recovery need a separately authorized, device-specific provisioning procedure.
This U1 source cleanup does not activate or weaken any of those device settings.

## Required software evidence

Keep checks for malformed/oversized metadata, traversal and aliasing, altered
payload rejection, exact ELF/import/ABI validation, caller-buffer mutation after
admission, forged privileged registration refusal, numeric downgrade refusal,
in-use replacement, interrupted publication/rollback, failed quiescence and stale
context/generation handles. The same ordinary engine handles four package kinds
and both online/offline sources. Hardware claims require actual owner-controlled
qualification, not merely a host test or successful module build.
