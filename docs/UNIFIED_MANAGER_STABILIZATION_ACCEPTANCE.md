# Unified manager and hardware ELF stabilization acceptance

This work follows the merged foundations in PR #76 and PR #78. The PR containing this document **must not be merged by an agent**; the repository owner chooses when to merge after reviewing the complete diff and green current-head CI. No release or device-flashing action is implied by a merge.

## Implemented in the first driver integration slice

The existing Driver Manager's downloaded `driver.elf` and `manifest.json` remain compatible with their installed `/Drivers/<id>/` layout. Publication now uses the shared four-kind **ordinary package transaction** for that driver: canonical kind/ID/version/artifact, target-exclusive replacement lease, newer-semver-only upgrades, verified stage and target, previous-generation recovery, and tombstone logic in the shared engine. The old `.install` / `.previous` layout is recovered before the new `.pkg-stage` / `.pkg-previous` layout. An unfinished stage is refused and retained, never silently purged. Directory verification checks exact file inventory, reparses metadata, and rehashes actual ELF bytes. Installation never activates or authorizes a hardware driver.

This transitional adapter deliberately retains the existing driver's sidecar manifest and payload source so already-installed drivers and the current Driver Manager downloads can be tested without a breaking package format migration. It is not equivalent to a complete four-kind on-device manager or physical ELF activation.

## Required before claiming full unified-manager completion

- Connect the existing App Store, offline file browser, and Driver Manager to a source-independent canonical ordinary manifest/parser, SD/download entry reader, and HalStorage stage adapter, without breaking existing assets.
- Use one generic installed-generation inventory, update, recovery and uninstall API for app, driver, service and provider packages, including dependencies, current ABI compatibility, diagnostics, and active package pin/refusal behavior.
- Pass a manager-validated, privately owned executable and metadata snapshot to the PR #78 generic provider executor. Verify import declarations and relocation against the actual private bytes. Installation must not activate hardware or grant consumer rights.
- Remove competing compiled physical USB/I²C owners before enabling their ELF counterparts; prove USB power and hotplug, I²C transactions, IRQ/DMA quiescence, teardown and recovery on both supported boards. Do not claim a host test as physical validation.

## Device test protocol

1. Record the exact firmware commit, board type, SD payload versions and the boot serial log. Back up the SD card before testing package recovery.
2. Install a driver from the current catalog. Verify the manifest, ELF size/hash, displayed installed version and ability to use the unchanged driver after reboot.
3. Try an older or identical driver version: both must refuse replacement and preserve the prior version. Interrupt or corrupt the next staged update: the old verified generation must remain usable and an unfinished stage must not be auto-deleted.
4. With a mapped driver active, attempt an update: replacement must refuse without renaming the active files. Stop the driver and repeat the versioned upgrade.
5. Remove and reconnect a USB serial target and check powering, enumeration, baud configuration, data and teardown. Separately qualify physical USB/I²C ELFs only after firmware hardware-owner handover exists.
6. For each app, driver, service and provider package type, independently check offline SD and online installation, exact manifest/inventory validation, compatible dependencies, rollback/recovery and uninstall. Mark any unconnected UI or driver handoff **NOT IMPLEMENTED**, not PASS.

No mandatory package signatures, keys, signed provenance or cryptographic NVS floor belong to this acceptance gate. SHA-256 detects content mismatch, not publisher identity.
