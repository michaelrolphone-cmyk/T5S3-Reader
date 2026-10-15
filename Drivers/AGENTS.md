# Driver and provider engineering reference

Related references:

- [RiscRTE Platform Specification](../docs/RISCRTE_PLATFORM_SPEC.md)
- [Package Identity and Version Policy](../docs/PACKAGE_IDENTITY_VERSION_POLICY.md)
- [Driver Platform Reuse](../docs/DRIVER_PLATFORM_REUSE_ACCEPTANCE.md)
- [USB Host Startup and Detection](../docs/USB_HOST_STARTUP_AND_DETECTION.md)
- [Bounded Cooperative Operations](../docs/COOPERATIVE_BOUNDED_OPERATIONS.md)

## Driver manifests

Driver and provider packages carry:

- package ID;
- numeric package version;
- driver ABI or architecture metadata;
- required capabilities and API versions;
- provided capabilities and API versions;
- build and package metadata.

Package metadata, catalog entries, release artifacts, and installed metadata use the same package identity and version.

## Package versions and IDs

The manifest `version` is the package product version.

Firmware version, capability API version, driver ABI, source directory names, release tags, and ELF hashes are separate metadata.

Package IDs identify installable component lineages. Version changes represent successive revisions of the same package ID.

## Capability interfaces

Drivers and providers expose versioned capability structs through the project SDK interfaces.

Consumers inspect API version and struct size when reading interface members.

Dependencies are declared by capability name and API version in package metadata.

## Board configuration

Board manifests contain chip identifiers, pins, buses, addresses, and setup data.

Reusable chip drivers consume board-provided configuration through the project interfaces.

## Bus interfaces

The project uses shared transport interfaces for I²C, SPI, UART, USB, and related buses.

Peripheral drivers use the established bus APIs and SDK headers for transport operations.

USB CDC `serial.port` and physical UART interfaces are represented separately.

## Lifecycle interfaces

Driver/provider modules expose the lifecycle entry points defined by the runtime ABI, including initialization, start, quiesce, and stop behavior where present.

Provider state and dependency references are represented through the runtime’s module and capability structures.

## Build and package data

Driver builders produce ELF binaries and package metadata.

Useful package checks include:

- manifest and package version agreement;
- ABI and architecture fields;
- imports and exports;
- declared dependencies and provided capabilities;
- catalog metadata;
- artifact hashes and sizes;
- host fixtures and target builds.
