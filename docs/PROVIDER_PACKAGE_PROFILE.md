# Ordinary provider ABI and import metadata

Authority: [ordinary packages](RISC_PACKAGE_FORMAT.md),
[provider admission/lifetime](PROVIDER_GRAPH_ADMISSION_LIFETIME.md),
[privileged OS/CPU ABI](PRIVILEGED_OS_CPU_ABI.md) and
[hardware boundary](HARDWARE_AGNOSTIC_DRIVER_BOUNDARY.md).

A privileged ordinary driver/provider bundle retains its executable and these
declared resources in one `.rte.zip` and extracted package generation:

- `provider-abi.v1`: versioned generic ABI/capability description
- `privileged-imports.v1`: exact sorted undefined-import declaration
- `.package.json`: identity/version/CPU/runtime/dependencies and entry size/SHA

All declared bytes are checked during installation. These resources contain
self-declared consistency metadata, not execution authority or a second package
format. Historical unsupported containers remain user data and are not deleted.

## ABI resource

ASCII, exactly three LF-terminated lines, no BOM, whitespace, extra keys or CRLF:

```text
os-cpu-abi=1
provides=example.capability
api=1
```

The capability is a canonical bounded name; API is positive uint32 decimal.
The generic core treats it as an opaque capability, never a chipset selector.
`os-cpu-abi=1` names the supported kernel/CPU import contract; it does not make
hardware functions part of the core. Only explicitly permitted bus providers may
use their own transitional private controller imports.

## Import resource and admission

Generate `privileged-imports.v1` with `generate_privileged_imports_v1.py` from
both actual ELF symbol tables. Names are bounded, strictly sorted, unique and
LF-terminated. The bounded declaration may be empty for a module with no imports;
its exact length/count must agree with the ELF. The private loader compares the
actual undefined set in both tables, rejecting missing, extra, forbidden,
malformed or ABI-permitted-but-undeclared symbols before mapping.

The trusted ordinary manager passes exact private bytes and copied metadata
through `DeviceProviderExecutorV2::registerManagerValidated`. The graph's private
manager-admission route remains inaccessible to caller-authored privileged specs.
`ProviderOwnedSpecV2` copies strings, dependencies and import names; the private
loader preserves candidate-byte ownership through relocation. Content digest
values alone confer no privilege. Hardware activation and consumer grants remain
separate, and code/dependencies stay pinned through unsuccessful quiescence.

## Verification

Use the ordinary package tests, provider-manager admission and owned-spec tests,
exact import/snapshot audits, provider graph failure tests and installed-class
integration runners. They test malformed metadata/imports, content mutation,
forged privileged specs, dependency lifetime, failed-start cleanup and quarantine.
They do not establish native memory isolation or physical hardware acceptance.
