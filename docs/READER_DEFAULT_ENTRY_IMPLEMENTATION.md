# Existing Reader ELF entry implementation

The current X4/T5 integration is documented in
[X4 Reader entry integration](X4_READER_DEFAULT_ENTRY.md). The sections below
retain the original entry and prior current-master checkpoint provenance.

Base: `cff6ef0c11b79634dfa2c688d880df393d8c153c` on `master`.
Branch: `feat/existing-reader-default-elf`.

The existing Reader UI now has `Apps/default.c` / `default.elf` as its real
cooperative control-flow entry. Its only import is the versioned firmware Reader
callback. No replacement Home, renderer, theme, pins, recents or storage model is
introduced. Paper boot resolves the installed ordinary package when the selector
is absent or explicitly names `default.elf`. The existing Home/book-resume choice
is entered by its first pump. Recovery and failed/absent package fallback remain
available; CAM entry behavior is unchanged.

The same owner task admits an invocation without a native drawing session or a
held RenderLock. Existing launch-bearing activities yield before mutating their
launch flags. Pointer/generation checks resume their exact loop after ELF unload;
existing synchronous child results and nested parent continuation remain intact.
Sleep/power-off also execute after unload. Failed Reader or child unload retains
the loader barrier and prevents pending work or a new invocation. Uncertain child
quiescence parks the owner stack before session/context/admission cleanup, retaining
the RenderLock and yielding until manual reboot. Reader readmission preserves
child Home/queued-navigation results for suspended Springboard workflows. No second owner task or
execution-context swap is used.

Versions: firmware `1.3.82 -> 1.3.90`, above open PR385's reserved `1.3.88`;
new `application/default` package `1.0.0`, requiring new firmware API `1.3.90`.
No existing app/driver/provider package payload is changed.

## Checks

- Production `default.c` and coordinator host tests: version/size/null API checks,
  repeated invocation, owner-task and recursive-pump rejection, unload-before-child,
  startup handoff, sleep return, load/unload/resume errors and retained failure.
- Production paper dispatch and original Home/book callback: default/explicit/custom,
  invalid/missing, saved book, desk wake, display failure and recovery routes.
- Eight actual production activity loop bodies and actual ActivityManager guards:
  synchronous child/parent chains, cancel/error result preservation, pre-mutation
  flags, stale pointer/generation/navigation rejection and exactly-once dispatch.
  ASan/UBSan pass locally with `ASAN_OPTIONS=detect_leaks=0`; LeakSanitizer cannot
  run under this workspace's ptrace. Hosted runner keeps its default sanitizer.
- Existing real loader tests plus Reader-specific hardware-takeover denial and
  retained-unload/nested-launch barrier checks pass.
- Actual Xtensa `default.elf` builds and structural/import validation passes;
  ordinary package/ZIP/sidecar/catalog identity and version export pass. Local
  app compiler is Espressif's available Xtensa toolchain; hosted app builds verify
  the firmware-pinned toolchain.
- Local full firmware attempt is blocked before compile: PlatformIO toolchain
  mirror checksum mismatch, then bounded 180-second timeout. No checksum override.
- The full existing native-app aggregate passes locally. The Springboard/package
  aggregate exposed a source-contract test that still located the public launcher
  wrapper; it now verifies admission/context ordering in both real implementation
  paths. Re-run results are recorded in the PR.
- Hosted firmware/native-app/aggregate checks must be read from the exact PR head.
  No hardware test, install, flash, merge or release is claimed.

## Current-master continuation (2026-10-05)

The existing entry branch is refreshed with current `master`
`21ce3b5b720e106815c8f3a7778bc7003294e0e2`, retaining the original
`3f56936cf19261611493b0de6288fba8cf06a5f8` entry implementation and all
subsequent upstream fixes. The only textual conflict with master was the
firmware version. Firmware advances to `1.3.130`, above the independent
X4 TXT-index branch's `1.3.129`; the unchanged default application stays
`1.0.0` and its API minimum remains `1.3.90`.

This restores a mergeable current-master launch candidate; it does not claim
that the separate X4 branch already contains the entry. The inspected X4
working head is `36891e71ab651152c618d60d1d248abb4a7f44ff` (PR350).
Integration there must preserve both versions of `main.cpp`,
`ActivityManager.cpp`, `NativeAppHost.cpp` and the boot animation regression:
X4's shared boot/provider lifecycle and touch fences must survive alongside
the Reader handoff, exact activity generation, retained session and
navigation-resume guarantees. These are real source integration conflicts,
not missing device qualification or a request to redesign the GUI.

No change is made to X4 battery/time/minute progression, the independent
TXT-index optimization in PR416, board drivers, the minimal RiscRTE runtime,
Watch release pins or provisioning. The existing ELF calls the firmware's
Reader loop; extracting the complete UI into a freestanding ELF remains
outside this bounded integration refresh.

Current-tree local checks: real entry/coordinator/activity/boot/retained-session
and navigation suite; full Springboard/package aggregate; boot animation and
desk-clock wake contracts; changed-package version guard and whitespace pass.
The activity regression uses ASan/UBSan with `detect_leaks=0` because
LeakSanitizer cannot run under this executor's ptrace; leak coverage is not
claimed. Full NativeApp aggregate and exact-head hosted firmware checks are
reported separately in the PR. PlatformIO is not installed in this executor;
no local target link or physical test is claimed.
