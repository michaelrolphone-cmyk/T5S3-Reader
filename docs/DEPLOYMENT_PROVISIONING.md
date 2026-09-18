# Deployment provisioning manifest — future specification

**Status: FUTURE / NOT IMPLEMENTED / OUTSIDE U1 AND THE CURRENT PACKAGE-MANAGER MVP.** This document records the intended deployment capability, not authorization to add provisioning code, bootstrap hardware, release gates, network credential storage, a new installer, or a U1 test requirement. Read [RiscRTE Platform Specification](RISCRTE_PLATFORM_SPEC.md), [hardware-agnostic driver boundary](HARDWARE_AGNOSTIC_DRIVER_BOUNDARY.md), [Unified Package Manager MVP](UNIFIED_PACKAGE_MANAGER_MVP.md), [ordinary package format](RISC_PACKAGE_FORMAT.md), [application version policy](APP_VERSION_POLICY.md), and [U1 milestone](NEXT_HARDWARE_TEST_MILESTONE.md). The present work must keep generic package and driver interfaces reusable for later provisioning without implementing this feature prematurely.

## 1. Goal and operating model

An operator prepares a new SD card for an ESP32 deployment by placing a versioned **deployment provisioning manifest** and, optionally, a collection of ordinary RiscRTE packages on it. At first startup on a compatible, minimally bootstrapped device, RiscRTE reads the manifest, validates the hardware target, resolves the requested initial state, and installs/configures the necessary drivers/providers, applications, services and permitted settings. The end result is a reproducible initial deployment without manually opening the App Store or Driver Manager for each package.

Provisioning is a **declarative initial-state operation**, not a new package format, alternate installer, mandatory background reconciler, hardware-specific firmware path, or implicit permission to flash firmware. Once successfully applied, the normal package manager and runtime own the installed state. A later manifest edit or replacement SD card MUST NOT silently reprovision or downgrade a working device; explicit reapply, reset or upgrade behavior requires a separately defined, user-authorized workflow.

## 2. Manifest and SD-media interface (design target; not a frozen schema)

Specify a documented fixed discovery location on the removable boot media (candidate: `/riscrte/provisioning.json`) and a versioned, bounded JSON schema. This candidate filename, JSON keys and example below are illustrative until a dedicated implementation milestone approves and tests the on-device parser. Do not silently make them current runtime contracts.

A manifest SHALL support:

- **Identity and compatibility:** schema version; stable deployment ID and revision; intended platform/CPU architecture, firmware/ABI compatibility constraints; explicit board/model/revision or compatible board-profile IDs. Refuse an incompatible physical target before activating packages or applying pin/power settings. Device identity matching must use available trusted boot/port facts rather than untrusted manifest text alone.
- **Hardware declaration:** selected versioned board/profile package(s), intended controllers/buses, attached peripheral profiles, pin and power mappings where permitted. Hardware is represented as data consumed by installed hardware-owning driver ELFs; the core MUST NOT switch on a declared USB/GPS/LoRa/display/touch identifier or implement the device to satisfy the manifest. Identify immutable boot-critical port prerequisites separately from installable runtime drivers.
- **Desired packages:** an ordered-or-unordered set of stable package IDs, kinds (`application`, `driver`, `service`, `provider`), version constraints or pinned versions, architecture/ABI requirements, and source references. Package dependencies and actual installation order come from the ordinary manager's generic dependency planner, not a second provisioning dependency graph or a USB-specific list. Never silently substitute a different version for an explicitly pinned package.
- **Sources and reproducibility:** optional SD-bundled ordinary packages and a configured immutable online repository/release snapshot for missing packages, subject to the existing package manager's origin/path, size, checksum, manifest, executable-byte and import validation. Offline provisioning must be possible when every required artifact is on the card; networking is optional rather than required to bring up its own driver.
- **Initial configuration:** bounded, typed, documented, allowlisted settings relevant to system locale/time/display, enabled apps, UI startup/default app, and versioned per-package configuration where supported. Settings that require a capability/provider are applied only after that provider is installed and safely available. Hardware-dangerous settings require proper compatibility and authorization; configuration MUST NOT grant privileged imports, capability rights or physical access merely because it appears on an SD card.
- **Application placement/launch:** optionally request default launcher layout/startup app through existing generic APIs. An app listed for installation is not automatically started or granted device access. Its individual version must be taken from its package/manifest and remain consistent with [application version policy](APP_VERSION_POLICY.md); deployment revision and firmware version do not substitute for app version bumps.

Illustrative **non-executable** structure (not a committed parser contract):

```json
{
  "schema_version": 1,
  "deployment_id": "field-unit-a",
  "revision": 1,
  "target": {
    "platform": "riscrte",
    "board_profile": "example-esp32s3-board",
    "firmware": ">=1.2.0"
  },
  "hardware_profiles": ["board/example", "peripheral/example-display"],
  "packages": [
    {"kind": "driver", "id": "example-display-driver", "version": "1.0.0"},
    {"kind": "application", "id": "example-dashboard", "version": "1.1.0"}
  ],
  "package_source": {"mode": "sd", "root": "/Packages/Provisioning"},
  "settings": {"startup_app": "example-dashboard"}
}
```

The final design must specify exact field limits, version-constraint syntax, path normalization, package snapshot identity, optional/required declarations, conflict policy and canonical hashing before accepting any on-device manifest.

## 3. First-run bootstrap, execution and recovery

The board port may provide only the minimal boot services needed to identify the board, mount/read the provisioning SD card, initialize the generic runtime/ordinary manager, and display or log recovery status. It cannot hide built-in production display, touchscreen, network, USB, LoRa or GPS implementations as fallback drivers. Resolve the bootstrap dependency explicitly: if the runtime needs a driver to access installation media or show its setup UI, that boot-critical dependency must have a small, documented port/recovery path or another independently bootstrappable route, never an undeclared permanent firmware hardware driver.

A future provisioner SHALL use this sequence:

1. Detect candidate manifest only under the defined first-run or explicitly requested reapply conditions; parse bounded data and establish deployment identity/revision and target compatibility. An absent manifest means ordinary boot, not a fatal error.
2. Compare the desired state with manager-owned installed inventory. Produce a preview/plan of packages, versions, dependencies, conflicts, requirements, hardware profiles, configuration and any user actions. Refuse incompatible board, firmware/ABI, duplicate IDs, ambiguous provider choices, invalid settings or missing packages with actionable diagnostics.
3. Obtain any required authorization before privileged admission or risky configuration. Never treat the manifest, catalog hash or physical possession of the SD card as proof of a trusted publisher or as capability permission; reuse existing authorization. Provisioning does not introduce package-signing requirements.
4. Stage and commit packages using the ordinary package-manager transaction/recovery engine. Apply dependencies in safe order, preserve working installed generations and fail without loading unvalidated ELF bytes. Installation does not itself activate hardware or execute applications; activation occurs only through authorized generic provider lifecycles. Apply configuration through existing versioned APIs after prerequisites are ready.
5. Persist a bounded, atomic deployment ledger (deployment ID, revision, canonical manifest digest, planned/installed versions and steps, completion/failure state), without copying credentials into logs. Reboot/power loss or SD removal resumes or safely reports the incomplete operation instead of redoing successful installations or deleting prior working state. Separate package rollback/recovery from the all-or-nothing claim: if a multi-package operation cannot be globally atomic, expose exact partial progress and recover to a usable device before marking provisioned.
6. Mark a deployment complete **only after** package, dependency, activation/configuration validation and final inventory comparison succeed. Never mark complete merely because files were copied or a download finished. On later boots an unchanged completed manifest is a no-op; a modified manifest requires explicitly defined reapply/upgrade approval rather than silent repeated installs.

If no display driver is available, the recovery path must remain observable through an explicitly supported minimal channel and cannot depend on a missing network driver. A failed installation must leave the platform bootable and its ordinary manager usable where the port/medium permits. Do not corrupt or erase unrelated user SD files or previously installed apps as an implicit factory-reset operation.

## 4. Security, credentials and scope boundaries

The deployment manifest is **configuration and desired state, not an authority token**. Reject traversal, oversized or cyclic inputs, arbitrary download URLs, inconsistent package identities, unsupported versions and unauthorized device settings. Validate every package using ordinary integrity/ABI/import/permission policies. Do not assume SHA-256 proves who authored a manifest. Do not add cryptographic package signing, key stores, trust-root policy, secure boot, or a new security model as an implied prerequisite; any future trust/provisioning policy requires separate explicit scope.

Do not store Wi-Fi credentials, bearer tokens or other secrets directly in a portable plaintext manifest by default. A future implementation may reference a separately authorized secure onboarding mechanism or require a local user interaction; its design is a separate decision. Never expose secrets in installation logs, failure reports or the persistent deployment ledger. Provisioning must not automatically flash firmware, alter partitions, override bootloader policy or change irreversible power/pin settings without an explicitly scoped and authorized firmware/board procedure.

## 5. Future acceptance (NOT a U1 gate)

A separately authorized provisioning milestone shall demonstrate on real supported hardware: (a) a brand-new SD card with the manifest and offline packages creates the intended initial device state through the **same manager** used for manual installs; (b) optional online sourcing uses one immutable snapshot and survives network absence; (c) installing a newly introduced driver requires no core/source/catalog special case; (d) wrong board/profile and unavailable dependencies fail before unsafe hardware activation; (e) power loss, SD removal and interrupted installs recover with correct ledger/idempotence; (f) a completed unchanged deployment does not replay; (g) an edited manifest requires explicit reapply; (h) package and app versions, settings and capabilities match final inventory; and (i) bootstrap and recovery remain available without normal runtime drivers.

**Explicit deferral:** U1 and the current package MVP need only preserve sufficiently generic manifest/manager, provider, profile and recovery interfaces to make this feature possible later. No provisioning manifest parser, first-run wizard, persistent deployment ledger, settings migration, additional trust infrastructure, SD image generator or new CI/release gate is required now. Do not divert USB extraction, common manager completion or independent-driver proof into implementing this future feature.
