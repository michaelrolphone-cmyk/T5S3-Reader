# Bug repair progress

Snapshot: 2026-10-01 02:08 UTC. This record supersedes the pre-merge restoration snapshot.

## Completed hourly run — native confirmation safety

- Last owner: `fix_reachable_confirmation_bug`; active ledger owner: **none**; completed 2026-10-01 02:08 UTC; claim released after terminal exact-head CI
- Canonical bug: **249**, **Awaiting merge**, two related app confirmation fixes; branch `fix/native-confirmation-249` from master `3300229d0a232b4e6047a7c93b2f518c033c3cfa`
- Ledger parent captured: `fc72ad0dba3d207e44c83d0ee76738a9a017377d`; draft #332 still open. No prior active owner
- Open #96/#194/#220/#277/#333 checked: no edits to `Apps/ota_update.c`, `Apps/clear_cache.c` or their manifests. Aggregate test runner may require additive integration reconciliation
- #246 touches the separate legacy ConfirmationActivity also modified by #220; excluded from this batch. #333 remains held with review withdrawn; draft #334 occupies the second and final outstanding code-batch slot
- Source reachability: NativeSettingsBridge System actions → ClearCacheActivity/OtaUpdateActivity → installed native app → NativeAppHost raw tap → NativeUiBridge row hit test. PRO uses installed touch provider, not legacy X4 front buttons. Requires installed compatible apps and working touch; OTA needs network/new firmware. Owner device installation/use is not claimed
- Published index `e495c5e1` and current master both have clear_cache 1.0.0 and ota_update 1.0.0; completed app-only bump to 1.0.1 each. Existing UI event ABI suffices; no firmware bump
- Reconciled scan `66bc650056964dcba8e5164d6d76f6c45cdb81ab` by affected function/trigger/failure: 249 native confirmation differs from 246; 250 terminal FLASH_END loss differs from earlier flashing reports; 251 skipped-role/incomplete validation differs from 16 persistence rollback and 17 labels. IDs 249–251 preserve scan provenance; 250/251 remain unclaimed/unvalidated
- Current inventory: **240 canonical reports, 238 outstanding** (249 awaiting merge; 16/17 on hold; 235 need revalidation); 91/205 fixed on master. Next unused ID **252**. Historical counts below describe the earlier snapshot
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
