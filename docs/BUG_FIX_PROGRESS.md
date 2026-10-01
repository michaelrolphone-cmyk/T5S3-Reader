# Bug repair progress

Snapshot: 2026-10-01 01:03 UTC. This record supersedes the pre-merge restoration snapshot.

## Active hourly run

- Run: 2026-10-01 00:50 UTC; coordinator and active ledger owner: `fix_button_remap_batch`
- State: repair published; monitoring exact-head CI
- Canonical bugs: **16 and 17**, **Awaiting merge**
- Baseline master: `be82695ea0ecb14525c0de1ddc78cd0c77e4614b`
- Durable claim published before implementation: `08ba173adc72216635f07f058fda0068561cf287`
- Coordination branch: `automation/bug-ledger`; draft [#332](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/332), replacing merged #327
- Repair branch: `fix/button-remap-16-17`; draft [#333](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/333), targeting master
- Verified remote head: `3619048c6b178ddf6d767842b797d6fb1d7a87d4`
- Outstanding code batches from this pipeline: **one**. #205 is merged and does not occupy a slot
- Next: follow [exact-head CI run 36799152027](https://github.com/michaelrolphone-cmyk/T5S3-Reader/actions/runs/36799152027), currently pending; repair in-scope failures, then release the active claim. Do not start a duplicate run while this owner is active

## Button Remap evidence

Current-master source still mutated all four live mapping fields before save without rollback, including reset defaults. The app still advertised Reset/Cancel on front Left/Right but handled side Up/Down. Open PR paths #331, #277, #220, #194 and #96 were checked: none changes the native remap bridge or app. #220 changes the separate legacy activity, UI API and firmware version; those code paths are not edited here. The shared firmware version change is necessary for this firmware-owned bridge.

The fix restores the old four fields before returning failed save, preserving original physical-button decoding for retry; Reset uses the same path. Persistent side-button instructions replace the misleading front-button hints.

- Button Remap: **1.0.0 → 1.0.1**; firmware: **1.3.47 → 1.3.48**. Old versions verified in the published release-index and tags. No new ABI or minimum-firmware requirement
- Actual C app and C++ bridge linked into a host regression with persistence/input/UI stubs
- **PASS:** ASan/UBSan regression, 576 mapping transitions and 576 physical-input retry transitions, repeated failed saves, failed reset/reopen/retry, cancel at each stage and after save failure, API/null/duplicate/out-of-range validation, touch selection, exit/poll cleanup
- **Expected FAIL on baseline:** separate bridge case aborts on retained live-mapping assertion; chrome case aborts on misleading front previous-label assertion
- **PASS:** full `ASAN_OPTIONS=detect_leaks=0 bash test/run_native_app_test.sh` after test integration, app C syntax, shell syntax and whitespace checks
- Local `python3 scripts/build_all_apps.py --id button_remap` attempted but blocked by missing Xtensa compiler. Target ELF/sidecar/catalog verification is pending CI
- LeakSanitizer disabled under container ptrace. The persistence stub models failed atomic storage; these tests do not establish actual filesystem durability
- No hardware qualification, merge, release, deployment or flash

This unattended run successfully created a branch, trees/commits, non-force ref updates and draft PRs through the connected GitHub tools. Remote fetch/hash and working-tree comparisons verified publication. This proves this run's writes, not permissions for future runs.

## Resolved on current master

- **205:** [#326](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/326) merged as `f4952a31bd0f9ae5c8051b80b088e16426308b88` at 2026-10-01 00:02:16 UTC. Merge ancestry and compiled USB-handle runtime regression reverified on the baseline. File Browser version 1.3.1. Its earlier native aggregate and [exact-head CI 36793336681](https://github.com/michaelrolphone-cmyk/T5S3-Reader/actions/runs/36793336681) passed all three jobs, including both firmware boards and app validation
- **91:** [#283](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/283), merge `36be2ee496468feb3779616e69f61e3a671c192b`, is an ancestor. GT911 retains READY after contact-read failures; three failures followed by exactly-once delivery pass again in `test/run_gt911_touch_test.sh`
- Coordination [#327](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/327) merged as `2a55d0a05316a3fe5478011ada8285fcb24c1885` at 00:04:41 UTC. Neither closed PR was edited
- These fresh regressions used `ASAN_OPTIONS=detect_leaks=0`; no leak detection or device qualification is claimed

## Preserved inventory and provenance

237 retained canonical reports; **235 outstanding**: 233 need revalidation and 16/17 await merge. 91 and 205 are resolved. The next unused canonical ID is **249**.

The initial restoration retained all 196 master reports and reconciled 42 later source reports from 14 scan branches into 41 distinct IDs **208–248**. Immutable source aliases and original evidence remain in `bugs.md`. The 08:22 #209 / 15:20 #208 duplicate maps to canonical **215**. The distinct 16:24 and 17:22 reports retain IDs 243–245 and 246–248. Older bugs **4, 6–12** were already merged through [#246](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/246); do not reopen them from old scan/fix branches.

## Handoff rules

Follow [BUG_FIX_WORKFLOW.md](BUG_FIX_WORKFLOW.md). One ledger writer, captured-parent non-force updates, at most two outstanding code batches, current-master revalidation and durable claims before source changes. Record latest remote head, results, unfinished work, owner and next action before clearing/transferring a claim. Awaiting merge is not fixed on master. A stale timestamp, closed PR or historical narrative is not permission to duplicate work.
