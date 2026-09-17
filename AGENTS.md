# Agent instructions

## Architecture and specification authority

Repository: michaelrolphone-cmyk/T5S3-Reader. Platform name: **RiscRTE (RISC Runtime Environment)**.

Before making architectural changes, adding or extending platform APIs, applications, services, drivers/providers, streams, devices, jobs, packages, or hardware access, **read [docs/RISCRTE_PLATFORM_SPEC.md](docs/RISCRTE_PLATFORM_SPEC.md) first**. It is the canonical specification entry point and defines document precedence. Then read the applicable section of [docs/PLATFORM_CAPABILITY_ROADMAP.md](docs/PLATFORM_CAPABILITY_ROADMAP.md) and the child specification(s) linked by the master spec.

**Mandatory for all hardware work:** read [docs/HARDWARE_AGNOSTIC_DRIVER_BOUNDARY.md](docs/HARDWARE_AGNOSTIC_DRIVER_BOUNDARY.md) before touching firmware, driver, registry, hardware/transport, device or capability source/specs. The RiscRTE core MUST treat `usb`, `serial.port` and every other capability identifier as opaque data. It must not contain USB-specific or other device-specific discovery, host/transfer/endpoint state, protocol code, power sequencing, provider selection or transport-specific ownership. An installable driver ELF (possibly depending on other provider ELFs) MUST implement the actual hardware behavior. A package consisting of descriptor matching, packet encoding or thin calls into an embedded firmware driver is NOT a completed hardware driver. The generic runtime manages capability identity/resolution, contextual authorization, provider execution and lifecycle; the providing ELF owns the hardware. Do not describe firmware-owned hardware operation as the target design even if existing USB/GNSS code does it today. Keep boot-critical port/recovery primitives isolated and explicitly labeled; never silently fall back to them for normal installed-driver operation.

New functionality must follow the RiscRTE target architecture even when current code still implements a legacy design. Do not extend a legacy pattern merely because it is documented. When implementation has not migrated, preserve accurate current-state documentation under explicit current/legacy headings and prepend/update the canonical target specification. Architecture/API changes must update the authoritative specification in the same change.

Use **RiscRTE** for the overall firmware, runtime, build/release system, artifacts, versioning, platform terminology, and new platform facilities. **CrossPoint** is reserved for the ebook-reader subsystem/capabilities and for legacy implementation identifiers that have not yet been migrated (for example `CrossPointSettings`, `CrossPointState`, `.crosspoint/`, and temporary `CROSSPOINT_*` compatibility defines). Do not use CrossPoint as a name for the overall firmware or platform. `T5S3`, `T5 ePaper S3`, `T5S3 Pro`, and `EPD47` are hardware identifiers. Existing `T5*`, `native_*`, and other historical ABI/source identifiers may be referenced when required to describe current implementation or compatibility; do not propagate them as names for new platform facilities.

Core rule: new reusable functionality normally belongs in a capability, provider/service, stream, device, job, intent/content handler, package, execution-context facility, or core runtime primitive rather than private application infrastructure. Applications should request semantic capabilities instead of binding directly to concrete hardware implementations where the specification defines such a capability. **The capability manager is generic; no hardware-specific manager belongs in core.**

## Pull request workflow — mandatory

**NEVER stack pull requests. Every PR targets `master` directly. If work depends on an existing open PR, add the dependent changes to that existing PR and its branch; do not create a second PR, branch a second PR from it, or open a PR against `master` that duplicates its unmerged commits.** Keep extending the existing PR until its interdependent body of work is complete and merged. Create separate PRs only for genuinely independent work branched from `master`. If an unnecessary dependent PR has already been opened, preserve its unique changes in the original prerequisite PR, verify that the work is present, and close the redundant PR. Never discard changes merely to make PRs independent. Backmerge current `master` into the active PR branch when necessary to resolve divergence; do not merge one unmerged PR branch into a separate PR.

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