# Independent RiscRTE product releases

**Status:** implementation target for independent RiscRTE delivery. This document defines the release contract; it does not claim that the current release workflow or clients already implement it.

## Goal

Firmware, applications, and drivers must be releasable and installable on independent schedules. A release in one product stream must not require rebuilding, renumbering, or republishing another stream. Apps and drivers also retain their own per-package manifest versions.

The existing combined release is a migration source only. New releases use immutable, product-scoped tags and a small mutable index that points to immutable assets.

## Version and tag identity

| Product | Version authority | Tag format | Release contents |
| --- | --- | --- | --- |
| Firmware | `[riscrte] version` in `platformio.ini` | `firmware-v<version>` | Firmware flash/OTA images, matching ELF, checksums, release notes |
| App | That app's `Apps/<name>.json` stable ID and `version` | `app-<id>-v<version>` | One app's `.rte.zip` and checksums |
| Driver | That driver's manifest stable ID and `version` | `driver-<id>-v<version>` | One driver's `.rte.zip` and checksums |

App and driver versions continue to use numeric MAJOR.MINOR.PATCH and must increase for every changed distributable package, under the existing version policies. Firmware version changes do not change app or driver versions. Package compatibility requirements such as minimum firmware, driver ABI, architecture, and capability API remain separate fields and are still enforced.

A release tag is immutable. A package ID/version pair cannot be reused with different bytes or metadata. An app or driver release must contain the complete package archive, including its manifest, ELF, and declared resources. Firmware releases contain no app or driver payloads.

## Release index

The runtime needs to find the newest release of each product without relying on GitHub's repository-wide `releases/latest` selection. Maintain one bounded `release-index.json` on a dedicated `release-index` branch. The index is updated only after a product release has been created successfully. Updates are serialized, preserve entries for other products, and point to immutable release tags and asset names.

The index contains:

- the latest firmware tag, version, asset names, sizes, and SHA-256 values;
- the latest package release for each stable app or driver ID, with kind, package version, compatibility metadata, release tag, archive name, size, and SHA-256;
- a schema version and bounded entry counts.

The index is a locator. It does not authorize code or hardware access. Installers must pin downloads to the indexed immutable release tag, validate package identity and compatibility, check the declared size and SHA-256, and retain their existing transactional install and rollback behavior.

The App Store reads only app entries. Driver Manager reads only driver entries. Firmware update code reads only the firmware entry. The unified package manager consumes app and driver entries through the same generic index/parser and filters by package kind. Offline SD installation remains independent of index access.

## Release request and build behavior

A release request identifies exactly one product: firmware, one app ID, or one driver ID. Validation checks the requested version against that product's authoritative version and rejects an existing release tag or reused ID/version. Product-specific workflows build and validate only the requested payload and do not stage artifacts from the other product streams.

The workflow sequence is:

1. Validate the product selector, stable ID, version, manifest, compatibility fields, and absence of a conflicting immutable tag/version.
2. Build the selected payload, run its applicable package/firmware checks, and produce deterministic release assets plus SHA-256/size metadata.
3. Create the immutable product-scoped GitHub Release.
4. Atomically update `release-index.json` on `release-index` from the last published index, changing only the selected firmware pointer or package ID entry.
5. Verify the index points to the created tag and exact asset digest.

A failed build or release leaves the index unchanged. A failed index update is retryable and must not recreate or overwrite the immutable product release.

## Migration and acceptance

Keep existing combined releases intact as historical assets. During cutover, the readers may support the existing aggregate catalog as a bounded fallback while preferring the new index. Remove that fallback only after the new index and all three readers ship together. Do not rename or duplicate package IDs to create release channels.

Acceptance requires:

- publishing a driver updates only its stable ID and index entry; the firmware version and firmware assets do not change;
- publishing an app updates only its stable ID and index entry; other app/driver versions and firmware do not change;
- publishing firmware updates only the firmware entry and creates no app/driver assets;
- package managers resolve the highest compatible indexed version per stable ID and fetch from that entry's exact release tag;
- corrupted, missing, oversized, stale, or mismatched index entries fail closed without replacing installed packages;
- legacy combined releases remain readable during migration, and offline package workflows still work.
