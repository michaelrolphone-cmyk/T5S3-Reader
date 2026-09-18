# Deployment provisioning manifest — U4 implementation contract

**Status: FUTURE / NOT YET IMPLEMENTED. Authorized implementation milestone: [U4 provisioning and ESP32-S3-CAM multi-device proof](NEXT_MILESTONE_PROVISIONING_AND_ESP32_S3_CAM.md).** Deferred from U1, U2 and U3. This specification does not authorize implementing provisioning during earlier milestones or make it a U3 completion prerequisite. **U3 instead must boot the owner's ESP32-S3-CAM stably and headlessly with no board drivers or provisioning manifest installed.** Read [Platform Specification](RISCRTE_PLATFORM_SPEC.md), [hardware-agnostic driver boundary](HARDWARE_AGNOSTIC_DRIVER_BOUNDARY.md), [Unified Package Manager MVP](UNIFIED_PACKAGE_MANAGER_MVP.md), [ordinary package format](RISC_PACKAGE_FORMAT.md), [U2 publishing/source management](NEXT_MILESTONE_INDEPENDENT_PACKAGE_ECOSYSTEM.md), [U3 headless acceptance](NEXT_MILESTONE_T5_PRO_HARDWARE_DRIVER_MIGRATION.md) and [U4](NEXT_MILESTONE_PROVISIONING_AND_ESP32_S3_CAM.md). U4 settles precise schemas and implements this design; the current examples are not deployed parser contracts.

## 1. Goal and operating model

An operator prepares a supported provisioning medium with a bounded versioned manifest and optionally ordinary RiscRTE package ZIPs. On an eligible first boot or explicitly authorized reapply, a minimally bootstrapped compatible device validates its identity, resolves requested initial state and installs/configures the required driver/provider/application/service packages through the **same ordinary package manager** as manual installs. Headless devices must not require display/Settings, camera driver or graphical setup before initial package installation. Offline bootstrap works when all required packages are accessible on a verified supported medium; U2's independent GitHub repository is an optional online source. A missing medium/manifest is normal boot, not a fatal error.

Provisioning is a declarative initial-state operation, **not** another package format/installer, an always-on reconciler, firmware flashing, implicit hardware permission, a forced core-package installation or a board-specific runtime switch. After commitment, the normal manager and providers own installed/active state. Modified SD or a new manifest never silently downgrades/reprovisions a working deployment: reapply needs an explicit approved mode.

## 2. Manifest and source contract

Choose and document at U4 implementation a fixed discovery location on each supported bootstrap medium (illustrative SD candidate `/riscrte/provisioning.json`), schema and strict size/item/depth limits, normalized path/URL policy, conflict behavior, version-constraint syntax, pin/hash rules and resource budgets. A board without SD must not enter a boot loop or be declared provisionable through an imaginary slot; a verified alternative bootstrap medium may be supported without changing the ordinary installer.

Manifest fields SHALL cover:

- Stable deployment ID/revision, schema version, CPU/architecture, runtime/port/ABI and firmware compatibility and **verified actual board/profile identity**. Refuse incompatible targets before applying pins, power configuration or activating drivers. Manifest self-declaration is not trusted board identification.
- Required/optional board and peripheral profiles, controllers/buses, pin/power mapping inputs interpreted **only by eligible installed driver/provider ELFs**. The core transports generic data without parsing camera/LoRa/display/USB types. Immutable boot/module-store prerequisites are declared distinctly from ordinary installable drivers.
- Desired ordinary packages of all four kinds with stable IDs, pinned versions or bounded constraints, compatible target/ABI, required capabilities and configured source references. Resolve the order through the existing manager dependency graph, not a second provisioning planner or hard-coded board lists. An explicitly pinned version cannot silently change.
- Optional bundled SD/package-medium archives and immutable selected U2 GitHub repository/release snapshot for remaining packages; validate exact source identity, bounds, size/hash, manifest, ELF/import/ABI and runtime authorization. A network connection is not required to install the network driver needed to obtain one.
- Documented allowlisted typed initial settings (system locale/time, selected screen/startup app where applicable, package configuration), startup/headless services and optionally existing generic launcher layout. Apply only after necessary providers are available. Installing an ELF does not authorize execution/device access or start it automatically; per-app version remains independent of deployment revision or firmware version.

Illustrative, **non-executable** JSON; key names are NOT frozen until U4 implementation:

```json
{
  "schema_version": 1,
  "deployment_id": "field-unit-a",
  "revision": 1,
  "target": {"platform": "riscrte", "board_profile": "example-esp32s3-board", "firmware": ">=1.2.0"},
  "hardware_profiles": ["board/example", "peripheral/example-camera"],
  "packages": [
    {"kind": "driver", "id": "example-camera-driver", "version": "1.0.0"},
    {"kind": "service", "id": "example-headless-diagnostic", "version": "1.1.0"}
  ],
  "package_source": {"mode": "sd", "root": "/Packages/Provisioning"},
  "settings": {}
}
```

The example's camera and SD source are illustrative, **not claims about the owner's camera sensor or media**. U4 must inventory the real unit before selecting drivers, pin maps or provisioning media.

## 3. Bootstrap, execution, idempotence and recovery

The smallest separate platform/recovery path may identify actual CPU/port/board facts, access the verified provisioning medium, start the generic package manager and expose a diagnostic/consent channel. It must NOT register a general-purpose production camera, display, SD, Wi-Fi, touch or other fallback hardware driver. If accessing the package medium requires a driver located on it, resolve that circular dependency with a documented minimal boot-only reader or compatible independent recovery transfer. This exception must not race a runtime provider. Recovery status must be available through an actually supported non-display route for headless devices, without assuming a network driver already installed.

U4 SHALL implement the following state machine using [bounded cooperative operations](COOPERATIVE_BOUNDED_OPERATIONS.md):

1. Detect only during eligible first-run or owner-approved reapply; absent manifest proceeds to normal idle. Parse bounded data, validate deployment identity/revision, authentic boot/port facts and actual board/profile compatibility.
2. Compare desired state to manager-owned inventory and produce an inspectable plan of versions, dependencies, sources, conflicts, electrical/profile requirements and authorized configuration. Reject wrong board/ABI, duplicate package IDs, ambiguous providers, unsafe settings and missing packages before unsafe activation.
3. Request proper permission for privileged admission and dangerous configurations. The manifest, a SHA-256 value and possession of an SD card are NOT publisher authentication or capability grants. Use the existing runtime/loader authorization model; do not introduce signing as a presumed dependency.
4. Reuse normal preflight, verified staging, transactional install, rollback and recovery. Preserve prior complete generations and exact verified executable bytes. Install before activating through generic provider lifecycle; then apply allowlisted settings through existing APIs and check actual results.
5. Persist bounded atomic ledger: deployment ID/revision, canonical manifest digest, planned/installed versions, per-step results, completion/failure and recovery state. An interruption, reboot or media removal resumes idempotently or exposes diagnosable incomplete steps; do not redo successful installs, delete unrelated data or pretend multi-package global atomicity when only per-package atomic commits exist.
6. Only mark complete after package/version inventory, dependency state, activation/configuration checks and final comparison pass. Identical completed manifest on next boot is a no-op; changed revision requires explicit reapply/downgrade authorization.

If a failure occurs preserve a bootable generic platform and accessible recovery where physical port/medium permits. No implicit factory reset or deletion of unknown SD data. File, network, hashing and graph operations must declare caps, deadlines, retry behavior, progress and actual scheduler yields; watchdog resets alone are insufficient.

## 4. Security, privacy and excluded features

The manifest is *requested configuration*, not an authority token. Reject traversal, oversized/cyclic data, arbitrary remote URLs, inconsistent identity/versions, unauthorized source changes and unsafe hardware settings. All installed archives use ordinary integrity/ABI/import/permission validation. A manifest-provided hash is not proof of authorship. Do not introduce P-256 package signing, trust roots, anti-rollback key stores, security rearchitecture or secure boot merely to implement provisioning; separately authorize any such future change. Avoid plaintext portable Wi-Fi/API credentials by default and never place secrets in logs or ledger; credential onboarding requires an explicitly authorized secure mechanism. Provisioning must not flash firmware, alter partitions, override bootloader policy or perform irreversible pin/power changes without distinct explicit owner authorization.

## 5. U4 acceptance, not a U3 gate

At owner-initiated Release Qualification for U4 verify the actual compatible CAM and T5 profiles using exact artifacts: first-run offline manifest installs real packages through the ordinary manager; optional online source uses immutable U2 snapshot and fails safely without network; no core source rebuild for a novel camera-board driver; wrong board/profile and unavailable dependencies refuse before electrical activation; interruption/SD removal and rollback are ledger-correct; completed manifest does not replay and changed revision requires approval; final capability/inventory/settings match the request; bootstrap/recovery survive absent/corrupt drivers. Confirm functional camera capture and existing T5 Pro behavior separately. The **driverless CAM boot and stable headless idle are U3 acceptance**, not something U4 may defer or paper over.

**Explicit deferral for U1–U3:** preserve only generic reusable manager, port, profile and recovery interfaces; no first-run manifest parser/wizard, persistent provisioning ledger, camera driver, image generator, new CI/release gate or owner hardware-test demand. U4 implements the approved contract only after the owner advances milestones.
