# U1 implementation ledger

PR: [#96](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/96), branch `impl/u1-riscrte`. This ledger records pushed source and actual audit evidence, not inferred implementation completion or hardware qualification. **Binding owner review and source-to-work allocation:** [USB contract remediation](USB_CONTRACT_VIOLATION_REMEDIATION.md); [four-milestone order](FOUR_MILESTONE_STREAM_FIRST_EXECUTION_ORDER.md). This documentation update does not complete any implementation item below.

## Branch integration

- `0a8c21c`: merged then-current `master` (`1ebfbfe`) into `impl/u1-riscrte`. The merge preserved U1 generic package-builder/test versions where master still carried the fixed USB version whitelist, and incorporated board-power/VBUS diagnostics, platform version, tests and firmware 1.2.33/1.2.34 artifacts.
- GitHub compare after that merge reported 0 commits behind master **at that check**. Recheck when master advances. No PR merge, tag, release or flash performed by this documentation update.

## Stream / USB source landed (as previously reported; revalidate after new edits)

- Generic ELF byte endpoints, grant/revoke/connectAcross, records compatibility and external provider I/O outside stream mutex. `open_usb` stays unsupported in favor of semantic `serial.port` RX/TX.
- Installed USB class ELF binding, checked teardown and retryable semantic leases retain token/table/package pin on uncertain physical close.
- `ClassStreamSession` and `NativeStreamBridge` retain RX/TX chunks across buffer backpressure, short/zero writes and retry; no accepted/unwritten suffix intentionally discarded.
- Duplicate class-session attachment was removed so `NativeSerialPortBridge` establishes physical ownership before attaching the published pair.
- I²C/VBUS provider source is present. USB controller ELF/IDF linkage was reported unresolved; verify current link output before claiming otherwise.

## Generic ordinary package path landed (previously reported)

- Schema-1 manifest/preflight/stage/transaction for application, driver, service, provider.
- Bounded `.rte.zip` bootstrap checks ZIP topology/CRC before SHA-256/ABI/import and recoverable publication; SD ZIP adapter retains unknown files.
- Generic `package-catalog.json` online discovery pins immutable release; `NativeOnlineRtePackageInstall` downloads exact ZIP into `/Packages/Inbox` with size/digest checks and calls same SD ZIP transaction.
- Package Manager exposes directory, ZIP and online operations with caller-kind filtering; managed nested app paths have scoped authority.

## App Store / release integration landed (previously reported)

- `befe901`: App Store normal online installation switched to common `t5_package_manager_api_v1::online_*` rather than legacy app-download ABI; SD directory/ZIP stays common.
- `41b5e5a`: App Store host fixture exercises generic online ABI.
- `f0fbca2`: native apps staged as ordinary application packages; legacy app catalog temporarily retained for older firmware.
- `5f28827`: generic release export accepts canonical underscore IDs, requires applications and drivers in real releases.
- `01bedb8`: generic release catalog contract verifies SHA/size/identity/inventory.
- `12431f7`: release workflow invokes catalog contract before publishing; not evidence workflow ran.

## September 19 owner USB contract audit — verified source versus open findings

The owner's analysis reports successful CH34x-to-CAM serial behavior. Treat this as **owner-reported device-path evidence**, not all-USB architecture, firmware build, PHY handoff or all-driver proof. The analysis referenced `master`; inspect **current PR #96 HEAD**, including newer work, before changing a file. This audit checked the U1 branch documents and selected source files, not complete link or hardware behavior.

- **Confirmed source-level U1 violation: firmware USB orchestrator.** `src/native/NativeUsbBridge.cpp` still contains `usb-host-v2` acquisition, fixed `usb-cp210x-v2`/`usb-cdc-acm-v2` choices and VID preference, host polling/descriptors, device/session state, line coding and `BOARD_T5S3_PRO` check. Physical transfers do call installed ELF interfaces, but a generic hardware-blind core cannot retain this USB-specific manager. Change to provider-originated device discovery/probing/publication and generic `serial.port` resolution. An existing compatibility label does not make these normal calls acceptable.
- **Confirmed source-level U1 violation: USB-specific serial bridge.** `src/native/NativeSerialPortBridge_implementation.inc` retains `NativeUsbDevices::Registry`, `UsbSerialProjection`, USB-specific session/physical leases and a fixed provider path. Replace device publication/leases/status/data with generic provider registry and class-ELF streams; preserve proper owner/generation revocation, console/error reporting and both client apps. Audit `NativeStreamBridge` and `NativeUsbClassBridge` for remaining `T5UsbApi` shuttling/coupling as a **current-code verification item**, rather than assuming the older review describes the latest bytes.
- **Confirmed source-level U1 safety violation: competing BQ writer.** `lib/Board_T5S3/BoardT5S3.cpp` still initializes BQ25896 with register reset and disables OTG; `src/native/NativeBatteryBridge.cpp` calls `Board::beginBatteryManagement()`/`readBatteryState()`. The installed `board_power_t5s3_v2` ELF also owns BQ `0x6B` VBUS operations. The conflict is a source-level **risk**, not proof of an observed electrical race. Consolidate **all normal-runtime BQ register writes** in one installed owner, safely serve charging/telemetry consumers or fail closed, and audit boot/sleep and any independently justified one-way bootstrap before removing legacy battery calls. Separate BQ27220 gauge belongs to U3.
- **I²C allowed exception:** `i2c_esp32s3_v2/driver.c` privately imports `risc_fw_i2c_transact_v1`; this is explicitly allowed in U1–U2. Audit actual sole-importer, boundedness, controller concurrency and no absent-bus fallback. Native takeover/removal is U3. Do **not** count this as unresolved U1 native-controller extraction.
- **PHY/boot console:** the owner reports a previous working `Serial.end()/begin()` sequence to prevent debug-CDC/USB-host PHY contention. Current inspected `NativeUsbBridge.cpp` does not show those calls in its inspected sections; safe current handoff is **not established**. Trace real port/controller code and link graph. Provide exclusive platform-port ↔ USB controller ELF ownership and verified restore after safe quiescence; do not delete the conflict mitigation or reintroduce a device-specific bridge.
- **Distribution coupling:** owner analysis identifies fixed USB list/version/exact-count in `build_installed_usb_stack.py` and `export_canonical_driver_release.py`, `usb-provider-catalog.json` in Driver Manager and four named/latest files in `NativeOnlineDriverInstall.h`. Earlier U1 generic catalog/App Store work does **not** prove these remaining paths are removed. Verify current versions and eliminate all normal fixed exports/discovery/downloader paths, preserving common engine/recovery and immutable release pinning.
- **Other U1 residue to audit by linked reachability:** `UsbCdcDriverRuntime.cpp` legacy fixed loader; USB log-prefix parsing in native serial bridge; fixed CH34x/CDC/CP210x wording in Serial Monitor. Remove only active/unneeded code, replace with structured provider errors, and bump any modified app manifest.
- **U3 tracked only, not a U1 USB blocker:** firmware `GpsKernelIo`, LoRa/SPI, board expander/touch/gauge/display/backlight, native I²C/SPI/UART bus takeover, and active `Esp32NetworkProvider` Wi-Fi implementation. U3 must audit network hardware ownership explicitly even though integrated Wi-Fi is not a board-added peripheral. Do not add Wi-Fi or unrelated physical drivers to U1.

## Actual validation / limits

- GitHub branch writes and old merge ancestry were confirmed. Previous ledger states latest source suites/firmware **not run on those commits**; do not extrapolate CI from earlier SHAs.
- No new build, runtime test, physical USB/VBUS/PHY or package-install hardware check was performed for **this specification update**.
- Release workflow was edited previously, not run or dispatched here. Keep owner-reported CH34x success distinct from independently checked U1 acceptance.
- Older App Store legacy release ABI may remain only as non-hardware compatibility; U1 normal App Store uses common package ABI.
- Driver Manager UI/recovery may remain while its online catalog/install moves to common index/ZIP; offline inventory must not depend on online catalog.

## Remaining U1 — implementation work order (do not mark complete on docs alone)

1. **Complete active package task without losing work:** migrate Driver Manager catalog/install to `RuntimeOnlinePackages::Catalog` + `OrdinaryZip`, retaining recovery/dependency UI; remove normal driver/USB catalogs and latest-release loose-file assets. Generalize the builder/exporter from fixed eight-driver counts and hard-coded versions/IDs to discovered manifests; prove adding a new class package does not require firmware/exporter edits.
2. **Eliminate dual BQ ownership before handing off USB:** classify every BQ touch in `BoardT5S3.cpp`, `NativeBatteryBridge.cpp`, startup and sleep; move register/power/charger behavior into existing single BQ owner or isolate a proven pre-runtime one-way bootstrap; preserve safe battery behavior and BQ27220 separation. Test charger read/init during active OTG and release/failure.
3. **Finish true USB extraction:** remove firmware host poll/descriptor and fixed provider/VID selection, class session/state, projection/transport shuttles and active `T5UsbApi` device work. Make installed host/class ELFs publish device/capability/generic RX/TX. Serial Monitor/ESP ROM resolve semantic `serial.port` without firmware USB special cases. Cover new fourth class, missing driver, attach/detach, lease revocation, partial I/O and no fallback; identify dead legacy loader via build/link audit.
4. **Preserve debug-console PHY safety:** trace real boot-console versus USB-host conflict/handoff in current branch, add isolated port resource lease and exclusive release/restore with fault quarantine, prove no overlapping PHY ownership. Never remove working mitigation solely to satisfy a grep.
5. **Finish U1 package hardening:** purge signing/P-256/trust/security-floor production code (keep SHA/TLS/ABI/import/rollback), canonicalize `usb-cdc-acm` lineage with bounded legacy migration, complete per-ID layout/four kinds and version bumps. Structured provider error reporting replaces parsing `USBREF`/`USBCTRL`/`VBUSREF`; update app wording if touched.
6. **Finish ELF launch performance and integration:** generation-bound verification receipts and invalidation remove repeated SHA/MD5 in ordinary launches/inventory while retaining install/recovery/explicit integrity; fix controller IDF linkage and actual source/build defects. Run targeted stream/native-app/package tests, firmware/ELF builds, symbol/import/reference scans and release-equivalent staging; report exact checks, failures and unrun items. Keep I²C firmware raw backend on allowed bus ELF until U3; no U2/U3/U4 scope expansion.

## Next source action

Continue any in-flight Driver Manager generic catalog/ZIP conversion to a coherent commit **without reverting concurrent work**, then inspect and repair the BQ `0x6B` dual-writer safety path and USB firmware orchestration. Use [USB contract remediation](USB_CONTRACT_VIOLATION_REMEDIATION.md) for file-by-file acceptance. Re-read current branch HEAD and this ledger at every `Continue`, record implementation commits and tests, and update statuses only on code evidence. No intermediate manual hardware test or unapproved merge/release.
