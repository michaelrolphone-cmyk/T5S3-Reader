# Publishing RiscRTE firmware and native-app releases

Update `platformio.ini` `[riscrte] version` and finish the firmware/app changes
first. Native ELF apps are part of the same tagged RiscRTE release: the release
workflow builds every `Apps/**/*.c`, validates its JSON manifest, and publishes
each `.elf` + `.json` pair beside the firmware images.

**Mandatory per-app release identity:** Follow [Application Version Policy](APP_VERSION_POLICY.md). Before releasing or completing any app update, compare every modified app against its prior published and target-branch versions; its source `Apps/<app>.json` `version` MUST have increased numerically. Update each affected app independently, even when firmware's version stays the same. Changing `min_firmware_version` does not count; only raise that floor for actual firmware compatibility. A published `(application ID, version, target ABI)` cannot be reused for changed ELF/manifest/resources, even in a newer firmware release. Record `app: old -> new` in the PR/release summary. Verify that staged sidecar, online/generic catalogs and package inventory use the same app version. Do not rely on ELF SHA-256 stamping or the current manifest syntax validator to detect version reuse: neither compares the version to previously distributed content. A source/release baseline comparison is required, manually until the specified automated guard has been implemented and demonstrated.

Before requesting a release, run the same native-app build locally when possible:

```sh
python scripts/build_all_apps.py
```

This validates every shipped manifest, verifies that its `file_name` matches the
emitted ELF basename, checks the exported `app_main`, runs the native loader test,
and stages pairs under `dist/apps/`. This build is necessary but does not currently establish that a changed app has a higher version than the previous release.

After checking CI, commit this file to master:
`.github/release-request.json`.

Example (use a new version matching `platformio.ini`):

```json
{
  "enabled": true,
  "tag": "v1.2.9",
  "commit_firmware": true
}
```

Only changes to that request file on master trigger automatic publishing.
GitHub Actions validates the request, builds `gh_release`, builds/validates all
native apps and manifests, optionally commits the merged USB firmware image into
`firmware/`, creates the tag, and publishes the release assets. No separate app
release workflow or manual workflow dispatch is required.

The build uses the request commit; commit all source and manifest changes before
or together with the request. Avoid advancing master during a release when
`commit_firmware` is true: a non-fast-forward firmware push will fail safely.

The initial request had `enabled=false` so installing this mechanism validated
the trigger without publishing firmware. Leaving an enabled request in place
does not publish on unrelated source commits. For the next release, change its
tag to the new version. Existing tags are rejected to prevent replacing releases.

The Actions **Cut RiscRTE release** manual workflow remains available with `tag`
and `commit_firmware` inputs. It uses the same version and existing-tag checks.
The workflow uses the built-in `GITHUB_TOKEN` with `contents:write`; no extra
secret is required. Check the Actions run and release assets before reporting
success.

## Product and compatibility naming

**RiscRTE** is the product/platform name used by version metadata, build tooling,
CI artifacts, release titles, and versioned firmware assets. **CrossPoint** names
the ebook-reader subsystem and may also appear in legacy source identifiers or
compatibility markers that have not yet been renamed. Do not introduce new
overall-firmware artifacts, release metadata, or version fields under the
CrossPoint name.

The canonical versioned T5S3 release assets are:

```text
riscrte_lilygo_t5s3_<version>-app.bin
riscrte_lilygo_t5s3_<version>.bin
riscrte_lilygo_t5s3_<version>.elf
```

`firmware-t5s3-pro.bin` is retained as an unversioned compatibility filename for
OTA/SD consumers that currently expect it.

## Native app release contract

`python scripts/build_all_apps.py` is the canonical app build. For each source:

```text
Apps/foo.c
Apps/foo.json
```

it emits:

```text
dist/apps/foo.elf
dist/apps/foo.json
```

Nested sources are flattened (`Apps/games/foo.c` -> `games__foo.elf`), and the
manifest must declare that exact output basename. Output collisions fail the
build.

The tagged release publishes every staged `.elf` and `.json`. The firmware App
Store reads the repository's latest release and exposes only safe ELF assets that
have a matching valid manifest. Do not manually publish a first-class app ELF
without its matching JSON sidecar; the App Store will ignore incomplete pairs.

An app's `version` is its independent update identity; the manifest's
`min_firmware_version` is the separate firmware compatibility floor enforced
by the springboard/App Store. Neither replaces append-only `struct_size`
checks inside the app for individual native API members.

## Firmware image types

`firmware-t5s3-pro.bin` and `riscrte_lilygo_t5s3_<version>-app.bin` are OTA/SD
application images. `riscrte_lilygo_t5s3_<version>.bin` is the merged USB flash
image at `0x0` and must not be used for OTA/SD updates. The matching `.elf` is a
non-flashable debug-symbol image.
