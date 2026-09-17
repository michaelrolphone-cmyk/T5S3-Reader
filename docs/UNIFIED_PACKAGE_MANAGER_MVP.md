# Unified Package Manager MVP — architecture and acceptance

**Status:** Priority 3, draft PR #76. The legacy online App Store uses a digest-aware ELF/JSON pair transaction. Driver packages use a shared directory transaction with a runtime-owned module mapping/replacement gate. Shared identity, guarded JSON, metadata preflight, version decisions, application release hash stamping, and bounded boot-time application recovery exist. **This is transitional, not the completed Unified Package Manager.** The `package.risc` decoder, authenticated publisher signatures/key policy, offline SD installer, unified four-kind lifecycle and verified-byte pinning remain outstanding. Legacy unsigned artifacts are never publisher-authenticated.

Authority: [RiscRTE Platform Specification](RISCRTE_PLATFORM_SPEC.md) → [Platform Capability Roadmap](PLATFORM_CAPABILITY_ROADMAP.md), §§26–28 and §39 → [Runtime Driver Architecture](RUNTIME_DRIVER_ARCHITECTURE.md), [Application Execution Context Architecture](APPLICATION_EXECUTION_CONTEXT_ARCHITECTURE.md), and [Security Architecture](SECURITY_ARCHITECTURE.md). This document tracks implementation and acceptance without redefining the roadmap.

## Target invariant

One runtime-owned manager validates and installs **applications, drivers, services and providers** without loading candidate payloads. A versioned envelope binds identity, all entry sizes/hashes, architecture/ABI, dependencies, capability declarations, security version and an authenticated publisher signature. SD and online distribution use the same envelope and installer. Interrupted updates retain a verified generation; installation grants no hardware access or activation. SHA-256 checks bytes against a declared digest but cannot authenticate a mutable manifest. Hardware protocols reside in driver ELFs; the runtime exposes generic capability-gated resources.

## Implemented

### Shared identity, guarded manifests and preflight

- `src/runtime/packages/PackageIdentity.h`: four kinds; `(kind,id)` namespace; bounded IDs and numeric `major.minor.patch` versions; rejects unsafe names, traversal, version overflow and trailing ID punctuation. Untyped/versionless application sidecars remain readable as untrusted legacy artifacts.
- `src/runtime/packages/PackageJsonGuard.h`: bounded allocation-free JSON syntax and duplicate-key guard, including escaped aliases and malformed escapes, executed before ArduinoJson in both legacy manifest parsers. Does not canonicalize signatures or parse archives.
- `src/runtime/packages/PackagePreflight.h`: typed borrowed metadata, bounded entries/requirements, declared hashes/budgets, architecture/runtime/security floors, capability/API availability and numeric version policy. `ReadyForContentVerification` does **not** mean content or publisher was verified. A common decoder is still required.
- Driver manifests validate ABI, ELF header, SHA-256 and capability declarations. Installed-driver version lookup hashes the actual ELF, not just the sidecar.

### Release construction and legacy App Store integration

- `scripts/package_integrity.py` and `scripts/build_all_apps.py` record built ELF size and SHA-256 in sidecars/catalog; reject inconsistent metadata and overlarge manifests/catalogs. `src/native/AppManifest.cpp` validates both fields together while continuing to parse older digestless sidecars.
- `src/native/AppPackageInstaller.cpp` validates filename, ELF header, length and declared hash, requiring both size/digest for newly staged downloads. A legacy digestless installed pair is checked only for basic filename/header/length consistency during migration; it is neither cryptographically verified nor authenticated.
- `src/runtime/packages/PackagePairTransaction.h` stages flat `/Apps/<name>.elf` and `.json` with `.part`/`.bak` recovery instead of assuming two renames are atomic. It verifies an available old/new generation and retains ambiguous or corrupt files for inspection.
- `src/native/NativeAppHost.cpp` recovers managed pairs before installs/launch, validates download lengths and hashes before publication, validates installed pairs at launch/version lookup, rejects numeric downgrades and refuses replacing the exact active app ELF.
- `src/native/AppPackageRecoveryInventory.cpp` and `src/runtime/packages/PackageAppRecoveryIndex.h` enumerate a bounded set of recognizable managed pairs, close the directory before modifying entries, and recover before springboard existence checks or installed-app enumeration. A mapped application with a backup is deferred rather than renamed; loose unmanaged files remain untouched. This is code plus host tests, not physical reset/power-cut acceptance.
- Host ASan/UBSan transaction fault tests, recovery-index tests and live App Store wiring tests run in CI.

### Driver transaction and in-use exclusion

- `src/runtime/packages/PackageTransaction.h` stages a driver directory, verifies before publication, retains the previous generation in `.previous`, and supports recovery of interrupted renames/managed cleanup without deleting unmanaged data.
- `src/runtime/packages/PackageUseGate.h` is a bounded, runtime-owned registry keyed by package directory. Mapping pins and exclusive replacement reservations share a mutex. A pinned driver blocks both publication and any recovery requiring a rename. A replacement reservation blocks a concurrent loader until transaction verification, renames and cleanup finish; read-only recovery/version checks with no backup remain possible while mapped.
- `GpsDriverModule.cpp` and `UsbCdcDriverModule.cpp` acquire pins before `dlopen` and release them only after successful `dlclose`. If `dlclose` retains the handle, the package remains pinned. The legacy driver installer's `replaceAllowed=true` flag is now additionally constrained by the transaction's mandatory runtime reservation; that flag alone cannot bypass a pin.
- `test/resources/package_use_gate_test.cpp` exercises mapping, replacement, rollback, invalid identities, capacity and refusal to pin during verification. It is wired into `test/run_springboard_test.sh` with ASan/UBSan. Other future module loaders must register pins before they can be considered covered by this gate.

## Outstanding milestones (implementation priority)

1. **Canonical package and trust policy.** Define bounded `package.risc` schema and signing bytes; reject duplicate fields/filenames including FAT aliases, unsupported fields, hidden executables, oversized/decompression-bomb entries and traversal. Verify all entries and an allowlisted publisher key before installation. Security-version floors must persist independently of a mutable SD manifest. A legacy SHA-256 sidecar is not an authenticated signature.
2. **Verified-byte lifetime.** Pin or copy authenticated ELF bytes from verification through load, so removable SD contents cannot be swapped between check and `dlopen`. The current package-use registry protects against installer renames, **not** hostile or external SD modifications, and is not a general all-ELF loader registry yet.
3. **One installer for both distribution paths.** Connect decoder, metadata preflight, dependency resolver, storage budgets, signature checks and staging to the same on-device installer for SD and online. Do not run candidate code during discovery; install grants no hardware permissions.
4. **Complete four-kind lifecycle.** Driver/service/provider/app activation separation, inventory, update, uninstall/quarantine, version/permissions/trust UI, fallback serial/Wi-Fi recovery and explicit unsigned development policy distinct from signed release policy. All current/future ELF loading paths and context leases must honor in-use replacement exclusion.
5. **Physical acceptance.** Exercise both boards and real SD media: online/offline installs, power cuts at transaction boundaries, preserving the last usable app/driver, recovery to springboard, in-use replacement refusal and recovery UI. No physical power-cut test has been reported.

`/sd` remains a read-only ELF VFS: mutation uses `HalStorage` and paths derived from validated identity. Package declarations never grant hardware rights; hardware-specific behavior belongs in driver ELFs.

## CI and release gate

The previously integrated App Store changes passed firmware and host CI at `93497fc`. The driver pin/reservation work began at `d806030` and is pending its own full CI result and physical validation at the time of this documentation update. Do not infer acceptance from a previous commit's green check. Keep PR #76 draft, unmerged and unreleased until its claimed scope and required gates pass.
