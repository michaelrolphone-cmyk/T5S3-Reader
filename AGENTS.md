# Agent instructions

## Architecture and specification authority

Repository: michaelrolphone-cmyk/T5S3-Reader. Platform name: **RiscRTE (RISC Runtime Environment)**.

Before making architectural changes, adding or extending platform APIs, applications, services, drivers/providers, streams, devices, jobs, packages, or hardware access, **read [docs/RISCRTE_PLATFORM_SPEC.md](docs/RISCRTE_PLATFORM_SPEC.md) first**. It is the canonical specification entry point and defines document precedence. Then read the applicable section of [docs/PLATFORM_CAPABILITY_ROADMAP.md](docs/PLATFORM_CAPABILITY_ROADMAP.md) and the child specification(s) linked by the master spec.

New functionality must follow the RiscRTE target architecture even when current code still implements a legacy design. Do not extend a legacy pattern merely because it is documented. When implementation has not migrated, preserve accurate current-state documentation under explicit current/legacy headings and prepend/update the canonical target specification. Architecture/API changes must update the authoritative specification in the same change.

Use **RiscRTE** for the overall firmware, runtime, build/release system, artifacts, versioning, platform terminology, and new platform facilities. **CrossPoint** is reserved for the ebook-reader subsystem/capabilities and for legacy implementation identifiers that have not yet been migrated (for example `CrossPointSettings`, `CrossPointState`, `.crosspoint/`, and temporary `CROSSPOINT_*` compatibility defines). Do not use CrossPoint as a name for the overall firmware or platform. `T5S3`, `T5 ePaper S3`, `T5S3 Pro`, and `EPD47` are hardware identifiers. Existing `T5*`, `native_*`, and other historical ABI/source identifiers may be referenced when required to describe current implementation or compatibility; do not propagate them as names for new platform facilities.

Core rule: new reusable functionality normally belongs in a capability, provider/service, stream, device, job, intent/content handler, package, execution-context facility, or core runtime primitive rather than private application infrastructure. Applications should request semantic capabilities instead of binding directly to concrete hardware implementations where the specification defines such a capability.

## Pull request isolation — mandatory

**NEVER stack pull requests. Every new PR must target `master` directly.** Never set another feature branch or unmerged PR as a PR's base, even temporarily, and never include another open PR's unmerged commits in a feature PR. Start each new feature branch from current `master`; keep unrelated work in independent branches and independent PRs. If a feature depends on another unmerged feature, finish/merge the prerequisite first, then branch from updated `master` and create its own PR. Do not work around this by opening a PR against `master` that contains a second PR's commits. If an existing PR violates this rule, preserve unique work, remove the cross-PR dependency and correct its base and diff before proceeding. Backmerge current `master` into a feature branch when necessary to resolve divergence, but never backmerge an unmerged feature branch.

## Tagged firmware releases

Repository: michaelrolphone-cmyk/T5S3-Reader. Release branch: master.

When the user asks to publish a firmware release, use the existing release-request mechanism. A workflow-dispatch tool is not required. Read docs/RELEASING.md and .github/workflows/release.yml for the current implementation before publishing.

1. Inspect current master, tags/releases, and CI.
2. Complete requested firmware changes and update `[riscrte] version` in platformio.ini.
3. Commit .github/release-request.json on master with enabled=true, matching v-prefixed tag, and commit_firmware=true unless requested otherwise.
4. GitHub Actions builds gh_release, RiscRTE ELF apps/manifests, merged firmware when requested, tags, and publishes assets.
5. Monitor the workflow and verify the published release/assets before reporting success.

Existing tags are rejected. Keep requested version and platformio.ini synchronized. Avoid advancing master during a release with commit_firmware=true. Unrelated source commits do not trigger publishing merely because the request remains enabled.

`firmware-t5s3-pro.bin` is retained as an OTA/SD compatibility filename. Canonical versioned release assets use the `riscrte_...` prefix; versioned `-app.bin` files are OTA/SD app images, while the merged versioned `.bin` is a USB image at 0x0 and must not be used for OTA/SD updates. Hardware identifiers such as `t5s3` are not platform names.

## Adding or extending apps

For **every** first-class RiscRTE application or application-facing platform API change, read:

1. [docs/RISCRTE_PLATFORM_SPEC.md](docs/RISCRTE_PLATFORM_SPEC.md)
2. [docs/PLATFORM_CAPABILITY_ROADMAP.md](docs/PLATFORM_CAPABILITY_ROADMAP.md)
3. [docs/APPLICATION_EXECUTION_CONTEXT_ARCHITECTURE.md](docs/APPLICATION_EXECUTION_CONTEXT_ARCHITECTURE.md)
4. [docs/NATIVE_APPS.md](docs/NATIVE_APPS.md) for the current compatibility ABI/implementation.

### High-priority new-app requirements

**Trusted RiscRTE system UI mediation and package-private application storage are high priority and should be implemented ahead of new app-private substitutes.**

For new apps:

- do not add a new unrestricted file/resource browser, credential picker, device picker, Wi-Fi/network picker, permission prompt, or similar security-sensitive selection flow when RiscRTE can mediate it;
- request the operation through a RiscRTE-owned picker/intent/platform facility and return only the scoped handle/result the user selected;
- store app-private persistent state in a package-private logical namespace rather than introducing new arbitrary shared `/sd` paths;
- treat broad current `/sd` enumeration/storage APIs as compatibility behavior to migrate, not the preferred new design;
- if the required picker/private-storage primitive is missing, implement the smallest reusable platform primitive first when feasible;
- attach newly acquired resources to the app execution context and prefer opaque generation-safe handles over exposed implementation pointers;
- make memory/resources attributable and quota-capable where the touched subsystem permits it;
- extend build tooling/manifests so capability/resource requirements can be validated or derived from SDK/API use where practical.

Native ELF apps under `Apps/` are the current implementation of the preferred extension path. Each shipped app source has a sibling JSON manifest; use `scripts/build_all_apps.py` for release-equivalent validation. Check exact current headers and use append-only `struct_size` checks for members an app requires.

Use [docs/ADDING_APPS.md](docs/ADDING_APPS.md) only when a feature genuinely needs to be compiled into firmware as a C++ `Activity` or requires firmware internals not yet exposed through a RiscRTE platform API. That guide is the legacy/in-firmware path, not precedent for new app architecture.