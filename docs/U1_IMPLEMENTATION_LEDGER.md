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

## Checks

- `test/run_stream_test.sh` host suite including `elf_endpoint_test.cpp`, `bridge_test.cpp` and `usb_direct_ownership_test.cpp` passed locally (ASan/UBSan).
- Firmware image not built in this increment.

## Remaining U1

1. USB controller/host/class/session extraction into functional ELFs.
2. Unified four-kind `.rte.zip` package engine; purge USB catalog and P-256 signing; keep SHA-256 / TLS / rollback.
3. Installed-ELF verification performance (no repeated SHA/MD5 on ordinary launch).
4. Integration/build defects only; no U2–U4 scope.

## Next source action

Extract USB controller/host/class/session into functional ELFs that consume `serial.port` and `board.power.vbus`, not firmware USB I/O.
