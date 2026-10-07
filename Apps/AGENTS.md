# App engineering reference

Related references:

- [RiscRTE Platform Specification](../docs/RISCRTE_PLATFORM_SPEC.md)
- [Application Version Policy](../docs/APP_VERSION_POLICY.md)
- [Application Execution Context](../docs/APPLICATION_EXECUTION_CONTEXT_ARCHITECTURE.md)
- [Native Apps](../docs/NATIVE_APPS.md)
- [Bounded Cooperative Operations](../docs/COOPERATIVE_BOUNDED_OPERATIONS.md)

## App manifests

Distributable apps use sibling manifests such as `Apps/<name>.json`.

Common fields include:

- display name;
- file name;
- component version;
- minimum firmware version;
- runtime profile;
- required capabilities and API versions;
- optional capabilities;
- category;
- icon.

## App versions

The manifest `version` is the app product version.

Firmware version, `min_firmware_version`, artifact hashes, release tags, and catalog timestamps are separate metadata.

Changed distributable apps generally use a higher `MAJOR.MINOR.PATCH` version than the previous published app version.

`min_firmware_version` records the runtime version associated with the APIs or behavior used by the app.

## Runtime APIs

Apps use the existing RiscRTE and native-app SDK interfaces.

Capability records expose an API version and struct size. Optional capabilities have a defined unavailable state.

Display, navigation, touch, storage, alarm, radio, networking, clock, telemetry, and lifecycle functionality use the corresponding shared interfaces.

## Persistence

App data uses the project storage interfaces and the app’s established namespace or app-data location.

Stored formats and keys remain documented alongside the app source where applicable.

## Build outputs

App builders produce ELF binaries and package metadata from app source and manifests.

Version information is represented consistently in the app manifest, generated package metadata, catalogs, and installed metadata.

Host fixtures, target builds, and hardware runs provide distinct types of validation evidence.
