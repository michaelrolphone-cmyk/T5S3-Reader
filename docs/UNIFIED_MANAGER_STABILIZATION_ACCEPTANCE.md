# Unified manager and hardware ELF stabilization acceptance

This work follows the merged foundations in PR #76 and PR #78. The PR containing this document **must not be merged by an agent**; the repository owner chooses when to merge after reviewing the complete diff and green current-head CI. No release or device-flashing action is implied by a merge.

## Implemented: transitional live Driver Manager

The existing Driver Manager's downloaded `driver.elf` and `manifest.json` remain compatible with their installed `/Drivers/<id>/` layout. Publication uses the shared four-kind **ordinary package transaction** for drivers: canonical kind/ID/version/artifact, target-exclusive replacement lease, newer-semver-only upgrades, verified stage and target, previous-generation recovery, and tombstone logic in the shared engine. The old `.install` / `.previous` layout is recovered before the new `.pkg-stage` / `.pkg-previous` layout. An unfinished stage is refused and retained, never silently purged. Directory verification checks exact file inventory, reparses metadata, and rehashes actual ELF bytes. Installation never activates or authorizes a hardware driver.

The live download intake now serializes refresh/install mutations, snapshots the selected catalog entry, checks the installed verified version and recovery state before network transfer, and refuses an existing `/Drivers/.driver-manager.part` rather than deleting an interrupted or unknown file. Same-version and downgrade candidates do not start a download. The native Driver Manager app compares numeric versions, correctly recognizes installed versions while initially building rows, suppresses downgrade actions and displays distinct status for an installed version newer than the catalog. Host tests cover the actual app's row-building and action paths, not merely an isolated numeric helper.

The transitional adapter deliberately retains the existing driver's sidecar manifest and payload source so installed drivers and current downloads can be tested without a breaking package format migration. It is not equivalent to a complete four-kind on-device manager or physical ELF activation.

## Required before claiming full unified-manager completion

- Connect the existing App Store, offline file browser, and Driver Manager to a source-independent canonical ordinary manifest/parser, SD/download entry reader, and HalStorage stage adapter, without breaking existing assets.
- Use one generic installed-generation inventory, update, recovery and uninstall API for app, driver, service and provider packages, including dependencies, current ABI compatibility, diagnostics, and active package pin/refusal behavior.
- Pass a manager-validated, privately owned executable and metadata snapshot to the PR #78 generic provider executor. Verify import declarations and relocation against the actual private bytes. Installation must not activate hardware or grant consumer rights.
- Remove competing compiled physical USB/I²C owners before enabling their ELF counterparts; prove USB power and hotplug, I²C transactions, IRQ/DMA quiescence, teardown and recovery on both supported boards. Do not claim a host test as physical validation.

## Device test protocol

1. Record the exact firmware commit, board type, SD payload versions and boot serial log. Back up the SD card before testing recovery.
2. Install a driver from the current catalog. Verify manifest and ELF size/hash, displayed installed version and ability to use the unchanged driver after reboot.
3. Reopen the catalog with the same release: the row must say installed and offer no update. Install a newer version: the row must say Update. With a newer version already installed than the catalog, it must show installed newer and no update. Test numeric `1.10.0` versus `1.9.99`, not string ordering.
4. Attempt same-version and downgrade installs through the firmware API: reject before downloading, preserve installed bytes and leave no new stage. Plant a sentinel `/Drivers/.driver-manager.part`: installation must refuse without deleting/changing it; remove this sentinel manually only after inspection. An existing `.pkg-stage` must also be retained, not overwritten.
5. Interrupt or corrupt the next staged update: the old verified generation must remain usable and an unfinished stage must not be auto-deleted. An installed package with corrupt ELF/manifest must be refused, not treated as a fresh writable target.
6. With a mapped driver active, attempt an update: replacement must refuse without renaming active files. Stop the driver and repeat the versioned upgrade. Reboot with a pending old/new-format backup and inspect recovery behavior.
7. Remove and reconnect a USB serial target and check powering, enumeration, baud configuration, data and teardown. Separately qualify physical USB/I²C ELFs only after firmware hardware-owner handover exists.
8. For each app, driver, service and provider package type, independently check offline SD and online installation, exact manifest/inventory validation, compatible dependencies, rollback/recovery and uninstall. Mark any unconnected UI or driver handoff **NOT IMPLEMENTED**, not PASS.

An existing incomplete `.driver-manager.part` is intentionally left for user inspection; this revision does not add a recovery/discard UI for it. CI and host tests cannot substitute for device or deliberate power-interruption testing. No mandatory package signatures, keys, signed provenance or cryptographic NVS floor belong to this acceptance gate. SHA-256 detects content mismatch, not publisher identity.
