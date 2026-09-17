# Unified Package Manager MVP — implementation and acceptance

**Status:** Roadmap priority 3, draft PR #76. Existing App Store and driver installer paths are integrity-aware but **unsigned**. The canonical RISC-PKG decoder, signed writer, all-entry verifier, trust-policy adapter, device inspection, and transport-neutral verified-copy staging are implemented. **The firmware does not publish or load signed `.risc` packages yet.** Production signing keys are not provisioned, package security floors are not persisted, and the verified ELF bytes are not protected against SD removal/replacement through `dlopen`. The unsigned legacy sidecar hash never establishes publisher trust.

Authority: [RiscRTE Platform Specification](RISCRTE_PLATFORM_SPEC.md) → [Platform Capability Roadmap](PLATFORM_CAPABILITY_ROADMAP.md) §§26–28 and §39 → [RISC-PKG v1 format](RISC_PACKAGE_FORMAT.md) → [Security Architecture](SECURITY_ARCHITECTURE.md). This document tracks implementation/acceptance without overriding those specifications.

## Required invariant

One runtime-owned package manager authenticates and installs applications, drivers, services and providers using the **same bytes and verification algorithm** from either removable SD or an online download. The bounded canonical signed envelope binds identity, architecture/ABI requirements, dependency declarations, complete payload hashes, security version and publisher identity. Only trusted device/firmware policy supplies signing keys and authorization. Package installation confers **no hardware access or activation**. Power cuts preserve a usable verified generation; verified ELF bytes must remain bound through loading even if removable SD changes.

## Implemented: legacy transitional paths

- `PackageIdentity.h`, `PackageJsonGuard.h` and `PackagePreflight.h`: bounded four-kind identity, guarded JSON, entry/dependency budgets, architecture/runtime compatibility and numeric versions. Their results are metadata decisions, not trust.
- `package_integrity.py`, `build_all_apps.py`, `AppPackageInstaller.cpp` and `NativeAppHost.cpp`: stamp and validate actual ELF size and SHA-256, reject downgrades, verify staged downloads, launch/version checks and recoverable `.elf`/`.json` publication. Old digestless sidecars remain migration-only and unsigned.
- `PackagePairTransaction.h` and `AppPackageRecoveryInventory.cpp`: recover managed `.part`/`.bak` pairs before springboard presence tests and installed enumeration, closing directory handles before mutation and retaining unknown files.
- `PackageTransaction.h`: verified driver-directory staging and interrupted rename/cleanup recovery with previous-generation protection.
- `PackageUseGate.h`: runtime-owned pins and exclusive replacement reservations. GPS and USB CDC loaders pin prior to `dlopen` through successful `dlclose`, keeping a pin on failed unload. Driver installation and mutating recovery honor the reservation. This blocks registered runtime loader/installer races, **not externally modified SD bytes** or unregistered future ELF loaders.

## Implemented: authenticated distribution building blocks

- `RISC_PACKAGE_FORMAT.md` and `PackageArchive.h`: deterministic uncompressed v1 wire format and bounded allocation-free framing/manifest decoder; canonical versions, strict entry ordering, safe basenames, exact archive length/offsets and shared preflight. `ReadyForAuthentication` alone is not trust.
- `scripts/build_risc_package.py`: builds real P-256/SHA-256 signed archives from an explicit external private key; binds each file's SHA-256, validates ELF architecture/machine, name and resource safety, entry budgets and signer curve. No private production key is embedded; host tests generate ephemeral keys.
- `PackageArchiveVerification.h`: hashes the exact buffered signed prefix and verifies a trusted P-256 callback; checks **every** payload by 512-byte streaming SHA-256, ELF class/machine, and resource masquerading. Caller-owned bounded workspace avoids ELF-sized allocation. `AuthenticatedContent` means only the bytes just inspected.
- `PackageTrustPolicy.h`, `PackageDeviceCrypto.*`: firmware-owned allowlist with unique key IDs, exact package kind/ID scope, minimum signer security version, revocation and point checks; empty/unconfigured policy fails closed. Cryptographic checks do not provision a key, persist package rollback state or grant capabilities.
- `PackageDeviceInspection.*`: read-only on-device entry point accepting an already-open source plus trusted signer/runtime policy, then authenticating all entries and applying preflight. No installation/activation.
- `PackageArchiveStage.h`: shared SD/network-independent staging algorithm, authenticating source, copying to fresh disposable stage in bounded chunks, sealing/reopening and authenticating all copied bytes again. It compares the original and staged signed-prefix digest to refuse replacement by a different valid signed package during copying. On failure, the stage is discarded and no package identity is returned.
- `PackageDeviceStage.*`: concrete privileged `HalStorage` adapter for the fixed `/Packages/.intake.part` path. Uses exclusive creation and exact writes, closes/reopens for readback, refuses an existing interrupted stage instead of destructively overwriting it. This is **intake for publication review**, not a signed installer; it is not yet called from the legacy App Store or driver manager.

## Tests and observed validation

- Host ASan/UBSan: identity/preflight, JSON guards, flat-pair and directory transactions, boot recovery, mapping pins and archive framing.
- OpenSSL interoperability: temporary real P-256 signing key, Python writer to C++ decoder/authenticator, corrupt header/signature/payload rejection and all-entry hashing.
- `package_stage_test.py` + `package_archive_stage_test.cpp`: two independently signed real archives and injected source mutation, valid-package substitution, corrupt staged payload, incompatible runtime policy, stage creation refusal, second-write failure and sealing failure. **Host suite passed on commit `073884f`** after repairing the fixture writer. A previous failure at `8fb0040` was a test setup error (the Python builder returned archive bytes but did not write fixture files); this was corrected, not concealed.
- Last complete pre-staging CI passed at `8582f19`. Latest-head firmware builds and host suite require a new completed workflow result; do not treat an earlier SHA as proof of a later one. Hardware testing, physical power cuts and SD removal races have **not** been performed.

### Developer signing example

```sh
python3 scripts/build_risc_package.py \
  --kind driver --id gps-nmea --version 1.2.3 \
  --artifact driver.elf --architecture xtensa-esp32s3 \
  --min-runtime-api 2 --security-version 3 --key-id 7 \
  --private-key /secure/signing-key.pem \
  --entry driver.elf=/build/driver.elf \
  --require kernel.serial:1 --output /build/gps-nmea.risc
```

The command produces a signed distribution artifact, **not** an installation accepted by production firmware. A package-supplied key ID is only a selector; signing keys and scope come from firmware/device trust. Do not store private keys in the repo or removable SD.

## Outstanding acceptance milestones, in dependency order

1. **Trusted and persistent policy:** actually provision firmware/device-owned package signing material and revocation policy; enforce trusted persisted per-package minimum security versions outside mutable SD, independent of semantic version; use a single privilege-controlled manager to check consent, dependencies, storage and version policy. Package manifests cannot approve their own rights.
2. **Verified-byte lifetime:** secure package stage and published executable generations against external removable-SD substitution between verification and `dlopen`. Reauthentication after a copy is necessary but insufficient if the resulting stage can be modified later. Extend mapping pins and context leases to every ELF loader. Do not declare production trust until this holds.
3. **Unified installer and recovery:** route both offline SD and online package downloads into the same signed intake, authentication, staging, all-entry extraction and recoverable publication. Test install/upgrade/failure recovery for all four kinds. The existing App Store and driver publishers remain unsigned transitional interfaces until explicitly migrated and regression tested. Retain explicit development-only unsigned policy.
4. **Four-kind management:** app/driver/service/provider activation separation; inventory, update, uninstall/quarantine, version/trust/permission UI, key rotation, fallback serial/Wi-Fi recovery and security floor migration.
5. **Physical acceptance:** both boards, actual SD/network transfers, resets at transaction boundaries, good previous-generation recovery, SD removal/replacement before load, busy driver replacement refusal and consent. No on-device acceptance has been reported.

Hardware implementations belong in driver ELFs; the runtime exposes **generic capability-gated resources** only. Installing a package neither activates hardware nor grants rights. `/sd` remains the read-only ELF VFS; privileged managed changes use `HalStorage`. Keep PR #76 **draft, unmerged and unreleased** until the claimed scope and gates pass.
