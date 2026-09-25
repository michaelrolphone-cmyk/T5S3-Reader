# Independent RiscRTE product releases

**Status:** implementation in progress on `feature/independent-product-releases`. The release workflow and index readers are wired for product-scoped tags; validate each product stream before enabling production releases.

## Goal

Firmware, applications, and drivers must be releasable and installable on independent schedules. A release in one product stream must not require rebuilding, renumbering, or republishing another stream. Apps and drivers also retain their own per-package manifest versions.

The existing combined release is a migration source only. New releases use immutable, product-scoped tags and a small mutable index that points to immutable assets.

## Version and tag identity

| Product | Version authority | Tag format | Release contents |
| --- | --- | --- | --- |
| Firmware | `[riscrte] version` in `platformio.ini` | `firmware-v<version>` | Firmware flash/OTA images and matching ELF; the index records size and SHA-256 |
| App | That app's `Apps/<name>.json` stable ID and `version` | `app-<id>-v<version>` | One app's `.elf` and matching `.json` manifest; the index records size and SHA-256 |
| Driver | That driver's package manifest stable ID and `version` | `driver-<id>-v<version>` | One canonical package's driver ELF, package descriptor, provider ABI, and privileged import inventory; the index records size and SHA-256 |

App and driver versions continue to use numeric MAJOR.MINOR.PATCH and must increase for every changed distributable package, under the existing version policies. Firmware version changes do not change app or driver versions. Package compatibility requirements such as minimum firmware, driver ABI, architecture, and capability API remain separate fields and are still enforced.

A release tag is immutable. A package ID/version pair cannot be reused with different bytes or metadata. An app release contains its manifest and ELF. A canonical driver release contains the complete four-file package inventory. Any declared app resources must also be included in that app's release. Firmware releases contain no app or driver payloads.

## Release index

The runtime needs to find the newest release of each product without relying on GitHub's repository-wide `releases/latest` selection. Maintain one bounded `release-index.json` on a dedicated `release-index` branch. The index is updated only after a product release has been created successfully. Updates are serialized, preserve entries for other products, and point to immutable release tags and asset names.

The index contains:

- the latest firmware tag, version, asset names, sizes, and SHA-256 values;
- the latest package release for each stable app or driver ID, with kind, package version, compatibility metadata, release tag, asset name, size, and SHA-256;
- a schema version and bounded entry counts.

The index is a locator. It does not authorize code or hardware access. Installers must pin downloads to the indexed immutable release tag, validate package identity and compatibility, check the declared size and SHA-256, and retain their existing transactional install and rollback behavior.

The App Store reads only app entries. Driver Manager reads only driver entries. Firmware update code reads only the firmware entry. The unified package manager consumes app and driver entries through the same generic index/parser and filters by package kind. Offline SD installation remains independent of index access.

## Release request and build behavior

A manual `Cut RiscRTE release` workflow request identifies exactly one product (`firmware`, `apps`, or `drivers`) and an app or driver ID when applicable. The workflow detects firmware version from `[riscrte] version` in `platformio.ini`, and app or driver version from the selected package manifest. The version is shown in the workflow logs and used to form the immutable tag; it is not a user input. The workflow builds only the selected stream, publishes only the selected package assets, then updates the index.

The workflow sequence is:

1. Validate the product selector, stable ID, version, source manifest, compatibility fields, and immutable tag identity.
2. Build and test the selected product. The driver build validates its provider package set, then selects one stable ID for publication. Produce release assets plus SHA-256/size metadata.
3. Create the immutable product-scoped GitHub Release.
4. Atomically update `release-index.json` on `release-index` from the last published index, changing only the selected firmware pointer or package ID entry.
5. Verify the index points to the created tag and exact asset digest.

A failed build or release leaves the index unchanged. A failed index update is retryable: rerun the same product request, which verifies the existing release's primary asset digest and fills any missing release assets without overwriting them.

## Bulk app and driver publishing

Run the manual `Publish updated RiscRTE packages` workflow to compare every built app and canonical driver package with its latest entry in `release-index.json`. It creates an individual immutable release for each package whose manifest version is newer, including packages that have not been released before. Packages at the same version or behind their latest release are skipped. Firmware is not built or published by this workflow.

The workflow builds all app manifests and the canonical driver package catalog, validates the full candidate batch and index size before publishing, then publishes candidates sequentially. After each release succeeds, it updates that package's index entry. The workflow runs on the selected branch and shares release serialization with the single-product release workflow.

## Migration and acceptance

Keep existing combined releases intact as historical assets. During cutover, the readers may support the existing aggregate catalog as a bounded fallback while preferring the new index. Remove that fallback only after the new index and all three readers ship together. Do not rename or duplicate package IDs to create release channels.

The release-index updater enforces at most 128 apps, 64 drivers, and 64 KiB total, matching the on-device readers. Acceptance requires:

- publishing a driver updates only its stable ID and index entry; the firmware version and firmware assets do not change;
- publishing an app updates only its stable ID and index entry; other app/driver versions and firmware do not change;
- publishing firmware updates only the firmware entry and creates no app/driver assets;
- package managers resolve the highest compatible indexed version per stable ID and fetch from that entry's exact release tag;
- corrupted, missing, oversized, stale, or mismatched index entries fail closed without replacing installed packages;
- legacy combined releases remain readable during migration, and offline package workflows still work.
