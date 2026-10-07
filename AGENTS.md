# RiscRTE engineering reference

Repository: `michaelrolphone-cmyk/T5S3-Reader`.

## Architecture references

- [Platform Specification](docs/RISCRTE_PLATFORM_SPEC.md)
- [Platform Capability Roadmap](docs/PLATFORM_CAPABILITY_ROADMAP.md)
- [Application Execution Context](docs/APPLICATION_EXECUTION_CONTEXT_ARCHITECTURE.md)
- [Native Apps](docs/NATIVE_APPS.md)
- [Package Identity and Version Policy](docs/PACKAGE_IDENTITY_VERSION_POLICY.md)
- [App Version Policy](docs/APP_VERSION_POLICY.md)
- [Driver Reuse](docs/DRIVER_PLATFORM_REUSE_ACCEPTANCE.md)
- [Bounded Cooperative Operations](docs/COOPERATIVE_BOUNDED_OPERATIONS.md)

## Versions and package identity

Distributable apps, drivers, services, and providers use manifest identities with numeric `MAJOR.MINOR.PATCH` versions.

Package IDs identify upgrade lineages. Component metadata appears in source manifests, generated package metadata, catalogs, installed metadata, and user-facing version displays.

Documentation-only edits leave unrelated component versions unchanged.

## Dependencies and capabilities

Dependencies are declared in manifests by capability name and API version.

Providers expose versioned ABI structs. Consumers inspect API version and struct size when reading interface fields or function pointers.

Existing SDK headers define the shared contracts used by apps, drivers, services, and providers.

## Package layout

Installable component archives use their package ID and component kind.

Common destinations include:

- `/Apps/<id>/`
- `/Drivers/<id>/`
- `/Services/<id>/`
- `/Providers/<id>/`

Package metadata, archive contents, catalog entries, and installed metadata carry matching component IDs and versions.

## Runtime interfaces

Platform features are exposed through the existing capability APIs and SDK headers.

Examples include display, input, storage, alarm, networking, radio, clock, telemetry, streams, and package services.

Runtime interface structs carry version and size information for compatibility across revisions.

## Board and driver data

Board manifests contain hardware composition, identifiers, pins, buses, and setup data.

Reusable drivers receive hardware configuration through the project’s manifest and capability interfaces.

Bus APIs represent I²C, SPI, UART, USB, and other transport facilities used by higher-level components.

## Build and validation references

Project build scripts produce firmware and component artifacts from the manifests and source tree.

Useful validation data includes:

- package and manifest version agreement;
- ABI version and struct-size checks;
- ELF imports and exports;
- dependency resolution;
- package and archive metadata;
- regression tests for changed behavior;
- host fixtures and target builds;
- hardware results recorded separately from host or synthetic results.
