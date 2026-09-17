# Unified Package Manager MVP — implementation and acceptance

**Status:** Roadmap priority 3, draft PR #76. Legacy App Store/driver installers still publish **unsigned**, integrity-checked artifacts. Signed RISC-PKG parsing, writing, verification, intake staging, device NVS security-floor checks and disposable signed extraction are implemented as separate privileged APIs. **There is no signed-package publication, activation or execution path yet.** Production signer allowlists have not been provisioned and extracted ELF bytes on SD remain mutable through `dlopen`.

Authority: [RiscRTE Platform Specification](RISCRTE_PLATFORM_SPEC.md) → [Platform Capability Roadmap](PLATFORM_CAPABILITY_ROADMAP.md) §§26–28 and §39 → [RISC-PKG v1 format](RISC_PACKAGE_FORMAT.md) → [Security Architecture](SECURITY_ARCHITECTURE.md). This is an actual-state tracker, not a replacement for those specifications.

## Required invariant

One runtime-owned manager authenticates and installs applications, drivers, services and providers from identical archive bytes regardless of removable-SD or online source. The deterministic signed envelope binds identity, architecture/ABI, dependencies, complete payload SHA-256, security version and signer identity. Firmware/device policy alone controls trust and rights; installing a package grants **no hardware permission or activation**. Updates must preserve a verified generation through interruption, and authenticated ELF bytes must remain bound through loading even when removable SD changes.

## Implemented: legacy transitional paths

- `PackageIdentity.h`, `PackageJsonGuard.h`, `PackagePreflight.h`: bounded four-kind identities, duplicate-safe JSON, budgets, ABI/dependency metadata and numeric versions. None authenticates a publisher.
- `package_integrity.py`, `build_all_apps.py`, `AppPackageInstaller.cpp`, `NativeAppHost.cpp`: ELF length/SHA-256 validation, downgrade refusal and recoverable legacy app ELF/JSON updates. Sidecars remain unsigned.
- `PackagePairTransaction.h`, `AppPackageRecoveryInventory.cpp`: managed backup/stage recovery before springboard enumeration, retaining unknown content.
- `PackageTransaction.h`: driver directory publication and verified backup recovery. `PackageUseGate.h`: GPS/USB CDC mapping pins before `dlopen` until successful `dlclose`, plus exclusive replacement/recovery reservations. This does **not** prevent external SD modification or cover every future ELF loader.

## Implemented: signed archive, intake and extraction

- `docs/RISC_PACKAGE_FORMAT.md`, `PackageArchive.h`: deterministic bounded uncompressed RISC-PKG v1 framing/manifest, safe sorted names, canonical versions, exact length/entry offsets and four-kind preflight. Decoder success is not authentication.
- `scripts/build_risc_package.py`: genuine P-256/SHA-256 signature with externally supplied key, complete entry digests and ELF machine validation. Test signing keys are ephemeral; production private keys are not in the repository.
- `PackageArchiveVerification.h`, `PackageTrustPolicy.h`, `PackageDeviceCrypto.*`: streaming all-entry hashing, immutable signed-prefix snapshot, ELF checks and mbedTLS signature verification against a firmware-supplied signer allowlist. Exact kind/ID, revocation, unique key ID and signer security version are enforced. Empty signer lists reject every package.
- `PackageDeviceInspection.*`: read-only device API authenticates an already-open source and checks ABI/capability preflight without loading or installing.
- `PackageArchiveStage.h`: transport-independent source→fresh-stage copy in 512-byte chunks, sealed-stage readback, full reauthentication and signed-prefix comparison. Signed-package substitution, corrupt bytes and failed copies discard only the disposable stage.
- `PackageDeviceStage.*`: concrete `HalStorage` `/Packages/.intake.part` exclusive file creation. Checks device NVS security floor before copying and again after authentication to detect concurrent advances. Missing floors require explicit trusted first-install authorization (`allowFirstInstall=true`, default false). Success can now optionally return an authenticated 32-byte signed-prefix fingerprint in privileged RAM; failure clears it. Existing interrupted intake is not overwritten.
- **`PackageArchiveExtract.h`:** new transport-independent bounded extraction. It requires the signed-prefix fingerprint captured *at intake* rather than trusting one first computed after the SD has potentially changed. It reauthenticates the intake, applies runtime preflight and floor, copies each signed entry in 512-byte chunks, checks the source stream's signed SHA-256, closes and independently reads back and hashes each extracted file, seals the disposable directory, reauthenticates the original intake and rechecks floor. Any error discards the newly created extraction stage and clears the accepted identity.
- **`PackageDeviceExtract.*`:** concrete privileged destination `/Packages/.extract.part`, with an exclusively created `.extract.lock`, validated entry basenames, `O_CREAT|O_EXCL` for every new file, exact write/close/reopen/size/readback operations and cleanup limited to files this operation created. It refuses prior interrupted stages instead of deleting them. `extractSignedDevicePackage` reads `/Packages/.intake.part`, supplies firmware trusted keys, capability resolver, device-floor guard and the manager-owned intake fingerprint to the common extractor. It does **not** publish the directory, advance the floor, register a driver, or activate a module.
- `PackageSecurityFloor.h`, `PackageDeviceSecurityFloor.*`: monotonic per-(kind, ID) floor policy with NVS backend, full identity check against truncated hash-key collision, canonical record validation, distinct missing/corrupt/inaccessible states, serialised writers and failure-aware commit. The advance API exists but **is not connected to signed publication**; advancing it before recovery is safe could strand the device on an older generation.

### Current extraction boundary and remaining trust requirements

An extracted directory with correct readback digests is not yet a signed installed package. A future signed publisher must retain authenticated provenance (signed manifest, signature and exact file content), verify it after restart, publish through a recoverable transaction with in-use reservations, integrate floor advancement without making rollback recovery impossible, and keep the same verified executable bytes bound through `dlopen`. Neither an SD-based lock nor an open handle prevents physical SD removal/rewrite. Online and offline sources must ultimately enter the same manager API; the legacy download/driver installers are not yet migrated.

### Security-floor threat model

ESP NVS lies outside removable SD and persists state through normal restarts. It is **not** a hardware monotonic counter: raw NVS erasure/reflash can reset it under a physical-flash-write attacker. Production resistance to that attacker requires secured boot/storage or a stronger trusted backend, explicit first-install provisioning and recovery-aware advances. Do not advertise complete physical anti-rollback or interpret every missing record as first install.

## Tests and observed validation

- ASan/UBSan host suites cover identity/preflight, JSON, transactions/recovery, mapping pins, archive framing and trust, signed intake copy races, monotonic security floors and the actual NVS adapter compiled against a commit-aware fault backend with real SHA-256.
- `package_stage_test.py` generates temporary real P-256 signed packages and drives both `package_archive_stage_test.cpp` and new `package_archive_extract_test.cpp`. Extraction tests cover correct multi-chunk files, valid signed candidate replacement, corrupted input, interrupted creation/write/readback/seal, silently corrupted output, intake mutation after sealing and a floor increase during extraction. Failed cases require cleared identity and discarded disposable output.
- Full host and both firmware CI succeeded at `4ccf885` before extraction. Extraction commits require a newer full green run. No physical SD removal, power interruption or on-device acceptance has occurred.

## Outstanding acceptance milestones

1. **Trusted policy and atomic advancement:** provision production signer allowlist/revocation; enforce dependency/storage/consent policy; couple NVS floor advances to durable signed publication and crash recovery. NVS alone is insufficient against raw flash rollback.
2. **Authenticated provenance and verified-byte lifetime:** retain signed receipt/content, verify installed generations after restart and prevent SD substitution between extraction, publication and `dlopen`. Extend execution-context and mapping leases to every ELF loader.
3. **Unified signed publisher:** route offline SD and online downloads through one intake/extraction/recoverable publication implementation and retire or clearly isolate unsigned legacy publishers after parity testing. Add staging recovery for interrupted intake/extraction without deleting unknown files.
4. **Full management:** four-kind inventory, update, uninstall, quarantine, isolated service/provider activation, key rotation, UI and fallback recovery.
5. **Physical acceptance:** both boards, SD/network installs, power cuts at each transaction boundary, SD removal/replacement, in-use refusal and previous-generation recovery.

Hardware-specific implementations belong in installable driver ELFs; framework resources remain generic and capability gated. A manifest never authorizes hardware. `/sd` remains the read-only ELF VFS and managed changes use HalStorage. Keep PR #76 **draft, unmerged and unreleased** until acceptance gates pass.
