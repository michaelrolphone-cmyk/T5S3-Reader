# U1 technical reference

Related references:

- [U1 milestone](NEXT_HARDWARE_TEST_MILESTONE.md)
- [Four-milestone order](FOUR_MILESTONE_STREAM_FIRST_EXECUTION_ORDER.md)
- [I²C bus ELF cutover](I2C_BOOTSTRAP_CUTOVER.md)
- [SPI/UART bus ELF cutover](SPI_UART_ELF_BOOTSTRAP_CUTOVER.md)
- [Streams audit](STREAM_PIPE_MILESTONE_ALLOCATION.md)
- [ELF load verification performance](U1_ELF_LOAD_VERIFICATION_PERFORMANCE.md)

## U1 implementation areas

U1 contains work in these technical areas:

- generic byte and record streams;
- cross-context runtime handles;
- I²C bus ELF integration;
- SPI/UART provider ABI foundations;
- USB controller, host, class, and power components;
- generic `serial.port` applications;
- non-USB stream usage;
- online and SD package intake;
- four-kind package management;
- `.rte.zip` package layout;
- ZIP bootstrap/service support;
- legacy package migration;
- CDC package identity migration;
- catalog and version metadata;
- package signing/provenance work;
- installed-ELF load and inventory performance.

## Stream interfaces

The stream system exposes byte and record endpoints through the runtime ABI.

Stream metadata includes endpoint type, context, handle identity, buffering state, and lifecycle state.

USB serial applications use the same generic stream contracts as other stream-backed applications.

## Bus providers

The I²C bus package exposes the project’s public I²C capability used by device drivers.

SPI and physical UART packages expose their corresponding bus capabilities.

The public bus ABIs remain stable across backend implementation changes.

USB CDC `serial.port` and physical UART capabilities are separate interfaces.

## USB components

USB functionality is split across controller, host, class, power, and application-facing interfaces.

The USB serial path connects USB class functionality to the generic `serial.port` interface used by applications such as Serial Monitor and programmer tools.

## Package system

The package system handles apps, drivers, services, and providers.

Package archives contain component metadata and payloads in the project package layout.

Catalog metadata identifies package ID, version, component kind, compatibility data, and source information.

## Installed ELF performance

Committed installed components carry enough metadata for efficient inventory and launch.

Install-time and explicit verification data include hashes, ABI metadata, imports, package identity, and generation information.

Runtime inventory and launch paths use the coherent installed metadata produced by installation and recovery.

## Validation data

U1 validation commonly includes:

- stream behavior;
- package parsing and extraction;
- version and identity migration;
- ELF imports and ABI metadata;
- provider lifecycle;
- USB class and serial behavior;
- package catalog consistency;
- installed-component inventory;
- target builds;
- hardware integration results.
