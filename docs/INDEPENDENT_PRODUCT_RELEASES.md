# Independent RiscRTE product releases

**Status:** release publication is driven by the `Cut RiscRTE release` workflow.

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

The manual `Cut RiscRTE release` workflow takes no product, package ID, or version inputs. Each run compares source versions with the latest entries in `release-index.json` before building. It builds firmware only when its configured version is newer, and builds only the apps and canonical drivers whose manifest versions are newer. A product is published only when its current version is newer than its indexed release, or when that product has no indexed release yet. If nothing is newer, the workflow reports that no releases are needed.

Firmware version comes from `[riscrte] version` in `platformio.ini`; app versions come from app manifests; driver versions come from canonical package manifests. Each changed firmware, app, or driver package receives its own immutable product-scoped tag and release assets. Firmware is published first when multiple product versions changed, and the index is updated after each successful release. A failed build or release leaves the affected index entry unchanged; rerunning verifies any already-created immutable release before completing its index update.

The workflow validates all candidates and the final index budget before publishing the first release. Run it from the branch whose code and package manifests should be built and released.

## Migration and acceptance

Keep existing combined releases intact as historical assets. During cutover, the readers may support the existing aggregate catalog as a bounded fallback while preferring the new index. Remove that fallback only after the new index and all three readers ship together. Do not rename or duplicate package IDs to create release channels.

The release-index updater enforces at most 128 apps, 64 drivers, and 64 KiB total, matching the on-device readers. Acceptance requires:

- publishing a driver updates only its stable ID and index entry; the firmware version and firmware assets do not change;
- publishing an app updates only its stable ID and index entry; other app/driver versions and firmware do not change;
- publishing firmware updates only the firmware entry and creates no app/driver assets;
- package managers resolve the highest compatible indexed version per stable ID and fetch from that entry's exact release tag;
- corrupted, missing, oversized, stale, or mismatched index entries fail closed without replacing installed packages;
- legacy combined releases remain readable during migration, and offline package workflows still work.
