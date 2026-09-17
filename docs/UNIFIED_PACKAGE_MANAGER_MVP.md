# Unified Package Manager MVP — architecture and acceptance

**Status:** Priority 3 implementation in draft PR #76. Shared identity, transport-independent metadata preflight, numeric version policy, and the driver's directory transaction are implemented as separate foundation slices. **The preflight helper is not yet connected to a package decoder/installer.** The common archive, authenticated signatures, atomic App Store migration, offline SD install and service/provider deployment are **not implemented**. No currently installed legacy package is assumed trusted.

Authority: [RiscRTE Platform Specification](RISCRTE_PLATFORM_SPEC.md) → [Platform Capability Roadmap](PLATFORM_CAPABILITY_ROADMAP.md), especially §§26–28 and §39 → [Runtime Driver Architecture](RUNTIME_DRIVER_ARCHITECTURE.md), [Application Execution Context Architecture](APPLICATION_EXECUTION_CONTEXT_ARCHITECTURE.md), and [Security Architecture](SECURITY_ARCHITECTURE.md). This document tracks the MVP and its migration, not a replacement architecture.

## Target invariant

One runtime-owned package manager validates and installs **applications, drivers, services, and providers** without executing payloads. One versioned package envelope binds manifest, content hashes, executable architecture/ABI, dependencies, capabilities, permission declarations, and an authenticated signature to an explicit package identity. Package installation never constitutes device permission or module activation. The same envelope is installable from removable SD storage or an online catalog; interrupted updates preserve a verified executable generation. SHA-256 validates bytes against a digest, but it cannot authenticate a mutable manifest. Hardware protocol code belongs inside the driver ELF; the runtime supplies generic, capability-gated resources and execution-context ownership, not a hardware-specific proxy implementation.

## Implemented foundation

### Identity (`src/runtime/packages/PackageIdentity.h`)

- `Kind` is one of `Application`, `Driver`, `Service`, `Provider`; `(kind,id)` is the package namespace.
- IDs are bounded lowercase alphanumeric slugs with *internal* `-` or `_`, maximum 63 bytes, no separators, traversal, or trailing punctuation.
- Versions are bounded numeric `major.minor.patch`; every component must fit `uint32_t`. They identify a package candidate, not a runtime/API compatibility claim.
- An artifact is a bounded `.elf` basename, maximum 127 bytes; paths and traversal are rejected. An invalid kind is rejected explicitly.
- New manifests MUST explicitly bind kind, ID, version, artifact. Existing untyped application sidecars may infer ID from the ELF basename and omit version for compatibility only; no equivalent exemption applies to drivers/services/providers or new signed envelopes.
- `makeIdentity` clears output on failure. Neither identity nor a declared capability provides authority to access hardware or load code.

`src/native/AppManifest.cpp` accepts optional canonical `type: "application"` and `id` alongside compatible legacy sidecars. `src/runtime/drivers/DriverPackage.cpp` uses the common identity check in addition to the existing driver ABI, architecture, ELF header, size, SHA-256 and capability validation. These remain *legacy-specific* validators, not signature verification.

### Bounded metadata preflight (`src/runtime/packages/PackagePreflight.h`)

- `PackageEnvelopeView` is a borrowed, typed view for a **future bounded decoder**, not an archive reader. Its caller must independently reject malformed/duplicate JSON keys and oversized raw inputs. No bytes are installed or executed by this helper.
- One explicitly named ELF, at most 16 entries and 16 declared requirements; each entry requires a bounded safe basename, nonzero size and syntactically valid lowercase SHA-256 digest. Duplicate filenames, hidden second `.elf` entries, traversal and aggregate/individual storage-budget overruns are rejected.
- Explicit architecture, runtime API floor and monotonically enforced security-version floor are checked. Dependency declarations target named capabilities plus minimum ABI/API; a read-only resolver reports availability, not grants or leases. All declarations are validated before lookup.
- `comparePackageVersions` compares integer tuples, rather than lexicographic strings. `decidePackageVersion` distinguishes fresh install, upgrade, same version, legacy migration, identity mismatch and downgrade refusal; downgrade requires an explicit caller policy. This policy is **not yet invoked by the App Store or Driver Manager**.
- Successful metadata preflight is named `ReadyForContentVerification`: the caller still MUST hash every content entry, verify an authorized signature over canonical manifest and digest data, pin bytes against verification-to-load substitution, and obtain user consent separately before publish/activation. No signature verifier exists in this PR.

### Directory transaction (`src/runtime/packages/PackageTransaction.h`)

The driver installer stages both its ELF and manifest together in `/Drivers/.<id>.install`, verifies the pair, renames the previous target to `/Drivers/.<id>.previous`, then publishes the staged directory. The common coordinator restores a complete verified backup after interrupted rename and preserves unknown/corrupt targets for manual inspection. A specific recovery bug is fixed: cleanup of the *old* backup may be interrupted after one known file is deleted. If the newly published target verifies, recovery now retries the managed-only purge without requiring the partial backup to verify. If the target is absent, the backup must verify fully before restoration. An unmanaged backup cannot be purged.

The caller still owns the loaded-module pin/replacement gate; the current driver install call passes `replaceAllowed=true`, so generic in-use replacement protection is **not implemented**. A host fault-injection test covers partially completed cleanup and unknown/corrupt paths; it does not establish power-loss durability on physical media or authenticate unsigned content.

## Current separate installers and known risks

App Store (`src/native/NativeAppHost.cpp`) downloads `.elf` and `.json` separately to `.part` files under `/Apps`, checks release asset length and manifest compatibility, then uses two independent `.bak` swaps. Its current interruption handler may remove an installed destination before establishing a complete, verified backup pair. **Do not claim crash-safe App Store rollback.** A manifest and size check is not a cryptographic binding of an executable to its publisher. A safe migration must either move applications into an atomic single-directory layout with launcher compatibility or implement a recoverable pair state machine with full content binding; it must also reject replacement while a module is mapped.

Driver Manager (`src/native/NativeDriverManagerBridge.cpp` and `src/runtime/drivers/DriverPackage.cpp`) discovers driver catalog entries and validates ELF SHA-256 plus ABI. Availability, installation, device binding and activation remain separate. Its transaction is single-directory, but the generic pin gate and authenticated publisher trust are still outstanding.

`/sd` is a read-only ELF VFS: all mutation uses `HalStorage` paths. A package decoder must never accept internal destination paths from an untrusted manifest. The runtime must remain hardware-agnostic beyond generic resources and capability interfaces.

## Remaining implementation order within PR #76

1. **Common package decoder:** define a bounded canonical `package.risc` layout and schema. Detect duplicate JSON keys, duplicate physical filenames including filesystem aliases, archive bombs, unknown fields/entries, unsupported ABI and unsigned entry substitution before mutation. Map decoded metadata to `PackageEnvelopeView`; do not treat the view as a signature.
2. **Authenticated content/trust:** verify all payload hashes and an allowlisted signing key over canonical metadata, version and digests. Separate unsigned development/legacy policy from signed release policy, enforce security-version rollback resistance and protect verified bytes until loading.
3. **Unified transactional installer:** migrate App Store and Driver Manager to the same signed stage/verify/publish/recover implementation. Add execution-context/module pin guards, install/update/uninstall/quarantine/inventory, storage preflight, and power-cut fault injection at every transition. Preserve last verified generation and refuse unmanaged directories.
4. **Offline/online parity:** SD and network supply identical package bytes to the same policy; implement the four module kinds and ABI/dependency resolution without loading candidates. Recovery Wi-Fi and serial remain built in.
5. **Platform-owned UI:** show identity, source, independently verified trust, requested capabilities, storage use and version changes; consent must not be delegated to arbitrary application paths.

## Acceptance gates

Host: identity and version boundaries; all four kinds; traversal/case-collision names, duplicate keys, false hashes/signatures, ABI/security floor, missing capabilities, stale dependencies, insufficient storage, unmanaged directory refusal, loaded-module replacement refusal, interrupted operations at every rename and partial deletion; install/upgrade/downgrade/reinstall/uninstall; no executable inspection through ELF loading. Compile both board targets and validate app catalog/driver packages. Physical SD: offline install and power-cut recovery with old known-good app and driver retained, and built-in recovery tools operational. **Metadata preflight and host transaction success cannot be treated as signature, package-archive, complete installer, or device acceptance.**

**Release gate:** Keep PR #76 draft, unmerged and unreleased until its claimed implementation subset passes CI. Track outstanding gates explicitly instead of labeling the foundation a completed Unified Package Manager MVP.
