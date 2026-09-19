# U1 implementation ledger

PR: [#96](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/96), branch `impl/u1-riscrte`. This ledger records committed source and observed checks, **not** all-USB architecture compliance, released firmware or hardware qualification. Governing amendment: [USB contract remediation](USB_CONTRACT_VIOLATION_REMEDIATION.md). One U1 PR only; owner controls merge, tag, release, flash and physical qualification.

## Branch and baseline

`0a8c21c` merged then-current master (`1ebfbfe`) into this branch, preserving board-power diagnostics and firmware 1.2.33/1.2.34 artifacts. Master may advance again: recheck ancestry before next backmerge. Version 1.2.35 is reserved in this branch for the new package ABI (`7bffec3`), not published here.

## Stream / USB work already pushed

- Generic ELF byte/record endpoints, cross-context rights, grant/revoke, bounded provider I/O outside the stream mutex. `open_usb` is unsupported; semantic `serial.port` RX/TX is the intended app contract.
- Installed class ELF binding, checked teardown/retryable serial leases and package pin retention on uncertain physical close. `ClassStreamSession` and production `NativeStreamBridge` retain RX/TX chunks across backpressure and partial/zero writes. Duplicate early class-session attachment removed.
- I²C bus ELF private bounded `risc_fw_i2c_transact_v1` port is the **only authorized temporary firmware I²C importer**; replacing its native controller backend is U3, not U1. USB host/controller/class and BQ-VBUS driver sources are present, but firmware orchestration and BQ dual ownership remain U1 blockers.

## Generic package engine and app integration already pushed

- Ordinary four-kind manifest, preflight, staging, transaction and recoverable publication. Bounded stored `.rte.zip` bootstrap checks topology/CRC, then retained SHA-256, ABI/import and ordinary transaction checks; SD ZIP adapter functions without an installed ZIP service or internet.
- Generic `package-catalog.json` pins online ZIP acquisition to an immutable release; archive SHA/size checked in `/Packages/Inbox`; App Store and Package Manager use common `online_*`, SD directory and ZIP APIs. Nested managed manager app launch paths retain caller-kind permissions.
- `befe901`/`41b5e5a`: App Store migrated to shared online ABI and test. `f0fbca2`: native apps staged as ordinary application packages. Release exporter/cat tests `5f28827`, `01bedb8`, `12431f7`, `ea8dd3f`, `376b9b5` and current `release.yml` publish one `.rte.zip` per current package and one generic catalog plus firmware; historical releases/loose compatibility inputs remain untouched. Firmware build outputs are preserved outside mutable `.pio` before ELF builds.
- `30763b7`, `758c02b`, `4919541`, `62cb43b`, `d5f0a38`: Driver Manager **app** normal online/SD ZIP and host/source tests migrated, recovery UI preserved; its app version advanced `1.0.4 -> 1.0.5`. App Store version `1.0.2 -> 1.0.3`; Package Manager `1.0.0 -> 1.0.1`. Their firmware API floor advanced for 1.2.35.

## September 19 production continuation: provider discovery + legacy intake retirement

- `8ba10d5`, `dc48e72`: `scripts/build_installed_usb_stack.py` and its real ZIP verifier discover **all ABI-v2 driver manifests** instead of a seven-identity/version/count whitelist. Deterministic dependency ordering, exact SHA/ZIP/file/import/relocation/MMIO checks and baseline witness contracts remain. A new class provider is not rejected for being an eighth driver.
- `5193811`, `3e88518`: new source-level discovery regressions and automatic conventional `scripts/build_<source-dir>.py` invocation if a discovered ELF is absent. Missing/ambiguous outputs fail closed; this is a build convention, not a claim that physical provider discovery is dynamic yet.
- `41838ff`, `70a2f7e`, `5b347c5`: provider dependency ordering checks **minimum capability API**, not merely a capability name; package-count bound applies before auto-build. Synthetic tests cover additional class, alternative providers, missing/ambiguous ELF, bad version/identity, missing dependency, cycle and API-floor refusal; existing provider package CI command now runs them.
- `3217ec0`: replaced `NativeDriverManagerBridge.cpp` legacy normal online loader with recovery-only ABI implementation. Its `catalog_*`, `install` and `install_with_progress` compatibility slots fail closed. Getter is restricted to authenticated nested or known flat Driver Manager app path; legacy stage inventory/inspection/retry and explicitly confirmed discard remain. Normal live discovery/ZIP install is **only** `NativePackageManagerBridge`. The old `NativeOnlineDriverInstall.h` source file has not been independently proven unlinked/deleted, but is no longer included by this bridge.
- `9b7ca0f`: source regression now forbids legacy/latest/USB catalog and loose network intake in the active driver ABI while asserting scoped manager identity, recovery safeguards and common immutable ZIP source. It does **not** replace a link/reachability test.

## Checks: evidence and limitations

- Earlier platform PR run `35427506383` on head `7bffec3` reported success, and prior experimental USB workflow `35427439693` succeeded. They predate these latest provider-builder and bridge commits and cannot be used as checks for them.
- No local checkout/firmware build or host suite was successfully run on **`9b7ca0f`**; no new green CI result or hardware test is claimed. Current host assertions are committed, not an observed PASS. Do not wait idly on CI or demand intermediate owner hardware approval.
- Owner-reported CH34x-to-CAM serial connection is device-path evidence only, not proof of all-class firmware agnosticism, unique BQ ownership or exclusive PHY handoff.

## Outstanding implementation (all U1; source, not paperwork)

1. **Single BQ25896 owner:** `BoardT5S3.cpp` charger register init/reset and `NativeBatteryBridge` runtime calls can contend with `board_power_t5s3_v2` ELF OTG. Consolidate chip writes and required charging/telemetry under the ELF; preserve battery/boot/sleep safety. BQ27220 is a separate U3 gauge.
2. **Real provider-originated USB discovery:** `NativeUsbBridge.cpp` still hard-codes `usb-host-v2`, `usb-cdc-acm-v2`/`usb-cp210x-v2`, VID preference, host polling, session/line settings and board guard; `NativeSerialPortBridge_implementation.inc` retains USB projection/leases and fixed class path. Make host and new class ELFs publish arrival/removal, probing, semantic `serial.port` and byte endpoints via generic registry/streams with no firmware class edits or fallbacks. Preserve Serial Monitor/programmer ownership and partial I/O behavior.
3. **Exclusive debug-console/internal PHY:** trace actual linked host/boot-CDC handoff; add a narrow platform-port/controller ELF exclusive lease and safe failure quarantine. Preserve known-working electrical conflict mitigation rather than deleting it by grep.
4. **Retire remaining active USB-specific support:** remove log-prefix parsing (`USBREF`, `USBCTRL`, `VBUSREF`) in favor of structured provider status, dead/historical CDC loader only after reachability audit, and class-specific serial app wording when modified (version bump required). Physically exercise a fourth provider via generic API without rebuilding firmware.
5. **Package finishing:** safe canonical `usb-cdc-acm` identity/version migration from legacy `-v2` and ABI1; remove signing/P-256/security-floor production paths while retaining SHA/TLS/ABI/import/rollback; nested resources/schema and per-ID roots for four kinds; coherent release/installed version guards. Ensure older loose pairs migrate safely and unknown SD files survive.
6. **Performance, integration, regression:** generation-bound ELF verification receipts/invalidation, no repeated entire-ELF hot-path hashes, installed controller link/admission and scoped imports, proportionate firmware/ELF/app/package builds and CI source defects. Debug actual failures, preserve in-use/unload safety and offline operation. Native I²C/SPI/UART takeover, other board peripherals, Wi-Fi migration and CAM provisioning remain U3/U4 as specified.

**Next source action:** inspect/fix BQ 0x6B ownership and firmware USB orchestration with current linked code. Address any new provider-builder/bridge build errors when actual CI reports them. No second PR, unauthorized merge, release or hardware testing.
