# USB ELF migration: implementation status and acceptance

**Authority:** [RiscRTE platform](RISCRTE_PLATFORM_SPEC.md), [hardware-agnostic boundary](HARDWARE_AGNOSTIC_DRIVER_BOUNDARY.md), [runtime driver architecture](RUNTIME_DRIVER_ARCHITECTURE.md), [USB architecture](USB_OTG_HOST_ARCHITECTURE.md) and [privileged OS/CPU ABI](PRIVILEGED_OS_CPU_ABI.md). This is an implementation snapshot, **not physical acceptance**. Shipping USB, Serial Monitor, `Wire` and charging retain their legacy paths. Experimental physical ELFs are unpublished and noninstallable.

## Current state: physical PIC ELFs build; signed admission and exclusive cutover remain

The previous 36 missing USB and 29 missing I²C OS/CPU imports are satisfied by a separate 46-symbol privileged generic OS/CPU ABI v1. Its strong references link in both firmware targets; ordinary app exports remain unchanged and the generic core does **not** implement USB/I²C operations. Hardware control, device-driver logic and teardown reside inside independently built ELFs.

The private loader validates both `.dynsym` and `.symtab`, bounded string/symbol/relocation tables and the exact ELF image. It rejects unwanted firmware/peripheral imports and unsupported REL layouts before privileged relocation. The default resolver cannot fall through to customer/IDF/global or another loaded ELF; the global **custom resolver bypass is closed** because privileged lookups directly use the private-scoped default, never the mutable process-global hook. Public `esp_elf_relocate()` now consumes a one-shot exact-module grant: nested ordinary loads on the same privileged task cannot inherit imports, and another task cannot enter or leave that module's relocation. Concurrent normal ELF loads of *different* modules remain possible. These gates were exercised by production-function host tests; they do not memory-isolate native ELFs.

The provider graph copies the expected ELF SHA-256 digest by value; the privileged module copies candidate ELF bytes into private memory, checks this exact snapshot against the expected digest and maps only the matching copy. Digest consistency protects against substitution only if the digest came from a genuine authenticated receipt, which is **not yet connected**.

The exact import gate requires a strictly sorted, unique, bounded signed import name list matching all actual undefined imports in both symbol tables. `scripts/generate_privileged_imports_v1.py` now emits a canonical ASCII `privileged-imports.v1` sidecar from **real linked** USB controller, I²C controller and clock ELFs. Experimental CI compiles the real firmware C import-policy and exact-import matcher and checks these three binaries; negative tests also exercise missing, extra ABI-permitted, duplicate, reordered and malformed declarations. These sidecars remain **unsigned diagnostic build outputs**: neither CI compatibility nor a user-provided hash/manifest is package authorization.

Separate [Unified Package Manager PR #76](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/76) implements experimental signed RISC-PKG envelopes with P-256 signature checks, signed per-entry SHA-256 hashes, signer policy and NVS security floors. Its v1 manifest already cryptographically binds resource *names and digests* and can carry the fixed `privileged-imports.v1` resource beside the ELF; the provider-specific profile must still authenticate that association, exact ABI, signer/identity/context/resource rights and copy all requisite data into a private immutable manager-owned receipt before graph admission. PRs #76 and #78 are draft and unmerged. No existing unsigned App Store or driver installer may authorize the privileged ELFs. Physical relocation/start/stop and exclusive ownership are untested.

## Independently built provider chain

```text
CDC ACM / CP210x ELFs: serial.port@1
     -> USB host ELF: usb.host@1
          -> ESP32-S3 USB controller ELF: usb.controller@1
               -> T5S3 BQ25896 power ELF: board.power.vbus@1
                    -> ESP32-S3 physical I2C controller ELF: i2c.bus@1
                    -> independent platform clock ELF: platform.clock@1
```

`Drivers/usb_cdc_v2` implements CDC IAD/union/alternate descriptor matching, interface claims, line coding, DTR/RTS, bulk I/O and stale-session rejection. Ambiguous CDC composite functions still need qualified selection. `Drivers/usb_cp210x_v2` implements chipset UART enable, baud, line/modem controls and bulk I/O; real PID/profile qualification must replace vendor-VID-only matching. Both are independent `serial.port@1` ELF implementations, not firmware hardware proxies.

`Drivers/usb_host_v2` consumes `usb.controller@1` and owns enumeration, discovery, generation-qualified device claims, interface/alternate/endpoint validation and disconnect/teardown. Its events still require routing through the serialized provider executor to the transport-neutral Unified Device Registry.

`Drivers/usb_controller_esp32s3/driver.cpp` owns actual USB OTG, IDF client/device/interface/PHY control, interrupts, control/bulk/DMA and shutdown. `scripts/probe_usb_controller_esp32s3.py` links pinned ESP-IDF v4.4.7 USB/PHY/HAL/SoC plus internal PHY GPIO implementation **inside its PIC ELF**, not resident firmware. ABI/relocation audits and exact import-set checks pass experimentally; on-board operation, ISR/DMA draining, roles and electrical safety are unverified.

`Drivers/i2c_esp32s3_v2/driver.c` physically controls I²C0 SDA39/SCL40 using IDF install/configuration, repeated-START reads, writes, bounded transactions, exclusive per-address claims, generation-safe tokens and hardware delete/quiescence. `scripts/probe_i2c_esp32s3_v2.py` links pinned IDF I²C driver, GPIO, peripheral controller, I²C HAL/ISR, GPIO HAL and SoC code **inside its PIC ELF**. Its audit rejects firmware I²C/GPIO/RTC implementation imports. It must not be activated concurrently with compiled `Wire` owning I²C0; a driver ELF's address claim cannot arbitrate against compiled owners.

`Drivers/board_power_t5s3_v2/driver.c` programs BQ25896 **through the I²C provider's exclusive 0x6B claim** and independent `platform.clock@1`. It rejects external VBUS/OTG conflicts, limits sourced current to 500 mA, snapshots REG02/03/0A, checks rail voltage and faults, and restores ADC/charger/boost state. REG11 `VBUS_GD` may be zero during valid OTG, so actual voltage is checked. Clock failure (`UINT64_MAX`), source-off/readback failure or uncertain shutdown retain the lease and prevent unsafe unload. Synthetic tests inject NACK, clock failures and restoration/teardown faults, but do not establish actual current, backfeed or thermal safety.

`Drivers/platform_clock_v1/driver.c` is an independent `platform.clock@1` ELF using libc `clock_gettime(CLOCK_MONOTONIC)`/`usleep`, bounded sleeps, startup checks and sentinel on failure, without a hardware-specific firmware proxy. Its Xtensa toolchain emitted trailing `R_XTENSA_NONE` records; `scripts/normalize_xtensa_relocations.py` excludes only trailing all-zero no-op entries and adjusts `.rela.dyn` size and `DT_RELASZ`, preserving real relocations and strict validation. The ELF audits and exact-import consistency pass, but no physical provider execution has been demonstrated.

## Generic lifecycle and test evidence

`ProviderModuleV2`/`ProviderGraphV2` implement identity/API/dependency validation, ambiguous-provider and cycle rejection, pins, grants, quiescence, and failed-start/teardown quarantine. Failed quiescence revokes published capabilities and preserves ELF mapping/dependency pins until shutdown can be demonstrated. `test/run_vbus_provider_chain_v2_test.sh` exercises the real graph with BQ25896 and clock sources plus simulated I²C, including source lease, bad/stale releases, failure recovery and retries; other source-level suites model I²C/CDC/CP210x/USB host. Host snapshot tests mock Xtensa relocation, not physical peripherals.

[Experimental CI 35270970027](https://github.com/michaelrolphone-cmyk/T5S3-Reader/actions/runs/35270970027) **passed** three physical-ELF exact-import matches and all previous build/audit tests. [CI 35271306752](https://github.com/michaelrolphone-cmyk/T5S3-Reader/actions/runs/35271306752) adds the real-ELF negative import checks; its completion must be verified separately. Firmware board CI for the latest head must also be verified separately; neither is physical acceptance.

## Remaining acceptance gates

1. In the trusted package manager, bind signed provider identity, key scope, architecture, security version, exact ABI/import resource, requirements and executable digest/length to an immutable manager-owned receipt. Enforce execution-context/resource authorization, persistent rollback and trusted-only graph registration; never interpret raw user pointers or a digest as privilege.
2. Eliminate simultaneous compiled owners before activation: USB PHY/OTG, charger/VBUS and I²C0 versus `Wire`, using generic exclusivity rather than special USB/I²C branches in the core. Keep a separate recovery facility, not hidden production fallback.
3. Actually relocate, start, exercise and quiesce both physical ELFs on a real T5S3; connect the generic registry, events/streams, install/remove and chipset qualification including CH34x.
4. Verify cold boot, hubs/replug/unplug mid-transfer, sustained CDC/CP210x duplex, charger/backfeed/current/rail, IRQ/task/DMA draining, failure/recovery and **absence of compiled framework driver fallback**.

**Status:** ABI, resolver isolation, one-shot relocation, digest snapshot, physical PIC ELF builds and exact import matching are implemented experimentally. Signed admission, physical ownership/execution and safe cutover remain outstanding. PR #78 stays draft and drivers stay unpublished.
