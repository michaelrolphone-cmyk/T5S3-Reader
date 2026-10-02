# U1 package-signing purge audit — October 1, 2026

Base: PR #96, `844a6d08`, following owner-progress implementation `75316089`.
This changes repository code/documentation only. No SD/NVS user data, deployed
firmware, historical git objects, released assets, credentials, device security
settings or unrelated cryptographic implementation is erased or modified.

## Reference classification

The signed-format decoder/verifier/device wrappers formed an isolated dependency
subgraph. Whole-tree incoming-reference checks found no ordinary production
caller outside that subgraph. Its only executable callers were signing-only
fixtures and their separate runner. Ordinary manager/provider paths already use
neutral helpers; no replacement installer or blanket admission bypass was needed.

Every removed path is listed below. Git history retains the previous source.

| Exact path | Decision / reason |
|---|---|
| `docs/experimental/RISC_PACKAGE_FORMAT_SIGNED_V1.md` | DELETE: active signed-format experiment proposal |
| `scripts/build_risc_package.py` | DELETE: private-key package signing writer |
| `src/runtime/packages/PackageArchive.h` | DELETE: isolated package-authentication implementation |
| `src/runtime/packages/PackageArchiveExtract.h` | DELETE: isolated package-authentication implementation |
| `src/runtime/packages/PackageArchiveStage.h` | DELETE: isolated package-authentication implementation |
| `src/runtime/packages/PackageArchiveVerification.h` | DELETE: isolated package-authentication implementation |
| `src/runtime/packages/PackageDeviceCrypto.cpp` | DELETE: isolated package-authentication implementation |
| `src/runtime/packages/PackageDeviceCrypto.h` | DELETE: isolated package-authentication implementation |
| `src/runtime/packages/PackageDeviceDirectory.cpp` | DELETE: isolated package-authentication implementation |
| `src/runtime/packages/PackageDeviceDirectory.h` | DELETE: isolated package-authentication implementation |
| `src/runtime/packages/PackageDeviceExtract.cpp` | DELETE: isolated package-authentication implementation |
| `src/runtime/packages/PackageDeviceExtract.h` | DELETE: isolated package-authentication implementation |
| `src/runtime/packages/PackageDeviceInspection.cpp` | DELETE: isolated package-authentication implementation |
| `src/runtime/packages/PackageDeviceInspection.h` | DELETE: isolated package-authentication implementation |
| `src/runtime/packages/PackageDeviceInstaller.cpp` | DELETE: isolated package-authentication implementation |
| `src/runtime/packages/PackageDeviceInstaller.h` | DELETE: isolated package-authentication implementation |
| `src/runtime/packages/PackageDeviceProviderProfile.cpp` | DELETE: isolated package-authentication implementation |
| `src/runtime/packages/PackageDeviceProviderProfile.h` | DELETE: isolated package-authentication implementation |
| `src/runtime/packages/PackageDevicePublication.cpp` | DELETE: isolated package-authentication implementation |
| `src/runtime/packages/PackageDevicePublication.h` | DELETE: isolated package-authentication implementation |
| `src/runtime/packages/PackageDeviceSecurityFloor.cpp` | DELETE: isolated package-authentication implementation |
| `src/runtime/packages/PackageDeviceSecurityFloor.h` | DELETE: isolated package-authentication implementation |
| `src/runtime/packages/PackageDeviceStage.cpp` | DELETE: isolated package-authentication implementation |
| `src/runtime/packages/PackageDeviceStage.h` | DELETE: isolated package-authentication implementation |
| `src/runtime/packages/PackageProviderProfile.h` | DELETE: isolated package-authentication implementation |
| `src/runtime/packages/PackageSecurityFloor.h` | DELETE: isolated package-authentication implementation |
| `src/runtime/packages/PackageSignedProvenance.h` | DELETE: isolated package-authentication implementation |
| `src/runtime/packages/PackageSignedTransaction.h` | DELETE: isolated package-authentication implementation |
| `src/runtime/packages/PackageTrustPolicy.h` | DELETE: isolated package-authentication implementation |
| `test/resources/package_archive_extract_test.cpp` | DELETE: signing-only fixture or optional runner |
| `test/resources/package_archive_inspect.cpp` | DELETE: signing-only fixture or optional runner |
| `test/resources/package_archive_stage_test.cpp` | DELETE: signing-only fixture or optional runner |
| `test/resources/package_archive_test.cpp` | DELETE: signing-only fixture or optional runner |
| `test/resources/package_builder_test.py` | DELETE: signing-only fixture or optional runner |
| `test/resources/package_device_security_floor_test.cpp` | DELETE: signing-only fixture or optional runner |
| `test/resources/package_floor_stubs/mbedtls/sha256.h` | DELETE: signing-only fixture or optional runner |
| `test/resources/package_floor_stubs/nvs.h` | DELETE: signing-only fixture or optional runner |
| `test/resources/package_provider_profile_inspect.cpp` | DELETE: signing-only fixture or optional runner |
| `test/resources/package_provider_profile_test.py` | DELETE: signing-only fixture or optional runner |
| `test/resources/package_security_floor_test.cpp` | DELETE: signing-only fixture or optional runner |
| `test/resources/package_signed_pipeline_test.cpp` | DELETE: signing-only fixture or optional runner |
| `test/resources/package_signed_transaction_test.cpp` | DELETE: signing-only fixture or optional runner |
| `test/resources/package_stage_test.py` | DELETE: signing-only fixture or optional runner |
| `test/resources/package_trust_policy_test.cpp` | DELETE: signing-only fixture or optional runner |
| `test/run_signed_package_experiment.sh` | DELETE: signing-only fixture or optional runner |

## Shared code retained and decoupled

- **KEEP** `PackageIdentity`, `PackageJsonGuard`, `PackagePreflight`, ordinary
  manifest/stage/installer/transaction/recovery, pair migration, use gate,
  sequential SD reader, ZIP bootstrap and catalog helpers. Remove only the
  unused package-authentication security-version fields/check from preflight;
  ordinary numeric version/downgrade and integrity checks remain.
- **KEEP / neutral internal names** `DeviceProviderExecutorV2`, `ProviderGraphV2`,
  `ProviderOwnedSpecV2`, `ProviderModuleV2`. Declared imports/content digests remain
  copied, bounded data; the private manager-admission route stays separate from
  caller-authored ordinary specs. Snapshot ownership, exact import/ABI policy,
  dependency pins and failed-quiescence quarantine remain enforced.
- **KEEP** the actual SHA-256 implementations and corrupt-content tests, TLS and
  general OS cryptography, firmware-image hashes/MD5 where protocol-required,
  independent user/device rights and transport security settings.
- **REWRITE** `PROVIDER_PACKAGE_PROFILE.md` around the current ordinary ABI/import
  metadata and manager handoff. Preserve generic parser, exact-byte, permission,
  lifetime, recovery and native-isolation limits in `SECURITY_ARCHITECTURE.md`;
  remove its obsolete package-signing/key/floor/provisioning mandate. Remove
  contradictory package-signing requirements from active architecture guidance.
  Explicitly labeled legacy documents and dated implementation history remain
  historical evidence, not active instructions or retained implementations.

## Compatibility and verification

Unsupported historical signed-only input is rejected by ordinary format checks;
there is no automatic conversion, execution grant or cleanup of unknown media.
Existing ordinary packages keep their format, package identity/version and
loader ABI. This is an internal preflight/admission cleanup, not a distributable
app/driver payload change.

The ordinary package and provider-graph suites passed after deletion. Final
springboard, stream, import/authorization and target checks are recorded in the
implementation ledger/PR. A dedicated source/build-reference absence check is
added to the normal ordinary-package suite. Host and CI results do not establish
physical hardware or adversarial native-code isolation.

**Implementation In Progress**
