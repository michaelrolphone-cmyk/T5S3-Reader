# Existing Reader ELF entry implementation

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
Sleep/power-off also execute after unload. Failed Reader unload retains the loader
barrier and prevents pending work or a new invocation. No second owner task or
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
