# X4 / T5S3 installed Reader entry integration

## Current performance integration refresh (2026-10-06)

PR350 advanced independently to cbf44781 by merging performance PR433.
This branch retains its prior green head and includes cbf44781 as the new
integration parent, preserving all twelve consolidated performance repairs.
Firmware 1.3.150 remains above the new base 1.3.147; no separate package
version or payload is changed by conflict resolution. Overlapping runner and
source edits retain both sets of changes.
Final software checks and exact-head target CI are recorded on the PR.
No write to PR350/master, release or device operation is performed. Earlier
sections below preserve the original repair and refresh evidence.

## Current X4-base refresh (2026-10-06)

The existing PR419 branch now includes X4 source b91bc323 and retains original
entry head 11daeb9d as its first parent. The only textual conflict was the
firmware reservation: 1.3.133 / upstream 1.3.143 advances to 1.3.150. Original
entry production files and tests are preserved; the native-app runner also
retains the upstream EPUB anchor-read regressions. Application/default remains
1.0.0 with minimum firmware 1.3.90; no package payload changed in this refresh.
Focused production entry/boot/activity/navigation/lifetime regressions pass.
Aggregate and exact-head hosted results are recorded on the PR. Hardware is
unavailable and unrun; it is not a prerequisite for this software integration.
PR350 and master are not changed. The sections below preserve prior provenance.

## Source and scope

Integrates existing entry f5eda3ebffc98446fd5360d63a7c64e7f7963b9e with X4
36891e71ab651152c618d60d1d248abb4a7f44ff. Both contain master 21ce3b5b.
Targets the X4 working branch without rewriting PR #350 or PR #386/#416/#417/#418.
Firmware advances from X4 1.3.128 / entry 1.3.130 to 1.3.133, above separate
performance reservations 1.3.131 and 1.3.132. Unchanged application/default 1.0.0
retains firmware API minimum 1.3.90. No driver/app payload, board profile, SDK
ABI, minimal-runtime source or Watch release pin changes.

This is callback-backed Reader control-flow entry, not complete UI extraction.
Installed default.elf owns the cooperative pump; the existing firmware-owned
Home, book renderer, ActivityManager and persisted state remain. Missing,
invalid or inadmissible entry falls back once. Custom selectors retain ordinary
launch. T5 recovery/panic destinations bypass entry; X4 boot failure cannot
admit a default ELF.

## Integrated control flow

- Both boards capture the same Home/saved-book choice in prepareReaderApplication.
  The first pump calls startReaderApplication once, retaining the saved-book
  boot-loop counter and state clear.
- X4 separates completed boot prerequisites from successful Reader presentation.
  Setup keeps its provider/storage/display/clock/touch/battery sequence and arms
  the common entry without creating another Home. The first pump schedules the
  original Home/book and requests its checked initial render. The ordinary loop
  still waits for actual successful presentation before consuming input. A prior
  static splash cannot qualify the Reader frame.
- T5 keeps its startup animation; X4 keeps its shared static logo. Desk-clock user
  wakes retain no-splash behavior and saved-book choice. Existing diagnostic
  scheduled/present logs and first-failure records remain. No failed provider
  acquisition retries are added.
- Pointer/generation handoff unloads default.elf before child apps, settings,
  keyboard/file workflows or sleep. Current X4 touch epochs and first-frame
  fences stay in the real ActivityManager/native-app paths.
- The idle-sleep latch survives the mapped Reader's deferred sleep request.
  Only the post-unload callback consumes it after sleep returns/refuses. Normal
  fallback sleep retains its original behavior. Ordinary child launch guards
  still prevent another app mapping while sleep is pending.
- Child Home/navigation results survive Reader readmission. Failed/unproven
  teardown retains the loader barrier and child owner stack/session/admission/
  RenderLock instead of dispatching into freed code.

## Verification

Focused production tests compile both board variants of boot callback/dispatch:
Home, saved book, desk wake, missing/invalid/explicit/custom selectors, recovery,
display failure, retained loader and X4 boot refusal. Real X4 gate and main-loop
sleep tests cover prior splash exclusion, pending/failed/successful first frame,
and mapped/unmapped sleep-latch ordering.

Existing touch/input, ActivityManager, Wi-Fi workflow, sleep, boot and battery
regressions retain their assertions while following the common entry. Wi-Fi
also checks deferred entry leaves launch flags/resolution untouched until
handoff. Hardware/RTOS/ELF boundaries remain modeled; no physical UI, latency
or power qualification is claimed.

Local full NativeApp, Springboard/package, idle/input and desk-clock results and
the changed-package version guard are recorded in the PR. Exact hosted CI is
tracked against the published head. ASan/UBSan are used; local LeakSanitizer is
disabled because the executor runs under ptrace. PlatformIO is unavailable
locally; hosted builds must establish target linking.

No working/default-branch merge, release, flash, install, physical test or
provisioning transaction is part of this change.
