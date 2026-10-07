# BUG-72: no-update result is a normal outcome

Repository: `michaelrolphone-cmyk/T5S3-Reader` (1367546328).
Baseline: `1d97f4f4050a2f0ec693d5b0f95b27f7be4e49d2` (`master`, after PR #384).
Repair branch: `fix/bug72-ota-no-update-result`, targeting `master`.

`NativeOtaBridge::mapResult` exposes `OtaUpdater::NO_UPDATE` as
`T5_OTA_NO_UPDATE`. The installed Firmware Update app previously treated this
explicit no-change result as an update-check failure, making its existing
up-to-date screen unreachable for that service outcome.

The app now admits `T5_OTA_NO_UPDATE` alongside successful checks and routes it
directly to the existing no-update screen. It does not query potentially stale
newer-version metadata on that path. Genuine check errors remain errors; an
available update still requires the existing explicit confirmation event.
No firmware or service behavior, network ownership, installation policy or
API changes. Only this app advances from **1.0.2 to 1.0.3**; its firmware floor
stays 1.1.22. Latest published app lineage was `app-ota_update-v1.0.2`.

## Verification

- Compile the entire production app with the existing host API/UI fixture:
  `cc -std=c11 -Wall -Wextra -Werror -Ilib/NativeApps/include Apps/ota_update.c test/native_apps/ota_update_test.c -o /tmp/ota-update-test`
- The 19-case regression covers explicit no-update despite stale newer metadata,
  five terminal-input routes, successful/current and successful/newer checks,
  every declared error plus an unknown result, cancellation/poll termination,
  ignored body taps, confirmed installation failure/success, restart sequencing,
  and repeated reopening/retry after errors and a previous available update.
- The final regression against the original baseline app compiles successfully
  and fails the expected no-update-screen assertion (exit 134).
- Strict C11 and ASan/UBSan runs pass. Local sanitizer runs use
  `ASAN_OPTIONS=detect_leaks=0` because of the container's ptrace limitation;
  no local LeakSanitizer result is claimed.
- The existing `test/run_springboard_test.sh` already builds and runs this exact
  fixture in the normal aggregate/CI path.
- Local app packaging was attempted with
  `python3 scripts/build_all_apps.py --id ota_update`; the Xtensa compiler is
  absent. Hosted app/firmware builds remain necessary compilation and package
  identity evidence. No physical network, device, update/flash or reboot test
  is claimed. API/UI/OTA operations in the regression are fixtures.

Canonical claim and final PR/check results are recorded on coordination PR #332.
The shared ledger's recorded writer is unchanged; this repair does not modify
the ledger files, another owner's work, default branches or published assets.
