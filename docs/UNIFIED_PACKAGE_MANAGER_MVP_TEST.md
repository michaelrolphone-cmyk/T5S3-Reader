# Unified Package Manager — MVP test and handoff

**Normative scope:** [UNIFIED_PACKAGE_MANAGER_SCOPE_CORRECTION.md](UNIFIED_PACKAGE_MANAGER_SCOPE_CORRECTION.md), [revised MVP](UNIFIED_PACKAGE_MANAGER_MVP.md) and [package model](RISC_PACKAGE_FORMAT.md). The requested MVP is common app/driver/service/provider package management, independent of cryptographic signatures. The branch's existing signed-only test harness is an *experimental regression suite*, not a pass criterion for the requested unified manager. Do not call the new MVP implemented until the code and UI actually meet these tests.

## 0. Preparation and evidence

1. Use development boards and a disposable SD card. Back up the working firmware, SD card (including hidden files) and necessary settings; do not power-cut during firmware flashing or NVS maintenance.
2. Record exact branch commit SHA, board, firmware version/SHA, SD filesystem, test packages and source (SD/online), serial log and test date. Run host/board tests against the same exact commit and compatible app/driver assets.
3. Use the documented build environments (`t5s3-pro`, `lilygo-epd47-s3`); do not interchange images. CI/build success is not physical acceptance.

## 1. Host checks

Run the existing broad regression suites:

```bash
bash test/run_driver_test.sh
bash test/run_springboard_test.sh
```

Also run `bash test/run_unified_package_mvp.sh` if that script remains in the branch, **but label its P-256, signer/provenance and NVS-floor results `EXPERIMENTAL-SIGNED`**. A green signed-only test does not satisfy unsigned package acceptance. Once implemented, the ordinary manager must have its own host tests and runner covering all four kinds, online/SD source equivalence, one install engine, corrupt/truncated files, architecture/API and dependency mismatch, path traversal/duplicate/alias rejection, SHA-256 content mismatch, active mapped-ELF update refusal, stage/read/rename failures, recovery across each file transaction phase, inventory, update and uninstall. Host tests must show packages function with NO keys, signatures, trust enrollment or signing CLI. Report missing harness/tests `BLOCKED`, not PASS.

Check all applicable current-head GitHub Actions jobs (host and both boards); older, superseded or cancelled runs do not validate latest changes.

## 2. Build and firmware smoke — both boards

```bash
pio run -e t5s3-pro
pio run -e lilygo-epd47-s3
```

Flash only to the matching development board using the established procedure. Capture serial logs at 115200 baud. Verify boot, home UI, SD detection, Apps screen, navigation, and a reboot. A crash, unexpected file deletion, lost installed package, loader failure or recovery loop is a failure.

## 3. Common manager functional matrix

For EACH of application, driver, service and provider, perform the following on test packages, from offline SD and from an online source. If the UI/manager entrypoint for a kind or source does not exist, mark that cell **BLOCKED**, not passed by testing the legacy installer or signed-prototype API. Record package identity/version, exact source, installed paths and logs.

| ID | Action | Required observation |
|---|---|---|
| M1 | Inspect/discover compatible package | Local mode works with Wi-Fi off; online mode works when connected; identical identity/compatibility rules. No signing key needed. |
| M2 | Install from source | One shared manager stages and verifies the package, publishes it, inventories exact kind/ID/version and survives reboot. No automatic hardware grant or start. |
| M3 | Update and same-version action | One managed replacement path; version policy and state reported accurately; previous version preserved on failure. |
| M4 | Corrupt/truncate ELF or provide wrong digest | Reject with explicit reason; leave previous install and unrelated files untouched. SHA-256 is integrity only. |
| M5 | Incompatible runtime/architecture or missing dependency | Reject before loading with actionable diagnostics; no partial install or unexpected hardware activation. |
| M6 | Attempt update while package ELF remains mapped | Refuse or safely defer; no remapping or lost running code; retry after release. |
| M7 | Interrupted stage/rename/reboot during controlled test | Recover a valid previous or new generation without deleting unmanaged files; no permanent install lockout. |
| M8 | Uninstall, then reboot | Inventory and owned files removed safely; unrelated user files preserved; no orphaned provider activation or fallback. |
| M9 | Permission and privileged loader checks | Package metadata never grants device rights; ordinary app cannot import privileged OS/CPU ABI, while appropriately authorized physical provider can load without signed receipt. |

Where source, four-kind lifecycle, generic loader or hardware functionality has not been implemented, report BLOCKED with a concrete code/entrypoint gap. A legacy App Store or Driver Manager success is useful compatibility evidence but does NOT establish the common manager.

## 4. Negative tests and recovery

Test malformed manifests, duplicate IDs/names, traversal and FAT aliases, unsupported ELF/ABI, dependency cycles, short read/download, input replacement during staging, bad digest, full SD, rename failures, repeated install/remove and reboot recovery. Use fault-injected host tests for destructive cuts where physical test apparatus is unavailable; distinguish those results from physical results. Do not introduce publisher-signature acceptance, signer rotation, NVS cryptographic floors or malicious-SD trust proofs as new gates. Continue enforcing normal input bounds, file-integrity checks, loader import restrictions, runtime authorization and in-use mapping safety.

Physical USB/I²C driver launch, exclusive ownership, voltage/current/backfeed, hotplug and ISR/DMA teardown belong to the linked PR #78 hardware test procedure; record them separately. A successful installation does not mean hardware activation was proven.

## 5. Test report

| Test | Commit / board / package kind / source | PASS / FAIL / BLOCKED / NOT RUN | Evidence | Next action |
|---|---|---|---|---|
| Host unified-manager matrix | | | | |
| Existing signed experiment (non-gating) | | | | |
| CI host / T5S3 / EPD47 | | | | |
| SD M1–M9 per kind | | | | |
| Online M1–M9 per kind | | | | |
| Recovery/uninstall | | | | |
| Independent loader authorization | | | | |

**MVP completion requires the actual shared unsigned-capable manager, source/UI migration, four-kind lifecycle, recovery and the documented host and board acceptance.** Existing P-256 signed-prototype success, green builds or functioning separate legacy installers are not substitutes. Keep the PR draft until these functional criteria are met; do not hold it for out-of-scope signing-key, signed-provenance or cryptographic rollback infrastructure.
