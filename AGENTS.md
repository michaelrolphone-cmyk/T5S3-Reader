# Agent instructions

## Architecture and specification authority

Repository: michaelrolphone-cmyk/T5S3-Reader. Platform name: **RiscRTE (RISC Runtime Environment)**.

Before making architectural changes, adding or extending platform APIs, applications, services, drivers/providers, streams, devices, jobs, packages, or hardware access, **read [docs/RISCRTE_PLATFORM_SPEC.md](docs/RISCRTE_PLATFORM_SPEC.md) first**. It is the canonical specification entry point and defines document precedence. Then read the applicable section of [docs/PLATFORM_CAPABILITY_ROADMAP.md](docs/PLATFORM_CAPABILITY_ROADMAP.md) and the child specification(s) linked by the master spec.

New functionality must follow the RiscRTE target architecture even when current code still implements a legacy design. Do not extend a legacy pattern merely because it is documented. When implementation has not migrated, preserve accurate current-state documentation under explicit current/legacy headings and prepend/update the canonical target specification. Architecture/API changes must update the authoritative specification in the same change.

Use **RiscRTE** for new platform/runtime terminology. `T5S3`, `T5 ePaper S3`, `T5S3 Pro`, and `EPD47` are hardware identifiers. Existing `T5*`, `native_*`, and other historical ABI/source identifiers may be referenced when required to describe current implementation or compatibility; do not propagate them as names for new platform facilities.

Core rule: new reusable functionality normally belongs in a capability, provider/service, stream, device, job, intent/content handler, or package rather than private application infrastructure. Applications should request semantic capabilities instead of binding directly to concrete hardware implementations where the specification defines such a capability.

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
   gh_release, builds all RiscRTE ELF apps and manifests, commits the merged
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
These `t5s3` artifact names are retained compatibility/build identifiers, not the
platform name.

## Adding or extending apps

For a first-class SD-installable RiscRTE application, read the master platform
specification first, then [docs/NATIVE_APPS.md](docs/NATIVE_APPS.md). Native ELF
apps under `Apps/` are the current implementation of the preferred extension
path when the required capability is available through the versioned ELF APIs.
Each shipped app source has a sibling JSON manifest; use
`scripts/build_all_apps.py` for release-equivalent validation.

The RiscRTE application framework currently exposes separate versioned UI/session,
storage, system-time, and system-UI service tables. It also supports installed-app
manifests, the ELF springboard, App Store catalog/install services, shared Font
Awesome rendering, settings integration, SD persistence, and firmware keyboard
handoff/resume. Check the exact current headers and use append-only `struct_size`
checks for the members an app requires.

Use [docs/ADDING_APPS.md](docs/ADDING_APPS.md) only when a feature genuinely needs
to be compiled into the firmware as a C++ `Activity` or needs firmware internals
not yet exposed through a RiscRTE host/platform API. That guide documents the
legacy/in-firmware path through ActivityManager, Home routing, rendering, child
dialogs, storage, networking, localization, and builds. Keep the Home count,
labels, icons, and activation indices synchronized; use the documented lifecycle
and render-lock rules. Check current source before applying either guide.