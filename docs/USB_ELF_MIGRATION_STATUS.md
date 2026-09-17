# USB ELF migration: implementation status and acceptance

**Authority:** [RiscRTE master specification](RISCRTE_PLATFORM_SPEC.md), [hardware-agnostic driver boundary](HARDWARE_AGNOSTIC_DRIVER_BOUNDARY.md), [runtime driver architecture](RUNTIME_DRIVER_ARCHITECTURE.md), and [USB architecture](USB_OTG_HOST_ARCHITECTURE.md). Implementation snapshot, not relaxed acceptance criteria. **Shipping firmware still owns the legacy USB path. Do not install experimental ELFs on a board.**

## Independently implemented experimental provider stack

```text
CDC ACM / CP210x ELFs ------ serial.port@1 --> USB host ELF
USB host ELF ------------- usb.controller@1 -> ESP32-S3 controller ELF
ESP32-S3 controller ELF -- board.power.vbus@1 -> T5S3 BQ25896 power ELF
T5S3 power ELF ---------- i2c.bus@1 -------> independent I2C bus ELF (NOT IMPLEMENTED)
              `---------- platform.clock@1 -> generic clock port (NOT INTEGRATED)
```

`Drivers/usb_cdc_v2` implements CDC descriptor matching including IAD/union and alternate settings, control/data claims, line coding, DTR/RTS, bulk I/O and teardown. Multiple ambiguous CDC function selection remains unavailable through its current `open(device)` API. `Drivers/usb_cp210x_v2` implements vendor UART enable/disable, baud, line format, modem controls and bulk I/O. VID-only matching is experimental and needs validated PID/profile restrictions. Both class ELFs independently offer `serial.port@1` and require `usb.host@1`.

`Drivers/usb_host_v2` is a real ELF consuming the separate `usb.controller@1` provider. It implements provider-owned discovery, generation-qualified device and claim tokens, exclusive interfaces, alternate/endpoint descriptor validation, direction checks, stale detach rejection and fail-closed release/quiescence. Its `poll()`/`devices()` extension has not yet been wired into generic registry publication or a serialized executor.

`Drivers/usb_controller_esp32s3/driver.cpp` contains **real ESP32-S3 USB host implementation**, not a compiled firmware USB forwarding proxy: actual ESP-IDF client/device/claim/transfer/DMA operations. `scripts/probe_usb_controller_esp32s3.py` builds pinned ESP-IDF v4.4.7 USB/PHY/HAL/SoC sources as PIC and links them inside the physical-controller ELF. SoC MMIO addresses and GPIO mux data are resolved within the ELF; `phy_gpio.c` implements the two internal USB pads' drive strengths. It consumes independently provided `board.power.vbus@1`. It is NOT installable or hardware validated.

**New actual power-driver implementation:** `Drivers/board_power_t5s3_v2/driver.c` is a genuine BQ25896 charger/boost ELF, not a proxy for `BoardT5S3`, Wire, or resident USB. It consumes `i2c.bus@1` for an exclusive claim on address 0x6B and `platform.clock@1` for monotonic settling and bounded timeouts. `sdk/driver/RiscI2cBusV1.h` and `RiscPlatformClockV1.h` define those hardware-neutral/independent contracts. The driver checks external VBUS, other OTG users, source-current request (maximum 500 mA), snapshots REG02/03/0A, configures 500 mA boost and continuous voltage ADC, verifies actual OTG status/ADC voltage of at least 4.4 V/no boost fault, and restores the original charger, boost and ADC state on release. I2C write failure is treated as potentially applied; source-off and restoration require hardware readback, and an uncertain rail or lower-I2C claim prevents unloading. Quiescence releases the lower device claim before the ELF can unmap. **This lower I2C bus provider and clock port are still missing, and the compiled legacy firmware has not relinquished charger/OTG hardware. Therefore the power ELF is not an installable or electrically validated stack.**

The [TI BQ25896 support report](https://e2e.ti.com/support/power-management-group/power-management/f/power-management-forum/749326/bq25896-detect-usb-source-with-otg-enabled) explains that REG11 `VBUS_GD` may be zero during *successful OTG sourcing*. Accordingly the new code checks REG0B OTG status and REG11 measured voltage (with ADC explicitly enabled), not the input-only VBUS_GD bit, and restores the previous ADC enable state. Battery/thermal, actual board wiring and backfeed acceptance need real-board verification.

Generic `RiscProviderV2.h`, `ProviderModuleV2` and `ProviderGraphV2` implement verified-root identities, versioned dependencies, competing provider choice, pins, quiescence and failed-unload quarantine without USB-specific branches. They still lack production installation/signature integration, execution-context rights, generic registry publication, serialized executor and streams.

## What CI actually verifies

The CDC, CP210x and host unit tests exercise control/bulk, descriptors, detach generations and dependency lifetimes. `test/run_usb_three_elf_stack_v2_test.sh` loads REAL host and CDC ELFs plus a strictly TEST-ONLY simulated controller. It does not establish actual ESP32 behavior. The hardware controller PIC link and loader audits passed [experimental run 35247590112](https://github.com/michaelrolphone-cmyk/T5S3-Reader/actions/runs/35247590112): 590 supported relocations, zero unsupported/text relocations or forbidden resident USB imports, but **36 imported OS/CPU/interrupt/allocator/log/ROM GPIO/assert symbols are absent from the shipping firmware loader exports**. It remains `loader-abi-blocked` and not loadable. Earlier counts were 43, then 37 after SoC GPIO/MMIO, then 36 with ELF-owned USB pad drive; audit reports are retained in unpublished inspection artifacts.

`test/run_board_power_t5s3_v2_test.sh` runs the *real charger algorithm* against an instrumented I2C register mock. Tests include external-source conflict, 500 mA limit, initially disabled ADC, REG11 `VBUS_GD=0` during OTG, rollback after late NACK, charger/ADC restoration, source rise timeout, boost fault, stale lease, failed I2C release and retry. `scripts/build_board_power_t5s3_v2.py` cross-builds its own unpublished Xtensa ELF, checks native symbols and validates ELF structure; see the **exact HEAD CI run** before claiming those checks passed. Host I2C mocks cannot validate real electrical current, backfeed or charger thermal behavior. No experimental artifact goes to the app/driver release catalog.

## Required for actual USB migration

1. Resolve the **36 physical controller loader imports** through a narrowly versioned generic OS/CPU port or appropriate ELF-owned SoC code. Prove actual loader symbol binding, task/ISR/callback lifetime, transfer cancellation, bounded drain and recovery. Never introduce a firmware USB forwarding symbol.
2. Implement an independently installable, exclusive `i2c.bus@1` ELF and generic monotonic clock provider/port. The board power provider must be the *sole* charger owner; experimental I2C claim bookkeeping does not prevent today's legacy Wire/BoardT5S3 from using the same hardware. Implement and verify safe board-wide USB role/PHY arbitration, port/charger voltage and thermal/current protection, and hub/disconnect states.
3. Wire verified manifests, dependency graph, execution-context grants, generic registry publication, provider executor, streams and install/remove into normal runtime. Implement missing CH34x and qualified device selection; do not expose experimental ELFs to the user-installable catalog.
4. Physically verify T5S3 cold boot, existing external supply conflicts, device attach/hubs/replug, unplug mid-transfer, sustained duplex, line coding, DMA/ISR teardown, powered-hub handling, rollback and absence of resident fallback. Only after real parity switch Serial Monitor and remove compiled `NativeUsbBridge`, `UsbCdcDriverRuntime`, USB-specific projections, and chipset handling; isolate recovery bootstrap.

**Status:** Draft PR #78 has independently implemented controller, BQ25896 power, host, CDC and CP210x ELF code. Missing port/I2C/runtime integration and real hardware acceptance still prevent a completed migration. Shipping USB and Serial Monitor are unchanged.
