# Independent RiscRTE product releases

**Status:** release publication is driven by the `Cut RiscRTE release` workflow.
The U1 working tree is migrating distribution to ordinary `.rte.zip` bundles;
the [bundled-package contract](BUNDLED_PACKAGE_ARCHIVE_AND_INSTALL_LAYOUT.md)
overrides historical loose-asset descriptions. A local successful build or
record update does not publish a release or establish device compatibility.

## Goal

Firmware, applications, and drivers must be releasable and installable on independent schedules. A release in one product stream must not require rebuilding, renumbering, or republishing another stream. Apps and drivers also retain their own per-package manifest versions.

The existing combined release is a migration source only. New releases use immutable, product-scoped tags and a small mutable index that points to immutable assets.

## Version and tag identity

| Product | Version authority | Tag format | Release contents |
| --- | --- | --- | --- |
| Firmware | `[riscrte] version` in `platformio.ini` | `firmware-v<version>` | Firmware flash/OTA images and matching ELF; the index records size and SHA-256 |
| App | That app's `Apps/<name>.json` stable ID and `version` | `app-<id>-v<version>` | Current independent app path still uses `.elf`/`.json`; ordinary bundle conversion remains U1 work |
| Driver | That driver's package manifest stable ID and `version` | `driver-<id>-v<version>` | One `driver-<id>-<version>-<architecture>.rte.zip` containing its ordinary manifest, executable, provider ABI, import inventory and any declared resources; the index records archive size and SHA-256 |

App and driver versions continue to use numeric MAJOR.MINOR.PATCH and must increase for every changed distributable package, under the existing version policies. Firmware version changes do not change app or driver versions. Package compatibility requirements such as minimum firmware, driver ABI, architecture, and capability API remain separate fields and are still enforced.

A release tag is immutable. A package ID/version pair cannot be reused with different bytes or metadata, including when converting a historical loose driver release to ZIP. That conversion needs a strictly newer manifest version. Historical app releases contain their manifest and ELF; historical driver releases contain the complete four-file package inventory. Keep those assets intact. Any declared resources must be included in their package's release. Firmware releases contain no app or driver payloads.

## Release index

The runtime needs to find the newest release of each product without relying on GitHub's repository-wide `releases/latest` selection. Maintain one bounded `release-index.json` on a dedicated `release-index` branch. The index is updated only after a product release has been created successfully. Updates are serialized, preserve entries for other products, and point to immutable release tags and asset names.

The index contains:

- the latest firmware tag, version, asset names, sizes, and SHA-256 values;
- the latest package release for each stable app or driver ID, with kind, package version, compatibility metadata, release tag, asset name, size, and SHA-256;
- a schema version and bounded entry counts.

The index is a locator. It does not authorize code or hardware access. Installers must pin downloads to the indexed immutable release tag, validate package identity and compatibility, check the declared size and SHA-256, and retain their existing transactional install and rollback behavior.

### Current driver records and historical compatibility

The outer index remains `schema: 1`. New driver records explicitly carry
`format: "rte.zip"` and `architecture`, alongside the existing `id`, `version`,
`tag`, `asset`, `url`, `size`, `sha256` and `kind: "driver"`. Their `manifest`
is the exact ordinary root `.package.json` object, with `schema`, `kind`, `id`,
`version`, `architecture`, `artifact`, `min_runtime_api`, `entries` and `requires`.
The outer size/hash describe the whole archive, never just its executable.
Architecture, version, identity, filename and immutable URL must agree.

Historical records have no `format` discriminator, point to `<id>--driver.elf`
and retain their original `capability`, `api` and four-file `files` manifest.
The updater accepts and preserves those records; it does not reinterpret them
as archives or rewrite untouched entries. Current/legacy metadata mixtures,
unknown formats, mismatched kinds/architectures and loose/archive filename
conflicts are rejected. A replay of identical indexed content is idempotent;
same-version changed content and downgrades remain blocked across both formats.

The current driver bundle validator mirrors ordinary schema-1 structural
rules: exact known fields, canonical bounded identity/version, supported CPU,
flat lowercase file names, one declared executable, provider ABI/import
metadata, lowercase SHA-256, exact boolean/integer types, and bounded unique
capability requirements. Current ZIP limits are 16 payload entries, 1 MiB per
entry, 4 MiB total including the manifest, a 4096-byte manifest, and 16
requirements. Nested schema-2 resources remain separate U1 implementation work.
The builder additionally verifies actual archive and payload bytes; a metadata
validator alone cannot establish their integrity or grant execution privileges.

App Store and firmware readers filter their own product entries. Historical
driver readers cannot consume a ZIP merely because the outer index schema is
unchanged: bundle-capable readers must explicitly route archive downloads to
the ordinary package installer. The generic `package-catalog.json` remains
the four-kind archive source contract. Complete reader/publication integration
before treating this compatibility layer as end-to-end release readiness.
Offline SD installation remains independent of index access.

## Release request and build behavior

The manual `Cut RiscRTE release` workflow takes no product, package ID, or version inputs. Its GitHub Actions graph separates planning, firmware build, app build, driver build, and publication into distinct jobs. Each product build job runs only when the plan contains a newer version for that product. Planning compares source versions with the latest entries in `release-index.json` before any product build. It builds firmware only when its configured version is newer, and builds only the apps and canonical drivers whose manifest versions are newer. A product is published only when its current version is newer than its indexed release, or when that product has no indexed release yet. If nothing is newer, the workflow reports that no releases are needed.

Firmware version comes from `[riscrte] version` in `platformio.ini`; app versions come from app manifests; driver versions come from canonical package manifests. Each changed firmware, app, or driver package receives its own immutable product-scoped tag and release assets. Firmware is published first when multiple product versions changed, and the index is updated after each successful release. A failed build leaves the index unchanged. Publication retries transient GitHub API errors, verifies and fills assets for a partially created release, and updates the index only after each release succeeds.

The workflow validates all candidates and the final index budget before publishing the first release. Run it from the branch whose code and package manifests should be built and released.

The independent workflow downloads the same immutable run plan in each selected
build job, invokes only that product's selective builder, and runs the offline
`verify_release_plan.py` gate before uploading artifacts. Driver custody carries
only exported ZIPs plus the generic catalog; record verification does not depend
on hidden intermediate files. After restoring all selected artifacts, the publish
job repeats the offline gate before invoking the existing index/version-checked
publisher. A selected product must have a successful build job; skipped or failed
selected builds cannot pass the publication gate. The existing `contents: write`
token permission is unchanged, and these source changes authorize no workflow
execution, release or live index mutation.

The offline gate rejects invalid/duplicate plans, missing or altered restores,
wrong whole-archive records, symlinked assets, app sidecar integrity/size failures,
and mismatched firmware source versions or OTA aliases. Firmware/app historical
formats remain explicit compatibility paths; this workflow repair does not claim
that independent apps have completed U1 ordinary-bundle migration.

### Remaining online routing

The online ordinary-package consumer currently pins one aggregate catalog tag
for all rows. Independently released ZIPs require a per-package immutable tag/URL
route and version-safe merging with aggregate/legacy entries. Do not replace its
endpoint with the release index before that complete route exists. The current
aggregate `package-catalog.json` now receives the same cache revalidation as other
mutable catalogs; selected versioned ZIP URLs remain unchanged. No live catalog
or deployed firmware was switched during this repair.


## Temporary third-party app listing

The App Store temporarily includes the RiscRTE ELF from the T5S3-GameBoy repository through the shared release index. A scheduled workflow checks the upstream stable release every five minutes, validates the published `gameboy.json` identity, firmware requirement, ELF size and SHA-256, then updates only the GameBoy app entry on the `release-index` branch. The app remains hosted by the GameBoy release, so GameBoy app updates do not require a RiscRTE firmware or app release.

The device continues to fetch one bounded release index during App Store refresh; it does not make a second live request to the GameBoy repository. The temporary trust rule accepts only the `gameboy` app from `michaelrolphone-cmyk/T5S3-GameBoy`. Replace this rule and the polling workflow when general third-party provider capabilities are available.

## Migration and acceptance

Keep existing combined releases intact as historical assets. During cutover, the readers may support the existing aggregate catalog as a bounded fallback while preferring the new index. Remove that fallback only after the new index and all three readers ship together. Do not rename or duplicate package IDs to create release channels.

The release-index updater enforces at most 128 apps and 64 drivers and writes
compact JSON. It has no fixed 64 KiB serialization ceiling; individual readers
must retain their own bounded allocation/filtering behavior. Acceptance requires:

- publishing a driver updates only its stable ID and index entry; the firmware version and firmware assets do not change;
- publishing an app updates only its stable ID and index entry; other app/driver versions and firmware do not change;
- publishing firmware updates only the firmware entry and creates no app/driver assets;
- package managers resolve the highest compatible indexed version per stable ID and fetch from that entry's exact release tag;
- corrupted, missing, oversized, stale, or mismatched index entries fail closed without replacing installed packages;
- legacy combined releases remain readable during migration, and offline package workflows still work.
