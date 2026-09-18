# Deployment provisioning manifest — U4 implementation contract

**SPECIFICATION ONLY; U4, NOT U1–U3.** U3 first delivers stable UI-optional, driverless ESP32-S3-CAM boot. U4 implements declarative provisioning as described by [U4](NEXT_MILESTONE_PROVISIONING_AND_ESP32_S3_CAM.md) and [headless acceptance](HEADLESS_RUNTIME_AND_CAMERA_SERVICE_BOUNDARY.md). This document's earlier continuous camera streaming/autostart wording is withdrawn. One real camera image saved to an available accessible output suffices for the U4 proof. No interactive serial UI, GUI wizard, external client, IP connectivity or permanently running camera service is required.

## Operating model

An owner prepares a **verified, supported bootstrap medium or recovery/staging route** holding a bounded versioned desired-state manifest and optional ordinary package ZIPs. On first eligible boot or explicitly approved reapply, the UI-independent generic runtime verifies real board/CPU/port identity, compares installed inventory, selects the board profile, plans and installs necessary `application`, `driver`, `service` and `provider` packages through U1's **single** ordinary streaming ZIP/transaction manager, activates required drivers, applies allowlisted configuration and records an idempotent completion/recovery ledger. U2 independent GitHub releases can supply optional pinned packages but cannot be mandatory to bootstrap network access. No new package format, second installer, continuously running installer, automatic firmware flash or board-specific core behavior.

For the owner's actual CAM, confirm sensor/revision/pins/flash/PSRAM, provisionable media and output before freezing profile or file path. The runtime must not require SD if this board lacks SD, nor load a driver from its inaccessible medium to mount that same medium. An isolated minimal port/recovery reader may bootstrap package installation; it cannot expose a normal runtime hardware driver or race the installed provider. Missing manifest/media leaves normal recoverable driverless idle.

## Bounded manifest schema

Finalize schema, path(s), byte/depth/item/time/retry limits, version and source pin policy at U4 implementation. Required concepts: deployment ID/revision/schema, verified board/port/ABI and firmware compatibility; required/optional profile; four-kind package IDs, versions, immutable sources/assets and capabilities/dependencies; actual driver-interpreted bus/pin/power data, not core-compiled hardware maps; safe typed initial settings and explicit owner-approved operational intent. A CAM headless profile excludes display, touch, GUI Settings and springboard. A service package/startup policy **may** be represented generically for future use but U4 does not need to implement, install or autostart a camera streaming service. Installing a package does not grant device privileges or automatic execution.

Illustrative only; **not** a real CAM BOM, settled schema or executable manifest:

```json
{
  "schema_version": 1,
  "deployment_id": "example-cam",
  "revision": 1,
  "target": {"board_profile": "verified-cam-profile", "firmware": ">=1.2.0"},
  "packages": [{"kind": "driver", "id": "example-camera-driver", "version": "1.0.0"}],
  "package_source": {"mode": "verified-bootstrap-medium"},
  "settings": {}
}
```

## Execution, owner authorization and recovery

1. Detect eligible first run/explicit reapply. Verify **actual** port/board/profile facts, not a self-declared manifest identity; absent manifest is nonfatal.
2. Parse bounded input and produce an inspectable source/version/permission/dependency/configuration plan through the existing manager. Refuse incompatible board, wrong ABI, missing dependency, duplicate ID, source substitution, unauthorized power/pin/camera configuration and nonexistent output path before unsafe activation.
3. Owner approval uses a **real supported preparation/recovery authorization pathway** and existing loader/capability permissions; no headless GUI prompt or interactive serial shell is presumed. A manifest, SD card or SHA-256 hash does not itself constitute authority. Do not dump credentials or image contents to logs; no plaintext secrets in portable manifest/ledger.
4. Verify, stream/stage and transactionally install existing package archives through the ONE engine; activate eligible drivers and apply permitted settings only after prerequisites are ready. Persist bounded recoverable steps and installed results. Explicitly distinguish installed package from a healthy camera provider.
5. On the same completed manifest, skip reinstallation; changed revision or dangerous change requires explicit owner-approved reapply. On interruption, corruption, removal or power loss preserve last-good packages, safe peripheral state, unrelated files and stable boot; do not claim atomicity across multiple packages unless genuinely implemented.

U4's functional proof uses a small headless diagnostic/application/service **only as needed** to request `camera.*`, capture an actual frame via generic bounded stream/owned-buffer contract and write a retrievable test image to a verified output. No streaming server, external client, network provisioning, service reboot autostart or continuous image sequence is a completion gate. Serial/ROM/port logging is optional one-way progress/error reporting where verified; not an interactive setup system. File, hashing, network if actually used, and driver loops require explicit time/memory/item bounds, true scheduler yields, cancellation and safe cleanup.

## Acceptance and deferrals

At owner-initiated U4 Release Qualification, verify actual CAM provisioning, package inventory and activation, capture and retrieval of **one real test image**, appropriate permissions, missing/wrong media/driver, reboot/idempotence/rollback and T5 Pro functionality under its own profile. Driverless CAM boot is U3 acceptance and remains intact. No physical PASS is claimed before actual owner tests.

Outside U4: full firmware UI-to-ELF migration, interactive serial console, multi-client or continuous camera streaming, network portal/credential onboarding, automatic camera service autostart, unspecified new camera models, public third-party package onboarding, signed package framework and provisioning-driven firmware flashing. A future streaming service can be installed through this same generic architecture when separately requested.
