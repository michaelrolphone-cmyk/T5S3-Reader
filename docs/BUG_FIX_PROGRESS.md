# Bug repair progress

Snapshot: 2026-09-30 23:58 UTC. Coordinator-reported runtime/PR evidence below; the ledger reconciliation itself made no implementation changes.

## Initial run result and next claim

- Run: initial pipeline restoration
- Last coordinator: `restore_bug_pipeline`; active ledger owner: none
- State: completed; initial restoration and exact-head verification finished. No active run or implementation claim remains
- Baseline master: `2b45ab662c0ffe3650ce0f841083ea47886022c9`
- Coordination PR: [#327](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/327), draft and unmerged
- Coordination branch: [`automation/bug-ledger`](https://github.com/michaelrolphone-cmyk/T5S3-Reader/tree/automation/bug-ledger)
- Outstanding integration item: **205**, File Browser oversized USB source handle leak; draft PR counts as one outstanding code batch
- Repair branch: [`fix/file-browser-oversize-usb-handle`](https://github.com/michaelrolphone-cmyk/T5S3-Reader/tree/fix/file-browser-oversize-usb-handle)
- Draft PR: [#326](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/326)
- Verified remote head: `5ec33bbe60928ec73f8d5f9fdc28023aa3a56c79`
- Integration status: **Awaiting merge**, still outstanding on master. The recovered branch previously had no matching PR; its earlier implementation is not a prior merge.
- Next scheduled invocation: 2026-10-01 00:50 UTC. It has not run at this snapshot. It must read this record, recheck PR #326 and current master, and claim the next bounded work before editing; do not duplicate #205. Only one code batch is outstanding from this pipeline.

## #205 verification and next action

- The coordinator revalidated the defect on master, recovered the existing repair, backmerged current master cleanly, and published the draft above.
- Runtime/source regressions, six retirement tests, six capability tests, and full C syntax check passed. The original faulty mutation fails the expected runtime assertion.
- ASan/UBSan runtime checks passed with `ASAN_OPTIONS=detect_leaks=0`; LeakSanitizer is unavailable under container ptrace. Leak detection is not claimed.
- File Browser version: **1.3.0 → 1.3.1**.
- Local Xtensa app build: attempted, blocked because the compiler is absent. CI subsequently built and validated all released apps/manifests and native ELFs on both supported firmware boards.
- Native aggregate suite: `ASAN_OPTIONS=detect_leaks=0 bash test/run_native_app_test.sh` passed, exit 0.
- Exact-head CI: [run 36793336681](https://github.com/michaelrolphone-cmyk/T5S3-Reader/actions/runs/36793336681) completed successfully on `5ec33bbe60928ec73f8d5f9fdc28023aa3a56c79`: host parser tests, t5s3-pro build, and lilygo-epd47-s3 build all passed, including native ELF and all-app/manifest validation.
- Next action: retain #205 awaiting merge; check its PR for subsequent changes before selecting new work. Revalidate candidate #16/#17 as the next related batch if no overlap and capacity permits. No second code batch was started in this restoration. No hardware qualification, merge, release or deployment occurred.
- Current-run GitHub connector branch/blob/tree/commit/ref and draft-PR writes all succeeded. Plain git push could not authenticate in this cloud shell; connector publication and subsequent remote fetch/hash verification succeeded. This does not prove a future scheduled execution has write access; each run must verify its actual result.

## Reconciliation result

- Inspected all 14 later scan branches through 2026-09-30 17:22 MDT. Each branch's unique diff changes only `bugs.md`.
- Preserved 42 later source reports as 41 distinct canonical reports, IDs **208–248**. The 08:22 #209 and 15:20 #208 exact-inventory reports are aliases of canonical **215**.
- The 16:24 reports map to **243–245**; the different 17:22 reports map to **246–248**. No number collision was allowed to overwrite either set.
- Retained inventory: **237** records. **236** outstanding: **235** need revalidation and **205** awaits merge. **91** is resolved. The next unused canonical ID is **249**.
- Only inherited #91 and #205 were specifically revalidated in this restoration. All other inherited reports and all newly imported reports retain historical evidence and an explicit needs-revalidation state.
- Existing bug IDs **4, 6–12** were already merged through [PR #246](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/246); do not reopen them merely because historical scan/fix branches still exist.

## #91 resolution evidence

Merged [PR #283](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/283), merge `36be2ee496468feb3779616e69f61e3a671c192b`, is an ancestor of this baseline. `Drivers/gt911_touch/driver.c:208–214` keeps READY latched when the contact-data read fails. `test/drivers/gt911_touch_test.c:208–219` covers three injected failures followed by successful exactly-once delivery.

`ASAN_OPTIONS=detect_leaks=0 bash test/run_gt911_touch_test.sh` passed on the baseline. The default sanitizer command could not complete LeakSanitizer under ptrace; no leak detection or physical-device qualification is claimed.

## Handoff rules

Before clearing or transferring the active claim, record the latest remote branch/PR head, results, unfinished work, owner status, and next action. Update the snapshot timestamp only when new evidence was actually checked. Resume existing work before opening another fix; never infer completion from elapsed time, a closed PR, or a historical status sentence.
