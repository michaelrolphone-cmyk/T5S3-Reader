# U1 implementation ledger

PR branch: `impl/u1-riscrte` from master `52226bc` after specification PR #86 merged.

## Completed

- 2026-09-19: Established implementation branch/PR from current master. Did not reuse `docs/usb-migration-package-manager-milestone`.
- Generic streams (in progress, first production increment):
  - Installed-ELF producer/sink endpoints are buffer-backed (`publishEndpoint`). No ELF function pointers stored in the registry.
  - Cross-context `grant` / `revoke` / `connectAcross` with generation-safe handles.
  - Protected record endpoints cannot pipe into a public sink.
  - Provider I/O is snapshotted (`ExternalIo`) and the firmware scheduler drops the stream mutex before file/USB/network callbacks.
  - Existing byte v1 and record v2 same-owner APIs unchanged.

## Checks

- `test/run_stream_test.sh` host suite including new `elf_endpoint_test.cpp` (run locally on this increment).
- Firmware image not built in this increment.

## Remaining U1

1. Finish stream integration: Serial Monitor / programmer consume ELF `serial.port` streams; remove firmware USB data plane and `open_usb`.
2. I²C bus ELF as sole importer of the temporary firmware controller API; BQ25896 single owner for VBUS/charger; PCA9535 single owner if USB path needs it; no TPS-as-VBUS assumption.
3. USB controller/host/class/session extraction into functional ELFs.
4. Unified four-kind `.rte.zip` package engine; purge USB catalog and P-256 signing; keep SHA-256 / TLS / rollback.
5. Installed-ELF verification performance (no repeated SHA/MD5 on ordinary launch).
6. Integration/build defects only; no U2–U4 scope.

## Next source action

Wire Serial Monitor and ESP ROM onto published ELF endpoints, then install the I²C bus ELF cutover.
