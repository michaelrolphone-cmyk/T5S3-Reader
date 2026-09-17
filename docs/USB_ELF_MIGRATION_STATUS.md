# USB ELF migration: implementation status and acceptance

**Authority:** [RiscRTE master specification](RISCRTE_PLATFORM_SPEC.md), [hardware-agnostic driver boundary](HARDWARE_AGNOSTIC_DRIVER_BOUNDARY.md), [runtime driver architecture](RUNTIME_DRIVER_ARCHITECTURE.md), and [USB architecture](USB_OTG_HOST_ARCHITECTURE.md). This is an implementation snapshot, NOT a waiver of hardware acceptance. **Shipping firmware still operates legacy USB and Wire; none of the experimental ELFs is installable or published.**

## Independently implemented provider chain

```text
CDC ACM / CP210x ELFs -> serial.port@1 -> USB host ELF
USB host ELF -> requires usb.controller@1 -> ESP32-S3 USB controller ELF
USB controller -> requires board.power.vbus@1 -> T5S3 BQ25896 power ELF
Board power -> requires i2c.bus@1 -> ESP32-S3 physical I2C controller ELF
           `-> requires platform.clock@1 -> generic clock port (NOT INTEGRATED)
```

The compiled RiscRTE core has not acquired USB or I2C forwarding functions. All device-specific behaviors shown here are in ELFs, but the experimental graph is not integrated with production signed package verification, authorization or the running application path.

`Drivers/usb_cdc_v2` implements CDC IAD/union/alternate descriptor matching, claims, line coding, DTR/RTS, bulk I/O, and stale-session rejection; its open API does not support qualified selection of ambiguous CDC composite functions. `Drivers/usb_cp210x_v2` implements chipset UART enable, baud, line/modem controls and bulk paths; vendor VID-only matching needs real PID/profile qualification. Both are independent `serial.port@1` ELFs consuming `usb.host@1`.

`Drivers/usb_host_v2` is a separate provider consuming `usb.controller@1`, owning discovery, generation-qualified device/claim handles, exclusive interface/alternate/endpoint verification and fail-closed disconnect/teardown. Its discovery observations are not yet forwarded through a serialized provider executor into the transport-neutral Unified Device Registry.

`Drivers/usb_controller_esp32s3/driver.cpp` is a **physical USB/OTG driver**, performing actual IDF client, device, interface, PHY, interrupt, control/bulk/DMA and teardown operations. `scripts/probe_usb_controller_esp32s3.py` compiles pinned ESP-IDF v4.4.7 USB/PHY/HAL/SoC code as PIC and links it INSIDE the controller ELF. SoC MMIO, GPIO mux and dedicated internal PHY pad drive are provider-owned, not firmware USB proxies. This ELF still fails actual loader import compatibility and awaits physical DMA/role/teardown verification.

`Drivers/board_power_t5s3_v2/driver.c` actually programs BQ25896 charger/boost registers through an exclusive I2C 7-bit address claim (0x6B) and a generic `platform.clock@1` dependency. Its preflight rejects external VBUS and OTG conflicts, bounds the requested current at 500 mA, snapshots REG02/03/0A, enables voltage ADC, checks OTG status/rail voltage/faults, restores original ADC/charger/boost configuration and retains its lease if source-off or readback cannot be verified. BQ25896 REG11 `VBUS_GD` can read zero during successful OTG; the implementation uses measured voltage instead. Host fault tests cannot verify board thermal, actual rail sourcing or reverse-current protection.

**`Drivers/i2c_esp32s3_v2/driver.c` now implements actual physical I2C0, not a proxy to compiled `Wire`:** ESP32-S3 pins SDA39/SCL40, IDF install/configure, atomic repeated-START reads, independent write/read, bounded timeouts/data, exclusive claims per 7-bit address, monotonic never-reused claim tokens and hardware-backed `i2c_driver_delete` at quiescence. `scripts/probe_i2c_esp32s3_v2.py` compiles IDF I2C driver, GPIO, peripheral controller, I2C HAL, ISR-only `i2c_hal_iram.c`, GPIO HAL and pinned SoC data as PIC within this ELF. Per-function/data section elimination discards unneeded RTC GPIO code and other peripheral paths; a pinned MMIO map and import audit fail on ANY remaining resident I2C/GPIO/RTC driver implementation. This provider is NOT allowed to start while resident firmware Wire owns the same I2C0 registers. Its internal claims prevent only two of its own clients from colliding; hardware arbitration with legacy firmware remains separate work.

Generic `ProviderModuleV2`/`ProviderGraphV2` handle identity/version/dependency verification against caller-prevalidated specs, competing provider resolution, cycle detection, pins, quiescence and fail-closed unloading. Production trusted manifests, invocation rights/consent, provider executor, registry, streams and package lifecycle are NOT integrated.

## Verified implementation tests versus hardware acceptance

Host tests exercise the real CDC/CP210x and host algorithms through **test-only simulated hardware**, plus generic provider graph and dependency lifetimes. Power tests fault-inject BQ25896 late NACK, source rise failure, current/power conflicts, restore/readback errors and teardown retries. I2C tests execute real provider source with instrumented IDF calls and check repeated START, bounds, exclusive address claims, stale-generation token rejection, transaction errors and failed hardware delete retry. None proves physical current/voltage correctness.

[Experimental CI 35251829340](https://github.com/michaelrolphone-cmyk/T5S3-Reader/actions/runs/35251829340) **passed all host tests, both PIC physical-controller builds and exact ELF structural checks, and all independent Xtensa class/host/power builds**. USB controller: 590 supported relocations, zero unsupported/text relocations, no unauthorized USB or firmware imports, but **36 OS/CPU/RTOS/interrupt/memory/log/ROM symbols absent from the actual loader export set**. I2C controller: **216 supported relocations, zero unsupported/text relocations or unexpected exports, NO hardware peripheral imports, but 29 missing OS/CPU imports** after removing extraneous GPIO/RTC paths (the earlier build had 46). Both remain `loader-abi-blocked` and non-installable. Exact JSON reports are retained in inspection-only CI artifacts, NOT in public releases or the app/driver catalog.

## Required before production migration

1. Build a narrow, versioned, verified-provider-only **generic OS/CPU ABI** and clock service; resolve 36 USB plus 29 I2C unavailable symbols through authorized loader scopes, not unrestricted RTOS/interrupt symbol exports to all ordinary apps. Prove actual dynamic symbol binding, ISR/task/DMA callback lifetimes, bounded drain and recovery. Do not put USB or I2C implementations back in compiled firmware.
2. Prove physical ownership: exclusive I2C0 vs existing `Wire`, charger vs existing board logic, OTG/PHY vs existing firmware debug/host usage. Implement and verify a generic resource grant plus actual legacy-to-provider cutover; an ELF's own claim bookkeeping cannot enforce exclusivity with resident hardware drivers. Verify safe board power, VBUS/backfeed/thermal constraints, cancellation and hardware teardown.
3. Integrate signed verified manifests, execution-context grants, serialized provider executor, generic device publication/observation, bounded streams, provider install/remove, qualified chipset selection and remaining CH34x. Keep experimental artifacts out of the install catalog.
4. On actual T5S3, verify cold boot, attach/hubs/multiple devices/replug, unplug during transfers, CDC/CP210x line settings and sustained duplex, charger boost and role safety, DMA/interrupt/callback teardown and no hidden firmware fallback. Only AFTER parity switch Serial Monitor and remove normal-runtime `NativeUsbBridge`, `UsbCdcDriverRuntime`, chipset code and USB-specific device projections; recovery bootstrap is separately isolated.

**Status:** Draft PR #78 contains physical USB and I2C, BQ25896 power, host, CDC and CP210x ELFs built in CI. Generic port/loading, actual exclusive hardware ownership and physical acceptance remain incomplete. Shipping behavior is unchanged.
