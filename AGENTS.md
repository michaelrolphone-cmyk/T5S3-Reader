# Agent instructions

## Tagged firmware releases

Repository: michaelrolphone-cmyk/T5S3-Reader. Release branch: master.

When the user asks to publish a firmware release, use the existing release-request
mechanism. A workflow-dispatch tool is not required. Read docs/RELEASING.md and
.github/workflows/release.yml for the current implementation before publishing.

1. Inspect the current master branch, existing tags/releases, and CI results.
2. Complete the requested firmware changes and update [crosspoint] version in
   platformio.ini to the intended new version. Verify relevant CI before release.
3. Commit .github/release-request.json on master with enabled=true, tag set to
   the matching new v-prefixed version, and commit_firmware=true (unless the user
   requests otherwise). Commit source changes before or together with the request.
4. This file change automatically triggers Cut release. GitHub Actions builds
   gh_release, builds all native ELF apps and manifests, commits the merged
   firmware when requested, creates the tag, and publishes firmware plus paired
   `.elf`/`.json` app assets.
5. Monitor the push-triggered workflow run for the request commit, inspect failed
   job logs if necessary, and verify the published release and assets before
   reporting success. Provide the release URL. Do not require a manual external
   Run workflow or Publish release action when this mechanism is available.

Existing tags are rejected. Keep the requested version and platformio.ini in
sync. Avoid advancing master during a release with commit_firmware=true because
its firmware commit may otherwise fail to push. Unrelated source commits do not
trigger publishing even when the request file remains enabled.

The mechanism was installed and its disabled-request trigger validated on
2026-09-12 (run 34726065234); this was not an end-to-end publishing test.
Do not publish merely because these instructions exist: act on a user release
request. Repository permissions and available tools must still be checked in
future sessions.

firmware-t5s3-pro.bin and the versioned -app.bin are OTA/SD app images. The merged
versioned .bin is a USB image at 0x0 and must not be used for OTA/SD updates.

## Adding or extending apps

For a first-class SD-installable application, read
[docs/NATIVE_APPS.md](docs/NATIVE_APPS.md) first. Native apps under `Apps/` are
now the preferred extension path when the required capability is available
through the versioned ELF APIs. Each shipped app source has a sibling JSON
manifest; use `scripts/build_all_apps.py` for release-equivalent validation.

The native framework currently exposes separate versioned UI/session, storage,
system-time, and system-UI service tables. It also supports installed-app
manifests, the ELF springboard, App Store catalog/install services, shared Font
Awesome rendering, settings integration, SD persistence, and firmware keyboard
handoff/resume. Check the exact current headers and use append-only `struct_size`
checks for the members an app requires.

Use [docs/ADDING_APPS.md](docs/ADDING_APPS.md) when a feature genuinely needs to
be compiled into the firmware as a C++ `Activity` or needs firmware internals not
yet exposed through a native host API. That guide traces the built-in Timecard
and Ask architecture through ActivityManager, Home routing, rendering, child
dialogs, storage, networking, localization, and builds. Keep the Home count,
labels, icons, and activation indices synchronized; use the documented lifecycle
and render-lock rules. Check current source before applying either guide.
