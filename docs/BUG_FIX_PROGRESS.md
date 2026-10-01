# Bug repair progress

Snapshot: 2026-10-01 04:30 UTC. The completion record below supersedes historical held/open statuses and batch counts; older evidence and applicability history are retained.


## Active Timecard revalidation and repair

- Owner: `01a0f728-f5cb-70e7-9297-7a6abc7c6034` (isolated Mac task-3); started 2026-10-01. Canonical candidate 14, matched by Apps/timecard.c punch_today/today/now_minutes, not numeric alias alone. Previous ledger owner is idle/completed; remote ledger has no active claim. Earlier duplicate tasks remain paused under fresh user direction to find and fix bugs.
- Baseline master `8157c0bb8bbb94c52229359ecef876dba046726a`; preserved repair `fix/timecard-clock-failure` at `97fd307c8636fd42a57b2c99b321455eacd0eb5d` will be reused if applicable. No duplicate Timecard PR found. Phase: revalidate actual local_datetime provider and workflow; no hardware claim.
- Scope: one Timecard clock-snapshot/failure batch. No archive, U1, Hollow or architecture implementation. PR339 is now merged on baseline. No master/release/catalog/dispatch/flash writes. Claim remains active until a documented checkpoint.

## Completed merge reconciliation — PR #333 and PR #334

- Coordinator `record_merged_bug_batches`; active ledger owner **none**. Previous ledger worker confirmed stopped by the parent; latest durable ledger records had no active claim. Captured parent `0b445caa34a0cc87fdf0a781c1ab88cf835ed1b7`; #332 remains the sole open draft coordination PR. This documentation-only run claims no implementation work; prior #333/#334 claims remain released and the reconciliation is terminal
- Owner merged **#334 / canonical #249** at **2026-10-01 04:16:44 UTC**, merge `e6d687733671c224768219ce52a5182fcf83795b`, second parent/head `98943199054e409987a957b9b909f32ccd4c9f49`. Owner merged **#333 / canonical #16/#17** at **04:17:30 UTC**, merge `f79f72911ccb1dbd0666117370f54e040b5fc8e4`, second parent/head `3619048c6b178ddf6d767842b797d6fb1d7a87d4`. GitHub PR state and merge timestamps independently refreshed; both merges are ancestors of checked current master `f79f72911ccb1dbd0666117370f54e040b5fc8e4`
- **#16/#17/#249: Fixed on master.** Inspected rollback of all four live fields on save failure and shared reset path, persistent side-button hints with blank front hints, and explicit `T5_UI_EVENT_CONFIRM` authorization in both destructive app loops. These source files and focused regressions are unchanged from their respective merged heads; all three app manifests are 1.0.1
- **Fresh local PASS on that master, macOS, without sanitizers:** existing `button_remap_runtime_test.py` covers validation, 576 mapping transitions, repeated failed apply/reset rollback, 576 live-input retries, cancel/duplicate/touch/exit cleanup and chrome labels; existing `confirmation_input_test.py` passes **2,788 cases** using actual apps and extracted production UI functions. Executed in isolated checkout with in-memory runner adaptation only: removed sanitizer flags and Linux PIE flags; no tracked source/test edits or global tool changes
- **Local sanitizer limitation:** unmodified remap runner initially fails because Apple Clang rejects unused `-no-pie` under `-Werror`; suppressing only that diagnostic permits compilation, but its sanitizer executable times out at 30 seconds without assertion output. Confirmation sanitizer executable also times out at 30 seconds; a longer bounded confirmation retry also timed out at 180 seconds without assertion output. These are not sanitizer passes. Leak detection was disabled. No fresh full aggregate, target firmware/app build, physical touch/button test, storage durability test, firmware write, installation or hardware qualification was performed
- **Prior exact-head CI reverified, not rerun:** [36799152027](https://github.com/michaelrolphone-cmyk/T5S3-Reader/actions/runs/36799152027) on `3619048c6b178ddf6d767842b797d6fb1d7a87d4` and [36803968828](https://github.com/michaelrolphone-cmyk/T5S3-Reader/actions/runs/36803968828) on `98943199054e409987a957b9b909f32ccd4c9f49` are completed/success, including host parser tests and both t5s3-pro/lilygo-epd47-s3 build jobs. Their historical baseline-failure, sanitizer and aggregate evidence remains below; it is not new local execution evidence
- Applicability remains limited: Button Remap assumes inherited X4 front/side inputs; merge does not establish a T5S3 PRO user path or resolve the separate unsupported Settings-entry candidate. #249 needs installed compatible apps/provider (and OTA network/new release); owner installation/use remains unverified. No broader fix is claimed
- Inventory: **249 retained reports / 244 outstanding**: 243 need revalidation, #252 host-fault confirmed with storage/device validation pending; **#16/#17/#91/#205/#249 fixed**. Next ID **261**, unchanged. These two code batches now occupy **zero** outstanding slots; no new batch is selected or authorized by this reconciliation
- Preservation checks: 249 unique canonical IDs; all 246 unrelated entries byte-identical; original affected-report bodies, source aliases, applicability corrections and superseded status text retained. Only `bugs.md` and this progress document change. Older sections below are historical snapshots, including their held/open instructions and counts. Closed #333/#334 are untouched; no master write, merge, release, catalog, deployment or flashing
- Next action: future work must refresh remote master, open PRs and durable ownership before selecting anything. Timecard/Hollow scan reconciliation, archive experiments and new implementation remain separate work

## Completed ledger-only reconciliation — 20:50 font/touch scan

- Coordinator `reconcile_archive_scan_candidates`; active ledger owner **none**. Refreshed open draft #332 and captured remote ledger parent `43812a57260d494d79d1d35e4fdfda4fb575797e`; no active claim. Master `a5e2db59077cc889079668dc9cd7428b08bc32a1` inspected; affected files identical on checked U1 `e377948b29e83b85a09995fb415045d671e6397d`
- Scan `21ba2700c7b262c966765600941dc3b1fcb0fba7` (`automation/bug-scan-20260930-2050`) reused local 255–257, mapping now **255 → 258**, **256 → 259**, **257 → 260**. Archive canonical 255–257 and every previous report remain unchanged. Original source bodies/status assertions retained as historical evidence, not adopted confirmation
- #258 is missing/wrong-type top-level font families accepted as empty success, distinct from #76 partial-prefix publication on later error and #174 zero-file family installation. #259 duplicate-name storage identity collision is distinct from #77 failed single-family update and #78 truncated paths. #260 firmware touch teardown destroys retry handles after failure, distinct from #85 takeover rollback, #135 app setup leak and canonical bug #96 text-input cleanup
- All three remain **unclaimed candidates needing revalidation**. No host, target, actual remote-catalog, storage mutation or device reproduction. #260 unsubscribe failure alone does not prove surviving resources after subsequent successful release; quarantine availability and actual resource leakage require separate fault cases. The earlier worker-stop failure retains handles and is a different path
- U1 owner notified and confirms no current NativeTouchInput change/known fix. Refreshed open PR changed paths #96/#194/#220/#277/#333/#334 have no affected font/touch-file edits; coordinate provider-lifetime integration before repair. Both code slots remain held by #333/#334; no third batch or implementation claim
- Inventory: **249 canonical reports / 247 outstanding**: 243 need revalidation, #252 host-fault confirmed with storage/device validation pending, #16/#17 held, #249 awaiting merge; #91/#205 fixed. Next unused ID **261**. Earlier inventories below are historical
- Documentation verification: 249 unique IDs, all 246 prior report bodies preserved, all three new original bodies preserved, immutable aliases, whitespace, captured-parent non-force publication and exact remote-content comparison. Only bugs/progress documentation changed; no source/version edits, merge, release or flash

## Completed ledger-only reconciliation — 20:22 archive scan

- Coordinator `reconcile_archive_scan_candidates`; active ledger owner **none**. Captured ledger parent `cff31d4596f61c23aa34ce266fa0d6afe97e501e`; no active claim. Open draft #332 remains the sole coordination PR. Master `a5e2db59077cc889079668dc9cd7428b08bc32a1` and U1 `e377948b29e83b85a09995fb415045d671e6397d` inspected
- Immutable scan `3bb0ef3bbc67deb6e9b23ea1319d740362e539ee` (`automation/bug-scan-20260930-2022`) adds scan-local **255 → canonical 255**, **256 → 256**, **257 → 257**. Preserved every prior report and each new original report body/verdict as historical evidence. Scanner “Verified” wording is not fresh host/device confirmation
- Three distinct, unclaimed **needs-revalidation** candidates: signed negative ZIP streaming reads converted to unsigned lengths; alignment-unsafe EOCD loads; bridge rejecting valid empty entries. Compared with #49/#56/#125/#143/#167/#175; no identical root cause/trigger found. Detailed qualifications and source locations are in each report
- #255 caveat: NativeArchiveBridge's bounded sink rejects oversized stored writes when the length exceeds remaining maxSize; do not assume a crash in every downstream consumer. #256 alignment is scan-buffer-relative and a target crash remains unverified. #257 empty stored/deflated fixture behavior remains untested here
- U1 owner confirms its current nested-resource slice does not change these ZIP/bridge files and archive.zip service remains open. These candidates are inputs to future coordinated archive work, not evidence that current U1 changes fix them. Refreshed #96/#194/#220/#277/#333/#334 status/paths; no affected ZIP/bridge edits in their checked diffs
- **Two code-batch slots remain occupied:** held #333 head `3619048c6b178ddf6d767842b797d6fb1d7a87d4` and draft #334 head `98943199054e409987a957b9b909f32ccd4c9f49`. No third batch, implementation claim, runtime edits or version bump
- Inventory: **246 canonical reports / 244 outstanding**: 240 need revalidation, #252 host-fault confirmed with storage/device validation pending, #16/#17 held, #249 awaiting merge; #91/#205 fixed. Next unused ID **258**. Prior section counts below are historical snapshots
- Documentation checks: unique headings/IDs, original-body preservation, immutable aliases, exact captured-parent publication and remote content comparison. No runtime test, build, actual SD fault, target alignment test, hardware I/O, merge, release or flash. Next run must refresh branch/claims and coordinate archive ownership before revalidation or repair

## Completed revalidation-only run — resolver #252

- Coordinator `fix_reachable_confirmation_bug`; active ledger owner **none**. Refreshed master `a5e2db59077cc889079668dc9cd7428b08bc32a1`, ledger `637f18064645adfcd0e3c795a7d315f42046b103`, and open #332/#333/#334 before publication. #333 and #334 heads unchanged; both code slots occupied
- #252 is **host-fault confirmed, unclaimed** on current master and U1 `015ae5d42a0d7fc294f0675703b8f4cb51810cac`: actual resolver source accepts the first managed match if the iterator returns invalid before a second duplicate. Seven controls pass; explicit fail-closed assertion fails (134) on both sources under ASan/UBSan. SD iterator and package validation are fixtures; no real SD fault, duplicate installation, target build or hardware qualification claimed
- Source reachability traced through ordinary Home pinned-app launch and InstalledAppActivity; prerequisites and remaining storage/device uncertainty recorded in #252. No X4 physical-button assumption
- U1 owner notified with source SHAs/reproduction and confirmed overlapping resolver edit changes only the policy initializer. No duplicate patch, implementation claim or third batch. Lower-layer EOF/error distinction remains an open implementation question, not an established fix
- Inventory unchanged at **243 reports / 241 outstanding**: 237 need revalidation, 252 host-fault confirmed with storage/device validation pending, 16/17 held, 249 awaiting merge; 91/205 fixed. Next ID **255**
- Only `bugs.md` and this progress document updated; original scan alias/provenance retained. No runtime/version/architecture edits, merge, release or flash. Future repair must coordinate with active U1 and available batch capacity

## Completed ledger-only reconciliation — 19:21 scan

- Coordinator: `fix_reachable_confirmation_bug`; active ledger owner: **none**. Read master `3300229d0a232b4e6047a7c93b2f518c033c3cfa`, ledger parent `06ce4a38f19ece22f6a1ce40be58283436d4720d` and open draft #332 before this atomic documentation update; no prior active claim
- Reconciled immutable scan `5a4a4653ce37b932f1485bb8cd9a5d4dda8dce87` from `automation/bug-scan-20260930-1921`: local **249 → canonical 252**, **250 → 253**, **251 → 254**. Existing 249–251 unchanged
- 252: installed-app resolver accepts provisional uniqueness after iterator failure; distinct from #24 provider count cutoff, #227 partial live inventory publication, and #129 resolver capacity exhaustion. Similar shared iterator error/EOF limitation is cross-referenced without collapsing distinct consumers/failures
- 253: combined I2C per-phase timeout reuse; 254: multi-device USB class-rejection cache represented by one token. Source patterns inspected on current master; these are **unclaimed candidates needing revalidation**, not new runtime or hardware findings. #254 overlaps NativeUsbBridge under active U1 #96 and must be reconciled before any repair
- Earlier reconciliation inventory: **243 canonical reports, 241 outstanding**: 238 need revalidation, 16/17 held, 249 awaiting merge; 91/205 fixed. Next unused ID **255**
- **Two code-batch slots remain occupied** by #333 and #334. No implementation claim or third batch was created. No runtime/build/version edits, target tests, hardware I/O, merge or release for this documentation-only reconciliation
- Verification: canonical headings/unique IDs, immutable source aliases, preserved existing report bodies, current-source/dedup notes, captured-parent non-force publication and remote content verification. Keep using open #332; next run must refresh claims and PR state

## Completed hourly run — native confirmation safety

- Last owner: `fix_reachable_confirmation_bug`; active ledger owner: **none**; completed 2026-10-01 02:08 UTC; claim released after terminal exact-head CI
- Canonical bug: **249**, **Awaiting merge**, two related app confirmation fixes; branch `fix/native-confirmation-249` from master `3300229d0a232b4e6047a7c93b2f518c033c3cfa`
- Ledger parent captured: `fc72ad0dba3d207e44c83d0ee76738a9a017377d`; draft #332 still open. No prior active owner
- Open #96/#194/#220/#277/#333 checked: no edits to `Apps/ota_update.c`, `Apps/clear_cache.c` or their manifests. Aggregate test runner may require additive integration reconciliation
- #246 touches the separate legacy ConfirmationActivity also modified by #220; excluded from this batch. #333 remains held with review withdrawn; draft #334 occupies the second and final outstanding code-batch slot
- Source reachability: NativeSettingsBridge System actions → ClearCacheActivity/OtaUpdateActivity → installed native app → NativeAppHost raw tap → NativeUiBridge row hit test. PRO uses installed touch provider, not legacy X4 front buttons. Requires installed compatible apps and working touch; OTA needs network/new firmware. Owner device installation/use is not claimed
- Published index `e495c5e1` and current master both have clear_cache 1.0.0 and ota_update 1.0.0; completed app-only bump to 1.0.1 each. Existing UI event ABI suffices; no firmware bump
- Reconciled scan `66bc650056964dcba8e5164d6d76f6c45cdb81ab` by affected function/trigger/failure: 249 native confirmation differs from 246; 250 terminal FLASH_END loss differs from earlier flashing reports; 251 skipped-role/incomplete validation differs from 16 persistence rollback and 17 labels. IDs 249–251 preserve scan provenance; 250/251 remain unclaimed/unvalidated
- Earlier batch inventory: **240 canonical reports, 238 outstanding** (249 awaiting merge; 16/17 on hold; 235 need revalidation); 91/205 fixed on master. Next unused ID **252**. Historical counts below describe the earlier snapshot
- Draft [#334](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/334); exact remote head `98943199054e409987a957b9b909f32ccd4c9f49` verified by fetch/content comparison; active claim commit `2697c3faa5432b41d30fde2302a1872be49ac610`
- PASS: 2,788 ASan/UBSan cases linking actual C apps and verbatim production event/hit-test/rotation functions; baseline body-row case fails independently for each original app. All 24 mappings/four orientations, non-control taps, explicit touch/physical Confirm, held buttons, cancellation, exit/poll failure, service failure/reopen and missing event API covered. Rendering/input hardware/destructive services are fixtures
- PASS: full native aggregate, C syntax, shell syntax, whitespace. LeakSanitizer disabled for ptrace. Both local target app builds attempted and blocked by absent Xtensa compiler
- Initial CI 36803281830 passed both firmware-board/all-app builds and native aggregate, but old clear_cache/ota_update rendering fixtures lacked the required event callback. Those fixtures now supply explicit events without removing assertions; full springboard/app-manifest suite and focused regression pass locally
- **PASS:** final exact-head CI [36803968828](https://github.com/michaelrolphone-cmyk/T5S3-Reader/actions/runs/36803968828) completed successfully on `98943199054e409987a957b9b909f32ccd4c9f49`: host parser/native/springboard/release tests and both t5s3-pro/lilygo-epd47-s3 firmware jobs, including native ELFs and all released apps/manifests. Local compiler absence is covered by successful CI target builds; no physical-device qualification
- Both app manifests bumped to 1.0.1, unchanged minimum firmware. #333 rechecked open/draft with original head `3619048c6b178ddf6d767842b797d6fb1d7a87d4`; no review requested
- Next invocation: recheck #333/#334 and master ancestry. **Two outstanding code batches; do not start another while both occupy slots.** #249 is not fixed on master until a merge/source/regression check proves it. Continue using open ledger #332. #333 stays on hold; do not resume or request review without direction
- No live deletion, install, flash, merge or release. All work for this run is terminal; unmerged integration remains pending owner action

## Board applicability correction — review request withdrawn

**Prioritization is awaiting user direction. This is not a demonstrated T5S3 PRO user-path fix.** The request for the user to review this batch is withdrawn. PR #333 stays draft and its code is unchanged.

The tested workflow assumes the inherited Xteink X4 arrangement of four front buttons and two side buttons. On the user's LilyGO T5S3-4.7-e-paper-PRO, [HalGPIO::getState()](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/be82695ea0ecb14525c0de1ddc78cd0c77e4614b/lib/hal/HalGPIO.cpp#L29-L39) emits only the expander and power/BOOT inputs (indices 7 and 6), whereas [the remap indices](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/be82695ea0ecb14525c0de1ddc78cd0c77e4614b/lib/hal/HalGPIO.h#L55-L62) are 0–3 and its side actions use 4–5. The [original X4 README](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/b04806126c1ec2af967d4d4798d239ac6e5c6088/README.md) and [original remap activity](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/b04806126c1ec2af967d4d4798d239ac6e5c6088/src/activities/settings/ButtonRemapActivity.cpp) establish its legacy origin.

Host fault tests and exact-head CI genuinely pass, but they simulate those inputs and establish code/build behavior, not physical-control applicability or actual use on this device. External controller navigation is [combined separately](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/be82695ea0ecb14525c0de1ddc78cd0c77e4614b/src/MappedInputManager.cpp#L108-L122) and is not remapped by these four settings.

**Separate triage candidate:** the unsupported Settings → Controls → Remap Front Buttons entry is [inserted without a board/capability gate](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/be82695ea0ecb14525c0de1ddc78cd0c77e4614b/src/native/NativeSettingsBridge.cpp#L85-L91). It [launches the ELF or asks the user to install it](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/be82695ea0ecb14525c0de1ddc78cd0c77e4614b/src/activities/settings/ButtonRemapActivity.cpp#L18-L59), and button_remap 1.0.0 is present in the [published catalog snapshot](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/7f07d124dc20435618ae32bb2f89e0f9595d6d9a/release-index.json). This needs separate applicability/reachability and duplicate triage; no new canonical bug ID or implementation claim is assigned here. Do not expand #333 into a gating or architecture refactor. Installation/use on the user's device has not been established.

## Historical completed hourly run — Button Remap

- Run: 2026-10-01 00:50 UTC; last coordinator: `fix_button_remap_batch`; active ledger owner: **none**
- State: completed; repair published and exact-head CI passed. Active implementation claim released
- Canonical bugs: **16 and 17**, **On hold: prioritization awaiting direction; review request withdrawn**
- Baseline master: `be82695ea0ecb14525c0de1ddc78cd0c77e4614b`
- Durable claim published before implementation: `08ba173adc72216635f07f058fda0068561cf287`
- Coordination branch: `automation/bug-ledger`; draft [#332](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/332), replacing merged #327
- Repair branch: `fix/button-remap-16-17`; draft [#333](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/333), targeting master
- Verified remote head: `3619048c6b178ddf6d767842b797d6fb1d7a87d4`
- Outstanding code batches from this pipeline: **one**. #205 is merged and does not occupy a slot
- Next invocation: recheck #333/head/merge status before selecting more work; do not duplicate 16/17. One draft batch remains on hold pending direction; count it against the two-batch limit. Do not request its review again or continue it without direction. Select future candidates only after checking relevance to the user's actual T5S3 PRO paths as well as revalidation and overlap. Continue using coordination PR #332 while it remains open

## Button Remap evidence

Current-master source still mutated all four live mapping fields before save without rollback, including reset defaults. The app still advertised Reset/Cancel on front Left/Right but handled side Up/Down. Open PR paths #331, #277, #220, #194 and #96 were checked: none changes the native remap bridge or app. #220 changes the separate legacy activity, UI API and firmware version; those code paths are not edited here. The shared firmware version change is necessary for this firmware-owned bridge.

The fix restores the old four fields before returning failed save, preserving original physical-button decoding for retry; Reset uses the same path. Persistent side-button instructions replace the misleading front-button hints.

- Button Remap: **1.0.0 → 1.0.1**; firmware: **1.3.47 → 1.3.48**. Old versions verified in the published release-index and tags. No new ABI or minimum-firmware requirement
- Actual C app and C++ bridge linked into a host regression with persistence/input/UI stubs
- **PASS:** ASan/UBSan regression, 576 mapping transitions and 576 physical-input retry transitions, repeated failed saves, failed reset/reopen/retry, cancel at each stage and after save failure, API/null/duplicate/out-of-range validation, touch selection, exit/poll cleanup
- **Expected FAIL on baseline:** separate bridge case aborts on retained live-mapping assertion; chrome case aborts on misleading front previous-label assertion
- **PASS:** full `ASAN_OPTIONS=detect_leaks=0 bash test/run_native_app_test.sh` after test integration, app C syntax, shell syntax and whitespace checks
- Local `python3 scripts/build_all_apps.py --id button_remap` attempted but blocked by missing Xtensa compiler
- **PASS:** [exact-head CI run 36799152027](https://github.com/michaelrolphone-cmyk/T5S3-Reader/actions/runs/36799152027) on `3619048c6b178ddf6d767842b797d6fb1d7a87d4`: host parser tests, t5s3-pro and lilygo-epd47-s3 all completed successfully. Both board jobs built and validated native ELFs and all released apps/manifests, including the updated Button Remap app. Local compiler absence did not block CI verification
- LeakSanitizer disabled under container ptrace. The persistence stub models failed atomic storage; these tests do not establish actual filesystem durability
- No hardware qualification, merge, release, deployment or flash

This unattended run successfully created a branch, trees/commits, non-force ref updates and draft PRs through the connected GitHub tools. Remote fetch/hash and working-tree comparisons verified publication. This proves this run's writes, not permissions for future runs.

## Resolved on current master

- **205:** [#326](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/326) merged as `f4952a31bd0f9ae5c8051b80b088e16426308b88` at 2026-10-01 00:02:16 UTC. Merge ancestry and compiled USB-handle runtime regression reverified on the baseline. File Browser version 1.3.1. Its earlier native aggregate and [exact-head CI 36793336681](https://github.com/michaelrolphone-cmyk/T5S3-Reader/actions/runs/36793336681) passed all three jobs, including both firmware boards and app validation
- **91:** [#283](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/283), merge `36be2ee496468feb3779616e69f61e3a671c192b`, is an ancestor. GT911 retains READY after contact-read failures; three failures followed by exactly-once delivery pass again in `test/run_gt911_touch_test.sh`
- Coordination [#327](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/327) merged as `2a55d0a05316a3fe5478011ada8285fcb24c1885` at 00:04:41 UTC. Neither closed PR was edited
- These fresh regressions used `ASAN_OPTIONS=detect_leaks=0`; no leak detection or device qualification is claimed

## Preserved inventory and provenance

237 retained canonical reports; **235 outstanding**: 233 need revalidation and 16/17 are on hold pending prioritization direction. 91 and 205 are resolved. The next unused canonical ID is **249**.

The initial restoration retained all 196 master reports and reconciled 42 later source reports from 14 scan branches into 41 distinct IDs **208–248**. Immutable source aliases and original evidence remain in `bugs.md`. The 08:22 #209 / 15:20 #208 duplicate maps to canonical **215**. The distinct 16:24 and 17:22 reports retain IDs 243–245 and 246–248. Older bugs **4, 6–12** were already merged through [#246](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/246); do not reopen them from old scan/fix branches.

## Handoff rules

Follow [BUG_FIX_WORKFLOW.md](BUG_FIX_WORKFLOW.md). One ledger writer, captured-parent non-force updates, at most two outstanding code batches, current-master revalidation and durable claims before source changes. Record latest remote head, results, unfinished work, owner and next action before clearing/transferring a claim. Awaiting merge is not fixed on master. A stale timestamp, closed PR or historical narrative is not permission to duplicate work.
