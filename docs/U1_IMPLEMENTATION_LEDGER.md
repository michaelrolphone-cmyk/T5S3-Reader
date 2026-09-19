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
- serial.port RX/TX pair is now published buffer endpoints (`nativeStreamOpenUsbPair`). USB bytes are shuttled outside the registry via `produce`/`consume`. The ELF consumer still sees WRITE-denied RX and READ-denied TX. `open_usb` remains as the compatibility data plane until the next increment.
- Existing `Drivers/i2c_esp32s3_v2` already publishes `i2c.bus` and is the only importer of `risc_fw_i2c_transact_v1`. U1 still must enforce that peripheral ELFs do not import the firmware API and assign single BQ/PCA owners.

## Checks

- `test/run_stream_test.sh` host suite including `elf_endpoint_test.cpp` and `bridge_test.cpp` passed locally (ASan/UBSan).
- Firmware image not built in this increment.

## Remaining U1

1. Remove firmware USB data plane and `open_usb`; Serial Monitor / ESP ROM stay on published `serial.port` endpoints only.
2. Confirm I²C bus ELF as sole importer; one BQ25896 owner for VBUS/charger; one PCA9535 owner if USB path needs it; no TPS-as-VBUS assumption.
3. USB controller/host/class/session extraction into functional ELFs.
4. Unified four-kind `.rte.zip` package engine; purge USB catalog and P-256 signing; keep SHA-256 / TLS / rollback.
5. Installed-ELF verification performance (no repeated SHA/MD5 on ordinary launch).
6. Integration/build defects only; no U2–U4 scope.

## Next source action

Delete `open_usb` after remaining tests are retargeted to `serial.port`, then lock the I²C importer/ownership graph.
