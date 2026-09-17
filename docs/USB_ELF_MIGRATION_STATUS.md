# USB ELF migration: implementation status and acceptance

**Authority:** [RiscRTE platform](RISCRTE_PLATFORM_SPEC.md), [hardware-agnostic boundary](HARDWARE_AGNOSTIC_DRIVER_BOUNDARY.md), [runtime driver architecture](RUNTIME_DRIVER_ARCHITECTURE.md), [USB architecture](USB_OTG_HOST_ARCHITECTURE.md), and the [privileged OS/CPU ABI](PRIVILEGED_OS_CPU_ABI.md). This is an implementation snapshot, **not physical acceptance**. Shipping USB, Serial Monitor, `Wire` and charging still use legacy paths. Experimental ELFs are not installable or published.

## Current state: ABI/link blocker addressed, admission and hardware cutover outstanding

The former 36 missing USB and 29 missing I²C OS/CPU imports are covered by a separate exact **46-symbol privileged generic OS/CPU ABI v1**, accessible only during a private, task-owner-gated provider relocation. Both firmware targets linked the strong OS/CPU references in [firmware CI 35261889542](https://github.com/michaelrolphone-cmyk/T5S3-Reader/actions/runs/35261889542). The ordinary application export list remains unchanged. The generic core still does **not** implement USB or I²C device operations.

The private loader now structurally validates the exact ELF image and rejects undefined imports in **both `.dynsym` and `.symtab`**, including invalid relocation-linked symbol tables, unsupported relocation format and unwanted firmware/peripheral imports. During privileged relocation the default resolver cannot fall through to customer/IDF/global symbols or already loaded ELFs; defined symbols stay in the provider image. A global custom resolver still needs explicit fail-closed handling before production admission. The strict audits and a compiled C preflight over the physical USB, I²C and clock ELFs passed [experimental CI 35264689941](https://github.com/michaelrolphone-cmyk/T5S3-Reader/actions/runs/35264689941).

The generic provider graph now carries an authenticated expected SHA-256 digest **by value**. Its privileged loader snapshots candidate ELF bytes into private memory, hashes the copy, rejects mismatches before mapping, and relocates from that same copy. Host tests exercise the real ESP branch with a mocked Xtensa relocator, tampered input, wrong digest, hash/allocation failure and PSRAM fallback. This closes the image substitution window **only when a genuine signature verifier supplied the expected digest**; it is NOT signing, context authorization or memory isolation by itself.

The separate [Unified Package Manager PR #76](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/76) implements experimental RISC-PKG P-256 signature verification, per-entry hashes and device-owned signer policy. It is draft/unmerged and not connected to PR #78. Its signed manifest still needs an OS/CPU ABI and exact import declaration/receipt; the installed package path, authorization, immutable digest handoff and anti-rollback persistence must be integrated. Neither existing unsigned app nor driver installer can currently authorize these privileged ELFs. On-target relocation/start/stop and exclusive ownership remain untested. Do not mark any of these artifacts installable on the strength of CI.

## Independently built provider chain

```text
CDC ACM / CP210x ELFs: serial.port@1
     -> USB host ELF: usb.host@1
          -> ESP32-S3 USB controller ELF: usb.controller@1
               -> T5S3 BQ25896 power ELF: board.power.vbus@1
                    -> ESP32-S3 physical I2C controller ELF: i2c.bus@1
                    -> independent platform clock ELF: platform.clock@1
```

`Drivers/usb_cdc_v2` implements CDC IAD/union/alternate descriptor matching, claims, line coding, DTR/RTS, bulk I/O and stale-session rejection. Qualified selection of ambiguous CDC composite functions remains. `Drivers/usb_cp210x_v2` implements chipset UART enable, baud, line/modem controls and bulk I/O; real PID/profile qualification is needed beyond vendor VID-only matching. Both are independent `serial.port@1` ELF implementations, not proxies.

`Drivers/usb_host_v2` consumes `usb.controller@1` and owns enumeration, discovery, generation-qualified devices/claims, interface/alternate/endpoint validation and disconnect/teardown. Its observations are not yet routed through the serialized provider executor into the transport-neutral Unified Device Registry.

`Drivers/usb_controller_esp32s3/driver.cpp` implements actual USB/OTG, IDF client/device/interface/PHY handling, interrupts, controls/bulk/DMA and shutdown. `scripts/probe_usb_controller_esp32s3.py` links pinned ESP-IDF v4.4.7 USB/PHY/HAL/SoC and internal PHY GPIO code **inside the PIC ELF**. It does not call a resident firmware USB driver. Symbol/relocation compatibility is covered experimentally; USB operation, PHY ownership, ISR/DMA draining, roles and electrical acceptance are not.

`Drivers/i2c_esp32s3_v2/driver.c` physically controls I2C0, SDA39/SCL40 with IDF install/configure, repeated-START reads, writes, bounded transactions, exclusive per-address claims, generation-safe tokens and hardware-backed delete/quiescence. `scripts/probe_i2c_esp32s3_v2.py` compiles the pinned IDF I2C driver, GPIO, peripheral controller, I2C HAL/ISR, GPIO HAL and necessary SoC code into its PIC ELF. Its import audit rejects resident I2C/GPIO/RTC implementations. However, the ELF cannot safely run while existing firmware `Wire` also owns I2C0: an ELF's address claim cannot arbitrate against compiled owners.

`Drivers/board_power_t5s3_v2/driver.c` programs BQ25896 charger/boost registers **through the I2C provider's exclusive 0x6B claim** plus independent `platform.clock@1`. It rejects external VBUS/OTG conflicts, bounds requested current to 500 mA, snapshots REG02/03/0A, checks rail voltage and faults, and restores ADC/charger/boost state after source use. REG11 `VBUS_GD` may read zero during valid OTG, so measured voltage is used. Clock failure (`UINT64_MAX` sentinel), source-off/readback errors or uncertain shutdown retain the lease and prevent unsafe unloading. Tests inject NACK, clock failure, restore and teardown errors, but do not establish board current, backfeed or thermal safety.

`Drivers/platform_clock_v1/driver.c` is an independent `platform.clock@1` ELF using the existing libc `clock_gettime(CLOCK_MONOTONIC)`/`usleep`, bounded sleeps, startup checks and a failure sentinel. It has no hardware-specific firmware proxy. Xtensa binutils 2.35 once emitted trailing `R_XTENSA_NONE` records; `scripts/normalize_xtensa_relocations.py` rejects unexpected layouts and excludes only trailing all-zero no-op slots by changing `.rela.dyn` size and `DT_RELASZ`, preserving real relocations and keeping structural validation strict. The independent ELF and audit pass, but no physical provider activation has been demonstrated.

## Generic module lifecycle and tests

`ProviderModuleV2`/`ProviderGraphV2` implement provider identity/API/dependency checks, competing-provider ambiguity rejection, cycle detection, pins, grants, quiescence and failed-start/failed-teardown quarantine. An active provider whose quiesce fails loses its published capability, refuses new grants and retains its ELF and dependency pins until shutdown can be proven. `test/run_vbus_provider_chain_v2_test.sh` tests the **real generic graph** and three separately compiled Linux objects (real BQ25896 and clock sources, test-only I2C simulation). Tests cover startup, source lease, invalid release, failure recovery, repeated retry, stale grants and state reset; they cannot establish physical board operation. I2C/CDC/CP210x/host tests execute provider sources with controlled simulated peripherals. The snapshot regression intentionally mocks only Xtensa relocation and does not execute hardware driver code.

## Remaining acceptance gates

1. Integrate the trusted PR #76 signer/manifest and per-entry digest receipt with a private authorized serialized provider executor. Declare exact ABI/imports in signed metadata, check identity/version/dependency policy and persistent anti-rollback floor, and verify context grants. The private graph's digest argument is **not** an authorization primitive.
2. Eliminate/disable all simultaneous legacy owners before physical activation: USB PHY/OTG, charger/VBUS and I2C0 versus `Wire`. Add generic exclusivity accounting without hard-coded USB/I2C core behavior. Preserve a separate recovery path, not a production fallback.
3. Actually relocate, start, exercise and quiesce both physical controller ELFs on the T5S3, then integrate generic device registry, event/stream dispatch, installation, removal, selection and remaining CH34x support. Ensure no cross-module/global symbol bypass through legacy resolver hooks.
4. Verify cold boot, attach/hubs/replug/unplug mid-transfer, sustained CDC/CP210x duplex, charger/backfeed/current/rail behavior, IRQ/tasks/DMA draining and teardown, failure/recovery and **absence of compiled firmware hardware fallback**. Only after parity cut over Serial Monitor and retire normal-runtime USB-specific bridges/projections.

**Status:** Privileged ABI/link, symbol namespace preflight and signed-digest image snapshot are implemented experimentally; signer-to-loader authorization, exclusive hardware ownership, physical execution and safe cutover remain outstanding. PR #78 stays draft and diagnostic artifacts stay unpublished.
