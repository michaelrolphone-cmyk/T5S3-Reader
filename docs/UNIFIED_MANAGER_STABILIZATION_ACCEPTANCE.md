# Unified manager and hardware ELF stabilization acceptance

This work follows merged PRs #76 and #78. This PR must remain unmerged until the repository owner explicitly decides otherwise. Neither a merge nor green CI authorizes a release or flashing.

## Implemented: live Driver Manager compatibility slice

The current Driver Manager retains `/Drivers/<id>/driver.elf` and `manifest.json`. Publication uses the shared four-kind ordinary transaction: canonical kind/ID/version/artifact, exclusive replacement lease, newer-semver upgrades, verified stage/target, previous-generation recovery, and uninstall tombstone machinery. Existing `.install`/`.previous` recovery precedes `.pkg-stage`/`.pkg-previous` recovery. Stage, target and previous generation are verified against their own retained metadata, exact inventory, ELF structure/size and SHA-256. Installation does not start hardware or grant privileges.

Catalog refresh and install are serialized; the selected metadata is copied across network callbacks. Intake checks installed integrity and version, pending files and stages, unresolved backup/removal, and current mapping pins *before* Wi-Fi/download. The exclusive lease at publication still resolves races with loaders. The HTTP downloader exclusively creates `.part` files in both native-stream and HTTPClient fallback paths. Its stream failure path deletes only a stage that this transfer actually created, not another writer's pre-existing file.

The Driver Manager app uses numeric version comparisons; the actual row-building and actions are host-tested for installed/current/newer/invalid states.

## Implemented: ordinary staged recovery engine, not yet user-facing

`PackageOrdinaryStageRecovery.h` adds common four-kind read-only stage inspection, retry of a validated retained stage without a second download, and explicit selective discard. Review diagnoses missing, corrupt, stale, mapped and unresolved generation states. Retry uses the existing publication path and its own lease and re-verification rather than treating an earlier read-only inspection as an authorization token. Discard refuses pending backup/removal or mapped packages and requires a caller-provided inventory check and selective purge that cannot remove unmanaged files.

`DriverStageActions.cpp` connects these operations to the *actual driver storage layout*: it enumerates known stage entries, rereads a bounded `manifest.json`, calls the existing real ELF/SHA-256 validator, and deletes only known stage entries with the manifest last. A legacy `.install` or `.previous` generation blocks this new-format recovery interface. These firmware functions are available for the next UI bridge integration, but the current Driver Manager app still does **not** expose inspection, retry or discard. No automatic stage deletion was enabled. The loose download `/Drivers/.driver-manager.part` is distinct from a `.pkg-stage` directory and still lacks a user-facing recovery flow.

Host tests cover all four package kinds, non-mutating inspection, stale/corrupt stages, unknown entries, partial-cleanup failure, active mapping and interrupted backup/removal. Firmware CI compilation does not prove physical SD interruption behavior.

## Required before claiming complete unified package manager

- Connect app, driver, service and provider assets to one canonical bounded on-device manifest parser, offline SD and online source adapters, HalStorage stage writer, inventory, update and uninstall lifecycle; migrate App Store and file browser without breaking working assets.
- Expose an explicit Driver Manager recovery screen for `.part` and `.pkg-stage`: inspect retained files and identity, choose retry where validated, and require confirmation for selective discard. Serialize the new live actions with existing catalog/install mutations. Do not silently delete unknown files.
- Pass privately owned, manager-validated ELF bytes and metadata to the #78 generic provider executor; independently enforce import/relocation policy and runtime grants. Installation does not equal activation.
- Remove competing compiled USB/I²C hardware owners before enabling physical ELF counterparts; verify power/hotplug, I²C, IRQ/DMA shutdown and recovery on both supported boards.

## Physical acceptance protocol

1. Record exact firmware head, board, SD asset versions and boot serial log. Back up the SD card before fault injection. PR-only functionality cannot be tested in older `master` firmware.
2. Install a driver; verify retained manifest/ELF SHA-256 and boot persistence. Reopen the catalog to verify Installed/Update/Installed newer and numeric `1.10.0` versus `1.9.99`.
3. Reject same-version/downgrade before downloading. Plant a backed-up `/Drivers/.driver-manager.part` sentinel; test both stream and HTTPClient fallback without altering it. Inspect and remove the sentinel manually until a recovery UI is connected.
4. Stage a valid newer driver, power off before publication, and verify the old version remains intact. Once UI recovery is connected, retry the retained validated stage without network. Test stale, corrupt, unknown-entry and partially purged stages; verify discard requires explicit confirmation and only touches known staged entries.
5. With an active mapped ELF, reject replacement before download; also race loading after early preflight and confirm the publication-time lease refuses the rename. Test interrupted backup/restore and uninstall tombstone states without resurrecting removed files.
6. Connect and disconnect USB serial devices; verify sustained power, enumeration, baud selection, data and teardown. Separately qualify hardware-owning USB/I²C ELFs only after actual hardware handover exists.
7. Test each of four package kinds from both SD and download, incompatible metadata, dependency failure, corruption, reboot during rename, installed inventory and uninstall. Mark any path not wired on device NOT IMPLEMENTED, not PASS.

A digest detects mismatch but is not publisher authentication. No signing keys, mandatory signatures, signed provenance or cryptographic NVS security floors are acceptance gates. Host and CI verification are not physical-device acceptance.
