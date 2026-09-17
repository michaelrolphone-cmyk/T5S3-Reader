# Unified Package Manager MVP — implementation and acceptance

**Status:** Roadmap priority 3, draft PR #76. Legacy App Store and driver installers use integrity-aware but **unsigned** publication. Signed RISC-PKG parsing, writing, verification, device inspection, staging and a device NVS security-version floor gate are implemented. **Signed package publication and execution are not implemented.** No production signing-key allowlist has been provisioned and verified ELF bytes are not protected against SD substitution through `dlopen`.

Authority: [RiscRTE Platform Specification](RISCRTE_PLATFORM_SPEC.md) → [Platform Capability Roadmap](PLATFORM_CAPABILITY_ROADMAP.md) §§26–28 and §39 → [RISC-PKG v1 format](RISC_PACKAGE_FORMAT.md) → [Security Architecture](SECURITY_ARCHITECTURE.md). This tracker distinguishes working code from outstanding acceptance.

## Required invariant

One runtime-owned manager authenticates and installs applications, drivers, services and providers from identical archive bytes regardless of removable-SD or online source. The deterministic signed envelope binds identity, architecture/ABI, dependencies, complete payload SHA-256, security version and signer identity. Firmware/device policy alone controls trust and rights; installation grants **no hardware permission or activation**. Updates must preserve a verified generation through interruption, and the exact authenticated ELF bytes must remain bound through loading even if the SD changes.

## Implemented: legacy transitional paths

- `PackageIdentity.h`, `PackageJsonGuard.h`, `PackagePreflight.h`: bounded four-kind identities, duplicate-safe JSON, budgets, ABI/dependency metadata and numeric versions. None authenticates a publisher.
- `package_integrity.py`, `build_all_apps.py`, `AppPackageInstaller.cpp`, `NativeAppHost.cpp`: actual ELF size/SHA-256 validation, downgrade refusal and recoverable legacy app ELF/JSON updates. Sidecars remain unsigned.
- `PackagePairTransaction.h`, `AppPackageRecoveryInventory.cpp`: managed backup/stage recovery before springboard enumeration, preserving unknown content.
- `PackageTransaction.h`: driver directory publication and verified backup recovery. `PackageUseGate.h`: GPS/USB CDC map pins before `dlopen` until successful `dlclose`, plus exclusive driver replacement/recovery reservations. This does **not** prevent external SD modification or cover every future ELF loader.

## Implemented: signed archive and intake

- `docs/RISC_PACKAGE_FORMAT.md`, `PackageArchive.h`: deterministic bounded uncompressed RISC-PKG v1 framing/manifest, safe sorted names, canonical versions, exact length/entry offsets and four-kind preflight. Decoder success is not authentication.
- `scripts/build_risc_package.py`: actual P-256/SHA-256 signature from an external private key with complete entry digests, ELF machine validation and hostile-input rejection. Test signing keys are ephemeral; production private keys are not in the repository.
- `PackageArchiveVerification.h`, `PackageTrustPolicy.h`, `PackageDeviceCrypto.*`: streaming all-entry hashing, authenticated signed-prefix snapshot, ELF checks and mbedTLS signature verification against a firmware-supplied signer allowlist. Exact kind/ID, revocation, unique key ID and signer security floor are enforced. Empty signer lists reject everything.
- `PackageDeviceInspection.*`: read-only device entry point authenticates an already-open source and checks ABI/capability preflight without loading or installing it.
- `PackageArchiveStage.h`: transport-independent bounded source→fresh-stage copy, seal/readback and full reauthentication. Signed-prefix binding rejects valid-but-different package substitution during copying; errors discard only the disposable stage.
- `PackageDeviceStage.*`: actual HalStorage stage `/Packages/.intake.part` created exclusively without truncating interrupted work. Before writing, it checks the signed candidate against the device NVS security floor. After readback and authentication, it checks the floor again to catch concurrent advances; rejection discards the stage. Missing floor records require explicit trusted `allowFirstInstall=true`; default is false. This API is not called by the legacy App Store/driver installer.
- `PackageSecurityFloor.h`, `PackageDeviceSecurityFloor.*`: monotonic per-(kind, ID) floor policy and NVS-backed record. The NVS key hashes the complete kind/ID and its stored record checks the full identity to reject short-key collisions. Missing, malformed and inaccessible records are distinct and fail closed unless first install is explicitly authorized. Concurrent floor writes serialize across the running firmware; a failed NVS commit never reports success. **The advance operation exists but is not yet connected to signed publication.** It must be invoked only after the new verified generation is durably recoverable and an older fallback will not be invalidated.

### Security-floor threat model

Ordinary ESP NVS is outside removable SD and can persist device state through normal restart and interrupted writes. It is **not** an immutable hardware monotonic counter: erasing/reflashing NVS can reset or roll back it when physical flash rewrite is possible. Production physical-rollback resistance requires secured boot/storage policy or a stronger trusted backend, explicit initial-install provisioning, and recovery-aware advancement. Do not advertise complete anti-rollback or treat a missing record as first install automatically.

## Tests and observed validation

- ASan/UBSan host tests: identity/preflight, JSON, transactions/recovery, mapping pins, archive framing and trust policy, signed-copy fault injection, monotonic security-floor policy, plus the actual NVS adapter compiled against a commit-aware failure-injection host backend with real OpenSSL SHA-256 key derivation.
- Python/C++ interoperability: real temporary P-256 key and OpenSSL verification; malformed metadata, signature, header and payload tampering; valid signed substitute, corrupt stage, refused stage, failed write/seal and source mutation.
- CI passed all host tests and both firmware builds at `0d581d2` (before the security-floor commits). Latest-head CI, including device NVS compilation and the new tests, must be checked separately. No physical SD removal, power interruption or on-device acceptance has occurred.

## Outstanding acceptance milestones

1. **Trusted policy and atomic advancement:** provision production signer keys/revocation, enforce dependency/storage/consent policy, and atomically integrate the NVS floor advance with durable signed publication and its recovery semantics. Decide and enforce a physical flash rollback threat model; device NVS by itself is insufficient for that attacker.
2. **Verified-byte lifetime:** prevent removable SD substitution between stage verification, publication and `dlopen`; enforce authentic bytes and execution-context/mapping leases for every ELF loader.
3. **Unified signed publisher:** route both SD and online inputs through the same archive intake, verified extraction and recoverable publication; retire or explicitly segregate unsigned legacy App Store/driver installation only after parity tests.
4. **Full management:** four-kind inventory, update, uninstall, quarantine, service/provider activation isolation, key rotation, UI and fallback recovery.
5. **Physical acceptance:** both boards, offline/online installs, power cuts at each transaction boundary, SD removal/replacement, in-use refusal and previous-generation recovery.

Hardware-specific code stays in driver ELFs; runtime resources remain generic and capability gated. No package manifest authorizes hardware. `/sd` is the read-only ELF VFS; managed mutations use HalStorage. Keep PR #76 **draft, unmerged and unreleased** until the relevant acceptance gates pass.
