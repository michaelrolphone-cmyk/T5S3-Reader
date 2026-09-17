# Unified Package Manager MVP — architecture and acceptance

**Status:** Priority 3 implementation in draft PR #76. Shared identity, metadata preflight, numeric version policy, guarded legacy manifest parsing and the driver's directory transaction are foundation slices. **The common preflight helper is not connected to an envelope decoder/installer.** The common archive, authenticated signatures, atomic App Store migration, SD offline installation and service/provider deployment are **not implemented**. No legacy package is implicitly trusted.

Authority: [RiscRTE Platform Specification](RISCRTE_PLATFORM_SPEC.md) → [Platform Capability Roadmap](PLATFORM_CAPABILITY_ROADMAP.md), §§26–28 and §39 → [Runtime Driver Architecture](RUNTIME_DRIVER_ARCHITECTURE.md), [Application Execution Context Architecture](APPLICATION_EXECUTION_CONTEXT_ARCHITECTURE.md), and [Security Architecture](SECURITY_ARCHITECTURE.md). This tracks the MVP and its migration, not a replacement architecture.

## Target invariant

One runtime-owned manager validates and installs **applications, drivers, services and providers**, inspecting metadata without loading payloads. A versioned envelope binds identity, complete entry sizes/hashes, architecture/ABI, dependencies, capability declarations, security version and an authenticated publisher signature. SD and online distribution use the same envelope and installer. Interrupted updates retain a verified generation; installation grants no hardware access or activation. SHA-256 validates bytes against a declared digest but cannot authenticate a mutable manifest. Hardware protocol implementations reside in driver ELFs; the runtime exposes only generic resource and capability interfaces.

## Implemented foundation

### Identity (`src/runtime/packages/PackageIdentity.h`)

- Kind is `Application`, `Driver`, `Service` or `Provider`; `(kind,id)` forms the namespace.
- IDs are bounded lowercase slugs, maximum 63 bytes, with internal `-`/`_`; no separators, traversal or trailing punctuation. Versions are bounded numeric `major.minor.patch` with each component fitting `uint32_t`.
- ELF artifact names are bounded basenames, maximum 127 bytes. Invalid kinds, paths and malformed fields are rejected, and identity outputs are cleared on failure.
- Newly issued manifests MUST explicitly bind kind, ID, version and artifact. Legacy untyped application sidecars may infer ID and omit version only to preserve existing installations; drivers/services/providers and signed envelopes cannot use the exemption.
- Metadata does not confer authentication, loading authority or permission.

`src/native/AppManifest.cpp` accepts optional canonical `type: "application"` and `id` alongside compatible legacy sidecars. `src/runtime/drivers/DriverPackage.cpp` adopts shared identity checks without dropping the existing driver ABI, architecture, ELF header, size, SHA-256 and capability checks.

### Lexical manifest guard (`src/runtime/packages/PackageJsonGuard.h`)

The allocation-free, input-bounded guard runs **before ArduinoJson** in both legacy app and driver manifest parsers. It requires a single JSON object, checks syntax, rejects duplicate keys at every nesting level, refuses escaped-key aliases, invalid escapes and excessive input/depth/key counts. It does not parse an archive, canonicalize for signatures, hash bytes, authenticate a publisher or grant access. Keys must be unescaped printable ASCII; UTF-8 payload values are passed to ArduinoJson for interpretation. The common future decoder must additionally validate its complete schema and canonical signature representation. `test/resources/package_json_guard_test.cpp` supplies sanitizer-backed hostile-input regression coverage.

Installed driver inventory version lookup now calls the full driver payload validator; a readable manifest and filename alone cannot claim an installed version if the ELF header, size or digest fails.

### Bounded metadata preflight (`src/runtime/packages/PackagePreflight.h`)

`PackageEnvelopeView` is a borrowed typed view for a future decoder, not a decoder itself. One explicitly named ELF, at most 16 entries and 16 capability/API requirements, safe lowercase basenames, positive bounded sizes, syntactically valid SHA-256 declarations, unique entries, no hidden `.elf`, and aggregate budgets are checked without executing anything. Architecture/runtime API/security floors and capability/API availability are validated; a registry lookup is not a capability grant. Version comparison is numeric and install policy distinguishes fresh install, upgrade, equal, legacy migration, identity conflict and explicit downgrade choices. The current App Store and Driver Manager do **not** yet invoke this shared policy.

A successful `ReadyForContentVerification` result means only the metadata is ready for further inspection. Actual payload hashes, authorized signatures, pinned bytes, install decisions and separate consent are required before release use.

### Directory transaction (`src/runtime/packages/PackageTransaction.h`)

The legacy driver installer stages ELF plus manifest together under `/Drivers/.<id>.install`, validates the stage, moves the old directory to `/Drivers/.<id>.previous`, then publishes the new directory. The shared coordinator restores an intact verified backup after an interrupted rename, refuses unknown/corrupt target data, and permits cleanup recovery after partial deletion of *managed-only* backup files if the new target revalidates. If the target is absent, the backup must verify in full before restoration. The driver install call still passes `replaceAllowed=true`: a generic in-use module pin/replacement gate is **not yet implemented**. Host fault injection does not establish physical-media crash durability or publisher authenticity.

## Legacy installer risks and required migration

**App Store:** `src/native/NativeAppHost.cpp` downloads separate `.elf` and `.json` `.part` files, checks release asset length and compatibility, then uses separate `.bak` renames. It can remove a destination before establishing a complete, verified backup pair. This path is **not crash-safe**. Move applications to the same verified single-directory transaction with launcher compatibility, or use a rigorously journaled pair state machine with content binding and no mixed-pair launch. Reject replacement of running/mapped modules. Neither the JSON guard nor a manifest and size check authenticates an ELF.

**Driver Manager:** `src/native/NativeDriverManagerBridge.cpp` and `src/runtime/drivers/DriverPackage.cpp` discover and validate legacy drivers; availability, installation, binding and activation remain distinct. Directory transaction exists, but generic module pins and authenticated signing still do not.

`/sd` is a read-only ELF VFS. Mutations MUST use `HalStorage`; decoded manifests cannot select arbitrary storage destinations. The runtime is hardware-agnostic beyond its generic resource/capability substrate.

## Implementation order

1. **Common bounded `package.risc` decoder:** define and enforce an exact schema, canonical signing bytes, all entries, archive limits, duplicate physical names and filesystem aliases, unsupported ABI and unknown fields. Map validated data to `PackageEnvelopeView`. The current lexical JSON guard covers duplicate keys in legacy app/driver manifests only; it is not the envelope decoder.
2. **Authenticated trust/content:** hash every entry, enforce allowlisted signatures and security-version rollback policy, separate explicit unsigned development/legacy mode from signed release mode, and prevent verification-to-load substitution.
3. **One crash-recoverable installer:** migrate App Store and Driver Manager, add module pin guards, install/update/uninstall/quarantine/inventory, storage preflight and fault injection after every transition; preserve last verified generations and refuse unmanaged content.
4. **SD/online parity and four kinds:** same bytes and checks, ABI/dependency resolution without loading candidates, support service/provider deployment. Built-in Wi-Fi and serial recovery must remain usable.
5. **Platform-owned consent UI:** show identity, source, independently verified trust, capabilities, storage use and version changes; arbitrary apps cannot authorize package mutation through path selection.

## Acceptance and release gate

Host: identity/version boundaries, four kinds, aliases/traversal, duplicate keys, false digests/signatures, ABI/security floors, missing dependencies, insufficient space, unmanaged files, in-use replacement refusal, cuts after every rename/deletion, reinstall/upgrade/downgrade/uninstall and no candidate execution. Compile both board targets, validate app/driver catalogs, then test SD offline install and real power-cut recovery with last-known-good app and driver intact. The current JSON guard, metadata preflight and host transaction tests are **not** evidence of signature verification, complete package installer functionality or on-device acceptance. Keep PR #76 draft, unmerged and unreleased until the claimed subset passes CI; continue tracking these missing gates explicitly.
