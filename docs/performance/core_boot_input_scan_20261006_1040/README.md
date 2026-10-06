# Core boot, touch and menu scan, 2026-10-06 10:40 UTC

**Result: zero newly confirmed substantive defects.** This report records a bounded read-only scan and its checks. It neither explains the owner's severe latency symptom nor establishes that the device is healthy.

Checkpoint alias: `michaelrolphone-cmyk/T5S3-Reader::SCAN-20261006-1040-CORE-LATENCY`. This is a scan checkpoint, not a bug ID. No canonical ID, implementation claim, version reservation or repair PR is assigned.

## Exact baseline and integration

- Repository ID 1367546328; default branch `master`, freshly verified at `c765a9931e2f1772d1dd3b5870362c18a08a9e57`.
- Scanned source: [PR350](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/350), `xteink-x4-pro-boot`, `1dab6634a9b01b3da4aee076f57da1f4ee1dd3f2`, firmware 1.3.157. The PR is open/unmerged.
- Ancestry checks establish that master is now an ancestor of this source, with zero master-only and 223 PR350-only commits. The reverse ancestry check fails: PR350 has **not** been integrated into master. This corrects the older scans' now-obsolete 31-commits-behind description without changing their original evidence.
- Closed-unmerged PR348 head `108daf2050065076346ae016b3b030b1e7ec3e2a` is ancestral to PR350. Its absorbed branch is not an independent master merge.
- The authentic 19-provider navigation cursor/capacity repair and final PR350 checks remain with `trace_basic_reader_latency`, [claim6012329134](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/332#issuecomment-6012329134). Its integration checkpoint is [6014308781](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/332#issuecomment-6014308781). This scan did not rerun that integration fixture, take the claim, or operate its CI.
- Canonical bugs/progress/workflow were read at sole-coordinator branch `automation/bug-ledger`, `101e2f7f58a8daabbccd3227ad878105e3847320`. Publication uses an isolated documentation checkpoint plus the existing PR332 reconciliation queue; shared ledger files are untouched.

## Inspected production paths and useful negative results

The scan traced setup → reader-state/input-health → mapped input → provider admission, normal mapped input → ActivityManager menu dispatch, and NativeAppHost's launch/admission/poll/return paths. It inspected the touch capture/gesture consumer, foreground navigation bridge, USB HID/keyboard/gamepad/XInput/navigation drivers, interrupt controller, and application admission/launcher. No image, model or book-parser investigation was undertaken.

1. **Quiet USB interrupt reads do not impose the nominal 10 ms caller deadline as a forced wait.** The production `PendingInterrupt.h` retains controller-owned DMA between polls and uses `pump(0)` for ready work. Its existing host regression executes 50 quiet reads with one submitted transfer, zero halt/free operations and zero artificial clock ticks, then checks independent endpoints, already-completed reports, rearming, STALL recovery and retained pending DMA on failed drain. The same unchanged body passes normal and fatal ASan/UBSan execution. This is a deterministic negative control against a per-idle-endpoint wait hypothesis, not a physical timing measurement or general transport-fault proof.

2. **Serviced-render exclusion is an intentional framebuffer-ownership boundary.** `NativeAppHost::current()` rejects ordinary UI polling while the refresh worker owns presentation. The actual Home/modal regression proves queued Home events remain unconsumed until presentation ends, then operate normally; Text Editor's direct keyboard collector remains serviced separately and its ordered-typing-during-refresh test passes. The exclusion was not promoted to a new missed-input defect.

3. **Navigation's ordinary admission/retry/focus/suspend paths pass the focused source test.** The existing test exercises acquisition, retry, polling, copied focus, disable/resume and sleep lifetime; the standalone normal and fatal ASan/UBSan runs agree. This does not replace the authentic package-registration test or target CI owned by the active repair.

4. **No new app-loading or return defect was established.** The scan checked managed/loose admission, default-reader handoff and native-app return. It retained package validation, coherent generation snapshots, retained-failure ownership, presentation fences and the existing release/quiet-input guard as required behavior. It did not turn a deliberate settling interval into a new performance bug or infer target latency from a source constant.

## Existing findings explicitly retained, not recounted

- FTDI/legacy serial-provider reload: [6005930052](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/332#issuecomment-6005930052).
- Completed keyboard SET_PROTOCOL failure retry: [6014124369](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/332#issuecomment-6014124369), immutable report commit `408516b6b7c853b9c4cab437549bfca43726cf09`.
- SdVfs stale-slot reuse witness: [6014402401](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/332#issuecomment-6014402401), alias `SDVFS-20261006-STALE-SLOT-REUSE`. Its separate full reconciliation remains pending; this scan does not claim to complete that revalidation.
- Existing pathname amplification, cold bootstrap scheduler waits, and the earlier 64-tick empty-allocation cleanup observation remain unchanged. None is presented as a new finding or a proven explanation for multi-minute latency.

## Census and verification

Snapshot: 395 branches, 436 all-state PRs, 588 distinct heads, 197 PR332 comments through 6014402401, zero non-PR issues. All selected trees were available. The source census records 16 files and their distinct variants, including 65 NativeAppHost variants, 37 launcher variants, 10 touch-consumer variants, 9 navigation-consumer variants and 3 persistent-interrupt variants. A current-plus-historical union contains 262 report/ledger Markdown blobs, all readable. Source hashes and the compact immutable head/blob census accompany this report. Fresh PR350/master reads before publication still matched the baseline above.

Passed:

- Existing `test/run_usb_hid_test.sh`: all 13 output groups, including class/keyboard/gamepad, descriptor layouts, bounded XInput retries/hotplug, semantic text, unplug-free cleanup, Text Editor core/UI, persistent interrupt reads, role switching, navigation/focus and native text input. This script's default builds are **not sanitizer builds**.
- `test/native_apps/global_home_overlay_behavior_test.py`: production Home/modal timing, delayed captured events, 400/401 ms boundary, presentation exclusion, frame restoration, takeover and shutdown behavior, under ASan/UBSan with leak detection disabled.
- Separate fatal ASan/UBSan builds of `controller_interrupt_test.cpp` and `native_navigation_input_test.cpp`.

Audit: the first Home/modal invocation used the test's default sanitizer configuration and ended at the environment's explicit `LeakSanitizer does not work under ptrace` error. The unchanged test was rerun successfully with `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1` and `UBSAN_OPTIONS=halt_on_error=1`. Its pre-existing misleading-indentation warning remains in both logs. No denied action was bypassed; LeakSanitizer coverage remains unavailable. Both the original failure log and successful rerun are preserved.

These are host/source checks with physical transport, clock and display fixtures. No target ELF execution, hardware measurement, USB-device operation, firmware build, package installation or device qualification is claimed. No source implementation, master/PR350/working-branch/shared-ledger write, merge, release, deployment, flash or Codex task was performed. The only publication is this documentation/evidence checkpoint and PR332 reconciliation notice.
