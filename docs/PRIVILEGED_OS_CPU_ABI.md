# RiscRTE Privileged OS/CPU ABI

**Authority:** [Platform specification](RISCRTE_PLATFORM_SPEC.md) and [hardware-agnostic driver ownership](HARDWARE_AGNOSTIC_DRIVER_BOUNDARY.md). The generic OS/CPU port supplies primitives, not USB/I2C/GPIO device drivers. This document distinguishes the target contract from the experimental implementation in PR #78.

## 1. Boundary and versioning

Hardware-owning provider ELFs may need interrupt registration, scheduling, synchronization, ISR-safe signaling, bounded memory allocation, logging, failure reporting, CPU pin routing and standard libc functions. The boot/port layer may expose these generic OS/CPU primitives. It MUST NOT implement a transport, bus, device protocol, enumeration, USB host, I2C controller, GPIO device manager, charger, or physical hardware fallback. Drivers implement and own that behavior inside their own ELFs. The runtime treats the capability name as opaque; it never selects an ABI based on `usb`, `i2c` or any other capability name.

An OS/CPU ABI is distinct from the public application capability ABI. It is **privileged**: an ordinary application, an untrusted manifest, and an undeclared provider cannot obtain its imports merely because the loader knows their names. Each driver manifest declares its required OS/CPU ABI major/minor and exact imports. Version changes are explicit and additive when compatible. Imported symbols must be satisfied by the compiled port and bound to a specific verified module. Never enable the global Espressif customer/IDF symbol tables or add this ABI to `NativeAppLauncher.c`'s app `host_symbols`.

ABI v1's exact transitional import names are maintained in `lib/elf_loader/include/private/privileged_os_cpu_symbols_v1.def`. This list comprises generic IDF/FreeRTOS OS/CPU helpers needed by physical USB and I2C implementations. Its `esp_rom_gpio_*` entries are low-level CPU pad/matrix routing primitives, NOT firmware-owned pin/bus drivers. This compatibility name list is temporary: new drivers SHOULD consume a versioned structured kernel-port interface instead of embedding uncontrolled ESP-IDF symbol imports. No peripheral implementation imports (including `usb_*`, `i2c_*`, `gpio_*`, `t5_*`) are permitted in a physical provider.

## 2. Admission and relocation

A trusted installer/provider loader SHALL authenticate the package identity and signature, verify the declared ABI and imported symbols, enforce policy and exclusive resource grants, then hand the **same verified bytes** to `esp_elf_relocate_privileged_verified_v1`. A file path or manifest assertion alone is not authority. Imports outside the declared version/allowlist SHALL fail closed before execution. The module remains mapped only while its execution context and provider lifecycle are valid. No unverified third-party ELF may enter the privileged resolution scope.

The private relocation function structurally validates the verified buffer, acquires a non-reentrant OS/CPU resolution scope for the calling FreeRTOS task, invokes the normal Xtensa relocator synchronously, then revokes the scope on every exit. A separate thread concurrently relocating an ordinary application sees **none** of the privileged names. The scope never enters `esp_elf_register_symbol()`, alters the process-global resolver, or exposes a public `t5_*` getter. Strong port-symbol references make a missing primitive fail the firmware link instead of reporting a fictional nullable export. The scoped resolver returns zero for unknown and hardware-implementation imports.

Relocation scope is **not execution isolation**: Xtensa native ELF code shares an address space with firmware; a malicious or compromised native driver could still access known addresses. Production trust requires authenticated packages and/or stronger memory isolation if available, not just hiding symbols in the loader. IRQ callbacks, DMA memory and asynchronous code require explicit module-owned lifetime tracking; granting an ABI name does not prove these resources are safe.

## 3. Lifecycle, failure and uninstall

Provider activation requires independently verified dependency graph and authority. A failed relocation revokes the scope and frees partially mapped image memory. A failed provider start or a negative `quiesce()` MUST keep its ELF mapped, dependency pins retained, and future grants denied until quiescence can be proven. A successful quiesce proves ISR registrations removed, outstanding ISR callbacks drained, DMA stopped/returned and tasks/queues/timers/callbacks terminated; only then may the runtime call stop, release dependency pins and unmap the ELF. The runtime owns the generic lifecycle bookkeeping, not the hardware registers.

The physical USB, I2C0, PHY, charger, and legacy `Wire`/USB stacks must not concurrently own the same physical resources. Claims inside one ELF do not arbitrate against existing compiled firmware drivers. Actual exclusive hardware cutover, power/role constraints and board tests remain separate acceptance requirements.

## 4. PR #78 implementation status (September 17, 2026)

Implemented experimentally: an exact ABI-v1 inventory; task-owner-gated import lookup with no global symbol registration; a private verified-*buffer* relocation entry; strong firmware linker references; separation between ordinary app exports and privileged exports in import audit tooling; and a host scope-isolation regression test. The private entry assumes the **caller has verified** the bytes—it does not yet implement package cryptographic verification or feed provider graph activation. CI must prove the strong firmware links and audits on both targets; device-load/electrical/ISR-teardown testing remains outstanding. The physical ELF artifacts remain diagnostic, not installable, and the shipping USB/I2C paths remain unchanged.

## 5. Acceptance tests

1. Host: ordinary lookup denies every privileged symbol; same-task scope permits only exact ABI-v1 names; concurrent/wrong-task, nested, post-scope and peripheral lookups fail.
2. Firmware: both board builds link all strong ABI references. Ordinary app export audit proves no privileged symbol leakage; strict USB/I2C imported-symbol and relocation audits report zero unresolved scoped imports and zero hardware implementation imports.
3. Trusted loader: a signed manifest plus immutable bytes successfully relocates the real physical USB and I2C ELFs; unsigned, mismatched ABI, unexpected import, wrong identity, changed bytes and wrong-context loads fail before mapping or execution. Do not equate an import inventory pass with this result.
4. Runtime: use real peripherals and fault-inject start/IRQ/DMA/unplug/sleep/power/stop races; no unload while callbacks or DMA can reach module text/data. Repeated cycles leave no stale grants or resident hardware conflict.
5. Physical board: verify exclusive controller/I2C0 ownership, USB re-enumeration/duplex and safe boost/VBUS behavior without any fallback to compiled USB/I2C driver code.
