# Unified Package Manager — software acceptance matrix and final U1 hardware handoff

**Updated September 18, 2026.** Governed by [package scope](PACKAGE_MANAGER_SCOPE_CONTRACT.md), [ordinary format](RISC_PACKAGE_FORMAT.md), [MVP state](UNIFIED_PACKAGE_MANAGER_MVP.md) and the integrated [U1 milestone](NEXT_HARDWARE_TEST_MILESTONE.md). Merged #76/#78/#79 already delivered ordinary package plumbing and App Store/Driver Manager integration; the user reports most package-management systems are hardware-tested and check out. Preserve evidence where available; do not call unreported tests PASS. USB driver/Serial Monitor behavior has NOT been owner-tested. This test plan contains implementation/host gates for contributors and **one final owner hardware sheet**, not repeated physical test requests between workstreams.

## 0. Evidence and safety

Record exact source branch/commit, both board targets where applicable, firmware image and SHA, app/driver/service/provider fixture artifacts and hashes, architecture/runtime ABI, catalog/release identity, SD layout, test date, build commands/logs and actual CI conclusion. Back up all working firmware and SD data before destructive fault injection. Use development/disposable SD for simulated power interruption; never interrupt firmware flashing or unsafe power/charger operation. Keep firmware/app/driver artifacts from **the same commit/release snapshot**. Do not equate CI, host simulation and physical acceptance.

## 1. Contributor-side software and build gates — before owner handoff

Run established relevant host suites including `bash test/run_driver_test.sh`, `bash test/run_springboard_test.sh` and **ordinary** `bash test/run_unified_package_mvp.sh` after confirming its current contents; remove P-256/signer/floor experiment test code and the separate signed experiment runner as part of U1. Add/execute production-path tests for both SD and online source adapters, generic catalog index, dependency/identity/version compatibility, installed inventory, recovery and full UI/API four-kind lifecycle. A signed-only harness can never substitute for these tests and must not remain as optional active-tree scope.

Build both applicable firmware environments (`pio run -e t5s3-pro`, `pio run -e lilygo-epd47-s3`), real ELF driver provider chain, and `scripts/build_all_apps.py`; run strict actual ELF import/relocation audits and release-equivalent staging. Preserve firmware artifacts outside `.pio` before later ELF builds; recheck SHA before asset publication/staging. Source/link architecture audits must prove no normal firmware USB host/class/VID/PID/session/discovery/direct stream, hard-coded provider IDs, `usb-provider-catalog.json` or package-signing implementation remain. A narrow boot/debug port exception is documented by file and purpose, not implicitly ignored.

**Do not stop development to await CI.** Push/check opportunistically, work on the next independent part, resolve returned failures as they appear and preserve a remaining-work ledger. At U1 completion report actual latest check states; pending/failed jobs are not successes. Do not auto-merge, tag, release or flash to qualify work.

## 2. Full four-kind, two-source matrix

For each `application`, `driver`, `service`, `provider`, exercise existing real packages where available plus minimal functional fixtures, from both SD and online delivery. Package kinds are schema categories, not a demand to build unrelated services. Each row must use the **same ordinary manager** through the relevant public API/UI, not a legacy or signed-prototype transaction. Log exact identity/version, source, result, installed generation and contents.

| ID | Action | Required observation |
| --- | --- | --- |
| M1 | Inspect/discover | Bounded manifest; offline works without network/catalog; online generic all-kind index lists compatible package from one immutable release snapshot. |
| M2 | Install | Common engine stages/checks exact files, publishes/inventories kind/ID/version; reboot survives; no automatic hardware activation or privilege. |
| M3 | Update/version policy | One transaction for newer version; equal/downgrade behavior explicit; older generation preserved on failure. |
| M4 | Corrupt/truncate/extra file or wrong hash | Reject with reason; previous installation and unmanaged data unchanged. A digest establishes integrity only. |
| M5 | Wrong CPU/runtime API, missing/cyclic/ambiguous dependency | Fail safely with exact reason, no partial hardware activation or incompatible import. |
| M6 | Update/uninstall mapped or unsafe-to-quiesce module | Refuse or safely defer; no live code unmapping, lost dependency pin or physically unsafe release. |
| M7 | Download truncation, mixed release or stage/rename interruption | Reject mismatched snapshot; recover previous/committed generation and avoid lockout or deletion of foreign files. |
| M8 | Uninstall then reboot; online unavailable | Correct inventory, safe removal and dependency revoke; no compiled driver fallback; catalog outage never hides local inventory. |
| M9 | Authorization and loader | Ordinary app cannot import privileged ABI; authorized physical provider admitted through independent policy without signing keys, receipts or NVS floors. |
| M10 | Catalog independence and extensibility | Delete/unpublish `usb-provider-catalog.json`, retain normal offline/installed/USB graph operation; add new compatible driver from manifest/build without firmware/exporter source edit. |
| M11 | App/UI parity | App Store, Driver Manager and Package Manager list relevant online and installed packages, expose inspect/install/update/uninstall/recovery, share the same engine and preserve working navigation. |

Record PASS, FAIL, BLOCKED or NOT RUN individually for each kind/source and test. A working app install is not proof that service/provider online UI is present. Missing U1 functionality is a **software implementation blocker**, not something to send the owner to test.

## 3. Fault and cleanup tests

Test malformed/oversized manifests; duplicate ID/names, traversal/FAT aliases and unknown extra files; unsupported ELF/ABI, dependency graph cycles; short read/partial download; source file mutation after inspection; integrity failure, full SD and failed rename; restart after target-to-backup, stage-to-target and target-to-removing; repeated install/remove; mapped-package update and failed quiescence quarantine. Exercise catalog missing/unreachable/malformed while offline install and installed inventory still work. Verify old legacy loose app/driver compatibility through the common adapter and no separate transaction. Verify signing-specific source, test runners, release gates and experimental active docs are **removed**, while SHA-256, privileges, TLS unrelated to package signing and transaction safety remain. Previously installed unsupported signed-only data must be preserved and explicitly reported, not silently deleted.

## 4. Single owner hardware acceptance after software milestone U1

Do not request a device test between A–E. After all U1 code/build/static/host gates are satisfied, provide one exact artifact manifest and recovery procedure and ask the owner to run this coherent hardware protocol on a matching board and backed-up SD:

1. Confirm clean boot, UI/SD/apps and Manager offline installed inventory with Wi-Fi disconnected and **without USB drivers**. Serial capability absent and no compiled USB fallback or unexpected VBUS activation.
2. Install the USB controller/host/I2C/VBUS/clock/class graph and Serial Monitor from common package routes; verify dependencies/inventory and no firmware flash for a new compatible driver.
3. Connect the known ESP32-S3-CAM; observe sustained 5 V/USB power safely, boot, CDC recognition, baud/DTR/RTS when supported, continuing RX logs, TX where applicable, controls and scrolling. Test CP210x separately if that device is available; otherwise NOT RUN.
4. Disconnect/reconnect and rapid replug, verify provider-originated records, revoked/stale handles and stream/session recovery; test competing clients, close/unload and memory/power safety. Check real I2C/charger exclusive ownership and IRQ/DMA/callback drain where safely measurable.
5. Disable/remove class then host driver with preserved SD backup; capability must vanish, no firmware fallback; reinstall and see capability return without rebuilding/reflashing. Corrupt/interrupt a package update with safe prepared fixture and confirm prior generation/recovery.
6. Run a representative all-kind package route and generic catalog-missing offline test, record exact outcomes and serial logs. Different ESP CPUs/board variants not physically available stay NOT RUN, never implied compatible from a T5S3 run.

Only the owner closes physical acceptance. A failed hardware result returns to the same implementation PR for a specific fix, not a request to re-approve each intermediate commit. No automated merge or release follows from CI alone.

## 5. Evidence report template

| Test | Exact commit / board / kind / source | PASS / FAIL / BLOCKED / NOT RUN | Log / artifact / reproducible evidence | Next action |
| --- | --- | --- | --- | --- |
| Ordinary four-kind host matrix and catalog independence | | | | |
| Signed-code/source purge with retained generic checks | | | | |
| Firmware and real ELF app/driver builds + linked hardware-boundary audit | | | | |
| Online and SD M1–M11 per kind | | | | |
| Failure/recovery and mapped-module tests | | | | |
| CI state at handoff | | | | |
| Physical USB/Serial Monitor/I2C/VBUS acceptance (owner) | | PENDING OWNER TEST | | |

**Stop rule:** U1 software READY requires all implemented source paths, generic catalog independence, signing purge, full USB firmware extraction and recorded relevant local build/host/static results; physical evidence is explicitly pending until the owner tests. No separate signed-prototype green result, firmware ELF publication or intermittent owner testing substitutes for this one completed unit.
