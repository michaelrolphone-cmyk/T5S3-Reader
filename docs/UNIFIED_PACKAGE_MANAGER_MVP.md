# Unified Package Manager MVP — scope, implementation and acceptance

**Status:** Draft PR #76. **Normative scope correction:** [UNIFIED_PACKAGE_MANAGER_SCOPE_CORRECTION.md](UNIFIED_PACKAGE_MANAGER_SCOPE_CORRECTION.md), aligned with the [master package scope contract](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/master/docs/PACKAGE_MANAGER_SCOPE_CONTRACT.md), takes precedence over legacy signed-format requirements. User requested a unified package manager, **not** a cryptographically signed package ecosystem. The existing P-256 prototype is code already written, not an approved MVP objective or a prerequisite for installing/activating drivers. This edit changes specifications only; it does not claim that signature gates have been removed from source.

## Change contract — required scope

Objective: a shared manager for applications, drivers, services and providers; one install/update/uninstall/inventory/recovery engine for both offline SD and online downloads. Package inspection must validate type, ID, version, CPU/runtime ABI, dependency requirements, safe paths, bounded payloads and optional/declared SHA-256 **integrity**. A digest detects mismatch and does not authenticate a publisher or grant privileged access. Use recoverable staging and replacement, preserve prior working generations on failure, refuse replacement of active mapped ELFs, and expose useful failure reasons. Installing a package must not itself start hardware or issue capability grants; runtime loader/import controls and execution-context consent remain separate.

Explicitly DEFERRED and **not acceptance gates**: compulsory digital signatures, P-256, publisher identities/keys, allowlists, revocation, signed provenance, NVS cryptographic security-version floors, signed-only ELF admission, secure boot and publisher anti-rollback. Do not substitute another cryptographic trust feature without user authorization. Keep regular validation, version/compatibility policy and crash consistency. No signing tool or production key provisioning may be required to install an ordinary package.

Acceptance: the same source-independent API handles all four kinds, from SD and online input, including inspect, dependency handling, install, update, inventory, uninstall, startup/restart recovery and in-use refusal. Test valid/incompatible/corrupt packages, bounded reads, partial download, stage/rename interruptions, repeat update, removed package, and recovery on both board types. Do not claim CI as physical testing.

## Current implementation state — do not confuse with target

The branch contains the following existing work. This is a factual implementation inventory, **not a checklist of mandatory signed-MVP deliverables**:

- `PackageIdentity.h`, `PackageJsonGuard.h`, `PackagePreflight.h`: four-kind identity, bounded JSON fields, ABI/dependency and budget validation. Existing App Store and Driver Manager use separate legacy unsigned installers and have NOT been migrated into one manager.
- `package_integrity.py`, `build_all_apps.py`, `AppPackageInstaller.cpp`, `NativeAppHost.cpp`: ELF length/SHA-256 matching, version checks and recoverable app ELF/manifest updates. Legacy sidecars are unsigned.
- `PackagePairTransaction.h`, `AppPackageRecoveryInventory.cpp`, `PackageTransaction.h`, `PackageUseGate.h`: stage/backup recovery, directory publication, mapped-package replacement reservations. These do not prevent malicious external SD substitution, and do not yet cover every ELF loader.
- `PackageArchive.h`, `scripts/build_risc_package.py`, `PackageArchiveVerification.h`: experimental deterministic signed RISC-PKG framing, P-256 writer/verifier, SHA-256 checking and ELF inspection.
- `PackageTrustPolicy.h`, `PackageDeviceCrypto.*`, `PackageDeviceInspection.*`: experimental firmware signer scopes, key revocation and read-only signed inspection.
- `PackageArchiveStage.h`, `PackageDeviceStage.*`, `PackageArchiveExtract.h`, `PackageDeviceExtract.*`: experimental source-to-intake staging, signature reauthentication, digest checks and extraction; currently coupled to signed-prefix fingerprints and NVS-floor checks.
- `PackageSignedProvenance.h`, `PackageDeviceDirectory.*`: experimental retained `.risc-auth`, signed directory provenance, exact inventory and per-file verification; presence on SD does not make data trusted.
- `PackageSignedTransaction.h`, `PackageDevicePublication.*`, `PackageDeviceInstaller.*`: experimental signed four-kind transaction, backup restoration, staged publication and source-independent already-opened `FILE*` orchestration. Existing online/offline UI call sites do not invoke this common installer.
- `PackageSecurityFloor.h`, `PackageDeviceSecurityFloor.*`: experimental per-kind/ID NVS security floors. NVS is not a physical tamper-resistant counter.

The experimental signed publisher offers cooperative ordinary-crash recovery, but SD operations and NVS commit are not an atomic transaction. It lacks a durable authenticated install-intent journal and all-loader executable-byte lifetime binding. These are limitations of that experiment, NOT additional MVP gates or permission to add a signing system. Ensure the ordinary manager uses bounded exact-byte inputs, independent loader permission/import checking, appropriate mapping lifetime handling and recoverable file operations without requiring cryptographic publisher identity.

## Implementation work still required

1. Decouple the common package management path from signing-specific APIs, key enrollment, signed provenance and NVS security floors. Preserve reusable identity, SHA-256 integrity, staging, safe publication and recovery pieces. Either isolate the signed format as a future optional experiment or remove it after verifying no generic behavior is lost. The unsigned envelope may reuse compatible existing manifests/artifacts; do not force a breaking signed-format migration.
2. Wire actual offline SD and online App Store/Driver Manager call sites into ONE shared install engine, inventory and lifecycle. Remove duplicate transient installers only after parity. Do not require desktop-side Python or signing to install from SD.
3. Implement and test shared four-kind inventory/update/uninstall, dependency checks, rollback/recovery, active-package protections and activation separation.
4. Connect to PR #78 through bounded, privately owned validated metadata and the exact executable bytes. Independently enforce module-specific privileged relocation/import policy and runtime grants; a hash alone is not privilege. No signed receipt is required.
5. Run existing host/firmware suites and real-device acceptance, document outcomes. Earlier CI green for the signed prototype does not demonstrate the unsigned unified manager.

## Scope-drift prevention

Before adding a requirement or dependency, document the explicit user request it serves, whether it is REQUIRED, OPTIONAL or DEFERRED, and its acceptance test. Roadmap security work, cryptographic best practices and existing prototypes are not permission to make signing or new trust infrastructure mandatory. Any expansion of security/crypto policy, package format, cross-PR dependency or release gate requires explicit user approval. On discovering drift, correct specs and code separately, preserving independent useful work without weakening existing safety protections.

Hardware-specific implementations remain in installable driver ELFs; core management is generic and hardware-blind. Managed SD writes use HalStorage; `/sd` is the legacy read-only ELF VFS. Keep PR #76 draft until the corrected, unified MVP is implemented and verified; do not hold it for the out-of-scope signed-package program.
