# U1 implementation ledger

PR: [#96](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/96) branch `impl/u1-riscrte` from master `52226bc` after specification PR #86 merged.

## Completed

- 2026-09-19: Established implementation branch/PR from current master. Did not reuse `docs/usb-migration-package-manager-milestone`.
- Generic streams registry (`6fed5a6`):
  - Installed-ELF producer/sink endpoints are buffer-backed (`publishEndpoint`). No ELF function pointers stored in the registry.
  - Cross-context `grant` / `revoke` / `connectAcross` with generation-safe handles.
  - Protected record endpoints cannot pipe into a public sink.
  - Provider I/O is snapshotted (`ExternalIo`); `pumpPrepare`/`pumpComplete` allow the firmware scheduler to drop the stream mutex.
  - Existing byte v1 and record v2 same-owner APIs unchanged.
- serial.port RX/TX pair is published buffer endpoints (`nativeStreamOpenUsbPair`). USB bytes shuttle outside the registry via `produce`/`consume`. ELF consumers still see WRITE-denied RX and READ-denied TX.
- `open_usb` is no longer a firmware USB data plane. The v1/v2 ABI slot remains and returns `T5_STREAM_UNSUPPORTED`. Serial Monitor / ESP ROM use published `serial.port` endpoints only. DirectStream physical claims are removed; `serial.port` is the exclusive USB serial owner.
- I²C ownership inventory for the USB/VBUS path:
  - `i2c-esp32s3-v2` publishes `i2c.bus` and is the only legal importer of `risc_fw_i2c_transact_v1` (`ProviderModuleV2` + privileged allowlist).
  - `board-power-t5s3-v2` is the single BQ25896 `0x6B` owner and publishes `board.power.vbus` through `i2c.bus` claims (duplicate address claims fail).
  - No PCA9535 owner is on the U1 USB activation path; TPS65185 is not a VBUS owner.
- USB class stream session (`e97ebcf`):
  - `RuntimeUsb::ClassStreamSession` binds an installed class ELF (`risc_usb_cdc_api_v1` shape) and publishes RX/TX endpoints.
  - Class `read`/`write` run in `pump()` outside the stream registry mutex; bytes enter/leave via `produce`/`consume`.
  - Grants, line coding and DTR/RTS stay on the class ELF. No `T5UsbApi` in this path.
  - Host/class/controller ELF sources already exist; CDC/CP210x/host manifests are `u1-*-functional`. Controller remains `experimental-hardware-port-not-yet-linkable` (IDF USB host + `board.power.vbus`).
- Class-ELF control plane (`NativeUsbClassBridge`):
  - `nativeUsbClassBind` installs a class-ELF ops table. A missing bind makes `usb.serial` unavailable; there is no `t5_usb_get_api` fallback.
  - `NativeSerialPortBridge` acquire/configure/control/status/release call `nativeUsbClass*` instead of `T5UsbApi` `serial_*`.
  - Discovery (`nativeUsbProviderAttach` / owner-task tick) remains snapshot-only.
  - Host check: `usb_class_bridge_bind_test` compiles production serial + class bridges with `t5_usb_get_api` returning null.
- Installed class ELF bind + class-session data plane:
  - `nativeUsbClassEnsureInstalled(vid)` acquires `usb-cdc-acm-v2` / `usb-cp210x-v2` from the provider graph and `BindApi` without opening.
  - `usb.serial` acquire calls EnsureInstalled, starts the class token, publishes RX/TX, then `attachPublished`.
  - `nativeStreamOpenUsbPair` no longer consults `t5_usb_get_api`. Pair shuttle uses `nativeUsbClassRead`/`Write`.
  - `NativeUsbBridge` opens through the bound class table (`openBoundClass` / Adopt) and routes compatibility `serial_read`/`serial_write` through the class bridge when a token is adopted.
  - Stream registry `read`/`write` honor grants (`streamByHandle` + `allowed`).
- Four-kind `.rte.zip` bootstrap (this increment):
  - `PackageRteZip.h` inspects/packs stored ZIP; rejects slip, case-folded
    duplicates, encryption, DEFLATE/ZIP64, extra/comment, missing manifest.
  - `planRteZip` binds archive files to ordinary `.package.json` entries.
  - `PackageRteZipInstall.h` feeds the existing ordinary stage/transaction.
  - Generic `package-catalog.json` parser (`PackageCatalog.h`) requires one
    `.rte.zip` asset per package and refuses `usb-provider-catalog.json`.
  - Host packer `scripts/pack_rte_zip.py` emits stored archives + catalog rows.

## Checks

- `test/run_stream_test.sh` includes `usb_class_stream_session_test.cpp`, `usb_class_bridge_bind_test.cpp`, and the prior stream/bridge suite (ASan/UBSan).
- Firmware image not built in this increment. Physical USB/VBUS not exercised.

## Remaining U1

1. Route SD inbox and online intake through `installOrdinaryFromRteZip`; replace `usb-provider-catalog.json` exporters and Driver Manager URL.
2. Purge package-only P-256 / signed RISC-PKG / security-floor code; keep SHA-256, TLS, ordinary rollback.
3. Nested resource paths (schema bump) and per-ID installed roots for apps/services.
4. Installed-ELF verification performance (no repeated SHA/MD5 on ordinary launch).
5. Finish retiring leftover `NativeUsbBridge` `T5UsbApi` serial_* compatibility.
6. Integration/build defects only; no U2–U4 scope.

## Next source action

Wire SD/online four-kind intake to `.rte.zip` + `package-catalog.json`; delete USB catalog and four-file export.
