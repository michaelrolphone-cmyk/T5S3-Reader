# Unified Package Manager MVP — architecture and acceptance

**Status:** Priority 3 implementation in draft PR #76. The shared identity/validation slice is implemented; the full package format, installer, signature verification, and service/provider module deployment are **not implemented**. No packages are assumed trusted by this document or current code.

Authority: [RiscRTE Platform Specification](RISCRTE_PLATFORM_SPEC.md) → [Platform Capability Roadmap](PLATFORM_CAPABILITY_ROADMAP.md), especially §§26–28 and §39 → [Runtime Driver Architecture](RUNTIME_DRIVER_ARCHITECTURE.md), [Application Execution Context Architecture](APPLICATION_EXECUTION_CONTEXT_ARCHITECTURE.md), and [Security Architecture](SECURITY_ARCHITECTURE.md). This is the detailed migration/MVP specification, not a replacement for the target architecture.

## Target invariant

One runtime-owned package manager validates and installs **applications, drivers, services, and providers** without executing payloads. One versioned package envelope binds manifest, content hashes, executable architecture/ABI, dependencies, capabilities, permission declarations, and an authenticated signature to an explicit package identity. Package installation never constitutes device permission or module activation. A user can install the same envelope from SD or an online catalog, and interrupted updates are recoverable without losing the last known-good package. The runtime cannot infer cryptographic trust from SHA-256 alone: a mutable manifest and ELF with a matching digest are still untrusted until an authenticated signature is checked against an authorized signing key.

## Identity and validation contract — implemented first slice

`src/runtime/packages/PackageIdentity.h` provides fixed-size firmware-only metadata:

- `Kind` is one of `Application`, `Driver`, `Service`, `Provider`. Type is part of the namespace; an application and driver with the same ID are distinct packages.
- `id` is an ASCII lowercase alphanumeric slug with internal `-` or `_`, maximum 63 bytes, no slash, dot, colon or traversal. It is not a display name, filename, origin or security principal.
- `version` is bounded numeric `major.minor.patch`, maximum 31 bytes. It is a package **candidate version**, not a runtime/API compatibility declaration. Comparisons must be numeric, not lexicographic; update/downgrade policy is not yet implemented.
- `artifact` is a single bounded `.elf` basename, maximum 127 bytes; no path, hidden file, `..` or separator. Its name does not authorize execution.
- A new manifest MUST explicitly bind kind, ID, version and artifact. Existing untyped application sidecars can derive their ID from the `.elf` basename and can omit a version for compatibility; this exception is never allowed for a new driver, service, provider or signed package. An explicitly empty ID is invalid.
- `makeIdentity` clears its result on failure. It checks metadata only; it never reads, maps, loads, launches, installs or grants rights to an ELF. `samePackage` compares `(kind,id)` independently of version.

`src/native/AppManifest.cpp` additionally accepts optional `type: "application"` and optional explicit `id` and validates identity after its existing app manifest, requirements and UI metadata checks. `src/runtime/drivers/DriverPackage.cpp` applies the same identity contract in addition to its existing driver ABI, architecture, SHA-256, ELF header, size and capability validation. The driver path remains allowlisted and installable only through the existing driver installer. Tests are wired into `test/run_driver_test.sh`.

**Security boundary:** The new shared check is neither signature verification nor a replacement for the existing manifest/ELF validators. A declaration of required capabilities is not a grant; use transient, firmware-owned consent and execution-context-owned leases for actual access. Treat every currently unsigned app or driver as unverified.

## Current separate installers — accurate legacy status

App Store (`src/native/NativeAppHost.cpp`) downloads app `.elf` and `.json` separately to `.part` paths under `/Apps`, verifies release asset length and manifest compatibility, and swaps files through separate `.bak` renames. This is not a signed, content-addressed envelope or a single atomic transaction over both files; interrupted swaps require recovery.

Driver Manager (`src/native/NativeDriverManagerBridge.cpp` and `src/runtime/drivers/DriverPackage.cpp`) discovers driver catalog entries, validates manifest/ELF SHA-256 and ABI, and stages a directory under `/Drivers/.<id>.install`. It renames an existing managed installation to `/Drivers/.<id>.previous` then publishes the stage. Driver availability, installation, binding and activation remain separate. Its current backup cleanup/recovery path must be audited for interruption between renames before declaring rollback complete.

`/sd` is a read-only ELF VFS; file mutation MUST go through `HalStorage` storage paths. A package manager must not introduce POSIX write/rename on `/sd` or allow untrusted ELFs to choose internal destination paths.

## Implementation milestones within this one independent PR

1. **Identity/validation:** completed first slice as specified above. Host test both typed/legacy behavior and hostile paths/versions, plus existing driver integrity cases. Preserve compatible legacy assets.
2. **Shared transaction coordinator:** centralize package install states (`downloaded`, `verified`, `staged`, `previous`, `active`, `quarantined`) and recover before deleting any last-known-good backup. Stage and verify payload plus manifest together before publishing. On every failed rename or reboot, preserve at least one validated usable version and refuse unknown/unmanaged files. Host-inject failures at each step. No active module is replaced while loaded.
3. **Common manifest/envelope:** define one bounded `package.risc` schema with explicit kind/ID/version/architecture/API/security version, complete entry list with sizes/hashes, dependency and capability declarations, and a signed canonical manifest/bundle identity. Reject duplicate JSON keys, duplicate filenames, traversal, archive bombs, unsupported ABI and unlisted entries before mutation. Multiple modules/resources are not accepted until a bounded decoder and layout are implemented.
4. **Authenticated trust:** verify an allowlisted signing key and signature over a canonical envelope including all content digests and security version. Clearly distinguish untrusted development/legacy install policy from authenticated release policy; SHA alone does not confer authenticity. Prevent executable-byte TOCTOU between verification and ELF load.
5. **One installer for four kinds:** offline SD and online catalog feed the same validation/staging/rollback implementation, never two different trusted paths. Resolve dependencies and ABI contracts without loading candidate ELFs; preflight disk/memory/ownership; install app, driver, service and provider; inventory/update/uninstall/quarantine. Keep recovery Wi-Fi and serial functionality compiled in.
6. **Platform UI and storage:** a firmware-owned install consent/details surface lists package identity, origin/trust, requested capability use, version differences and storage budget. Privileged package mutation cannot be driven by arbitrary app paths. Private package storage and trusted resource pickers follow their authoritative specs.

## Acceptance gates

Host: cross-kind identity isolation; malicious/traversal filenames, malformed versions, duplicate keys, false content digests/signatures, wrong architecture/ABI/security version, unknown capabilities, stale dependencies, insufficient space, abrupt interruption after every rename and recovery; managed/unmanaged directory confusion; reinstall/upgrade/downgrade/uninstall; no ELF execution during inspection. Compile both board targets and verify app catalog and driver packages. On physical SD: successful offline install and power-cut recovery, no loss of last working app/driver, and recovery tools still operational. Signing and package-envelope acceptance cannot be marked complete while the signature verifier or on-device installer is absent.

**Current release gate:** PR #76 stays draft, unmerged and unreleased until the claimed subset has passed CI. Do not describe an identity-validator success as a complete Unified Package Manager MVP.
