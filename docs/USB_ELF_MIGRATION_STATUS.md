# USB/I²C ELF migration — implementation status and acceptance

**Authority and scope:** [RiscRTE platform](RISCRTE_PLATFORM_SPEC.md), [hardware-agnostic boundary](HARDWARE_AGNOSTIC_DRIVER_BOUNDARY.md), [driver-loader scope correction](DRIVER_LOADER_SCOPE_CORRECTION.md), [provider graph/lifetime contract](PROVIDER_GRAPH_ADMISSION_LIFETIME.md) and [privileged OS/CPU ABI](PRIVILEGED_OS_CPU_ABI.md). This is a current-state snapshot, NOT physical acceptance. **Mandatory signed package admission was scope creep and is no longer an acceptance gate.** PR #76's existing P-256/signer/floor work is an out-of-scope experiment; actual signing-dependent source paths have not automatically been removed by these docs. Shipping USB, Serial Monitor, `Wire` and charging still use legacy implementations. Physical ELFs remain unpublished and noninstallable pending real cutover.

## Implemented: PIC drivers, generic ABI and private loader validation

Previously missing physical USB and I²C OS/CPU imports are provided by a separate 46-symbol privileged *generic* OS/CPU ABI. Both firmware targets link those symbols; ordinary application exports remain unchanged. USB/PHY and I²C hardware implementation, control and teardown are linked inside ELFs, not core drivers.

The private loader validates `.dynsym` and `.symtab`, bounded strings/symbols/relocations and executable layout, rejecting unwanted firmware/peripheral imports and unsupported REL layouts before privileged relocation. Privileged lookup cannot fall through to process-global custom resolvers or other loaded modules. A one-shot exact-module relocation grant prevents nested ordinary/same-task loads or another task from inheriting imports; concurrent ordinary loads of other modules remain possible. These were tested through compiled production resolver/relocation code, not isolated operating-system processes.

The graph copies the expected digest by value, and `OwnedNodeV2` copies names, dependencies, import tables and full ELF bytes. The private loader takes a second copy, verifies digest/imports on precisely that image, and relocates it. The digest is a byte-consistency/integrity check, NOT publisher authentication and NOT permission to load privileged imports. The loader must enforce its own permitted symbol policy and execution-context/resource grants, taking privately owned checked metadata and bytes from the common package manager without demanding signatures.

`scripts/generate_privileged_imports_v1.py` and `scripts/generate_provider_package_inputs_v1.py` derive bounded, canonical `privileged-imports.v1`/`provider-abi.v1` diagnostic declarations from actual linked driver ELFs and manifests. USB, I²C and clock build/ELF audits check exact undefined-import set equality against both symbol tables. Fifteen malformed/missing/extra/duplicate/reordered import/manifests cases are rejected. Generated inputs are ordinary build metadata and not themselves authority or required signed package resources.

The private provider graph supports IDs/API/dependency checking, cycles and ambiguity rejection, pins, grants, quiescence and teardown quarantine. Caller-owned name/dependency/import/ELF buffers are copied; the dependency interface array passed to `start()` persists through `quiesce()` and `stop()`. Failed quiescence preserves mapping/dependency pins, rather than unloading potentially live hardware. Existing `GraphV2::addVerified()` rejects public privileged specs; the private graph entry still needs a manager-owned executor with independent runtime policy. A C++ private method is not a sandbox.

## Provider chain

```text
CDC ACM / CP210x ELFs: serial.port@1
     -> USB host ELF: usb.host@1
          -> ESP32-S3 USB controller ELF: usb.controller@1
               -> T5S3 BQ25896 VBUS ELF: board.power.vbus@1
                    -> ESP32-S3 physical I2C ELF: i2c.bus@1
                    -> platform clock ELF: platform.clock@1
```

- `Drivers/usb_cdc_v2`: descriptor/IAD/union/alternate matching, interface claims, line coding, DTR/RTS, bulk I/O and stale-session refusal. Ambiguous composite devices still require qualified selection.
- `Drivers/usb_cp210x_v2`: chipset UART enable, baud/line/modem controls and bulk I/O. PID/profile qualification must replace vendor-VID-only matching; CH34x is not yet qualified.
- `Drivers/usb_host_v2`: consumes `usb.controller@1`, owns enumeration, discovery, generation-qualified claims, interface/alternate/endpoint validation and disconnect/teardown. Events still need routing through the provider executor to the generic registry.
- `Drivers/usb_controller_esp32s3/driver.cpp`: actual USB OTG, IDF host/device/interface/PHY, ISR, control/bulk/DMA and shutdown. `scripts/probe_usb_controller_esp32s3.py` PIC-links pinned ESP-IDF v4.4.7 USB/PHY/HAL/SoC plus internal PHY GPIO inside this ELF. Audit passes; physical USB, electrical/role safety and ISR/DMA draining remain untested.
- `Drivers/i2c_esp32s3_v2/driver.c`: I²C0 SDA39/SCL40, configuration, repeated-START read/write, bounded transactions, per-address exclusive claims and hardware cleanup. Its probe links IDF I²C/GPIO/HAL/ISR/SoC into the PIC ELF and rejects firmware I²C/GPIO/RTC implementation imports. It cannot safely coexist with compiled `Wire` ownership of I²C0.
- `Drivers/board_power_t5s3_v2/driver.c`: accesses BQ25896 through I²C provider exclusive address 0x6B, clock dependency, conflict checks, nominal 500 mA source limit, voltage/fault checks and state restoration. Uncertain shutdown retains leases and prevents unsafe unload. Synthetic NACK/clock/fault tests do not establish physical current, backfeed or thermal safety.
- `Drivers/platform_clock_v1/driver.c`: independent `platform.clock@1` via monotonic clock and bounded sleep, failure sentinel and strict relocation audits. `scripts/normalize_xtensa_relocations.py` removes only trailing zero no-op `R_XTENSA_NONE` records, preserving real relocation validation.

## Existing test evidence and limits

Host graph/module and source-level driver tests cover metadata copy, retained dependency tables, pin/grant lifecycle, UART/USB/I²C and charger simulation, failed startup, quiescence quarantine and malformed ELF/import declarations. Earlier experimental physical-ELF audit CI passed; current exact-head workflow status must be checked separately. No GitHub Actions run can substitute for real USB/I²C/charger on-device measurement. Current signed-profile P-256 verification in PR #76 was implemented experimentally, but does not establish a required integration contract here.

## Required remaining gates — corrected scope

1. **Generic unsigned-package admission:** integrate one source-independent package manager with a firmware-private provider executor. Validate and privately copy bounded package identity, dependencies, architecture/ABI, import declaration and exact candidate ELF bytes. Enforce privileged import/relocation policy, execution-context grants and provider lifetimes independently. Do not require P-256 signatures, signer receipts, signed provenance or cryptographic NVS floors. A digest by itself must not authorize a privileged operation.
2. **Exclusive physical ownership:** remove concurrent compiled USB PHY/OTG/VBUS, charger and I²C0/`Wire` owners before provider activation. Preserve boot/recovery access separately, never as hidden normal-operation firmware fallback.
3. **Actual hardware activation:** relocate, start, exercise, quiesce and stop USB and I²C ELFs on a real T5S3, with registry/stream/lease integration, install/remove and required chipset support.
4. **Physical acceptance:** cold boot, hub/hotplug/rapid replug, sustained CDC/CP210x duplex, power/current/backfeed/rail checks, real I²C arbitration, task/IRQ/DMA/callback draining, failure cleanup and absent compiled framework fallback; include both supported boards where applicable.

**Status:** PIC ELF linkage, generic ABI and strict import/relocation auditing are implemented experimentally. Source-independent unsigned admission, physical ownership/cutover, real-device execution and release qualification remain outstanding. Keep PR #78 draft and experimental ELFs unpublished until those functional and safety gates pass; do not block them on the unrelated signing program.
