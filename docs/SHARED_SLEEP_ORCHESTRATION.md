# Shared terminal sleep preparation

This source-only step extracts the existing terminal-preparation sequence from
the three Reader entrypoints in `src/main.cpp` into
`src/runtime/power/SleepOrchestration.{h,cpp}`. It does not introduce product sleep
policy, a new driver capability, rail control, or a complete hardware-qualified
sleep implementation.

## Durable source reconciliation

The comparison source is Reader `9c8783d6f20659a514bfaeb3847a77c7213dc4f6`.
Its `src/main.cpp:277-375` contains ordinary sleep, preserved-screen sleep and
power-off sequences. `lib/hal/HalDisplay.cpp:589-600` and
`lib/hal/HalDisplayX4.cpp:82-97` already own checked display/storage teardown.
`lib/hal/HalStorageVolume.cpp:70-93` already delegates prepare/cancel/commit to
the external volume. `src/native/NativeBoardPowerPort.cpp` already reserves and
uses the installed `board.power.vbus` provider. These owners are unchanged.

Runtime main `13eca114cb58469cf6004fafc3da84fbd9d29beb` already exposes generic
owned-input native sleep and explicitly leaves application preparation and rail
policy outside the headless runtime. Drivers main
`a7ae39994603c4545a3c014f09051ecfc12084d2` already contains generic power
capabilities and transitional adapters with legitimate `board.battery` edges.
Neither repository needs edits for this slice.

## Responsibility and ordering

- Reader retains deferral, debug suppression, consumer suspension, settings/state
  persistence, sleep-screen rendering, backlight and tilt choices, desk-clock
  policy, CPU-frequency lock lifetime, and terminal GPIO sleep entry.
- Ordinary and preserved-screen sleep use the same preparation operation and
  provide no shutdown callbacks. They cannot reserve a new PMU grant.
- Explicit power-off reserves the existing owner before display/storage teardown.
  Failed reservation must cancel successfully and then drain successfully before
  continuing. Cancellation failure short-circuits drain, as before.
- Display refusal cancels an explicit shutdown reservation, then invokes the
  existing Reader recovery. It never issues a PMU request or terminal fallback.
- Only after successful display/storage teardown may the helper ask about hard
  power-off availability and request it. An accepted but returning request keeps
  the existing 1500 ms wait; a rejected request keeps the existing fallback.
- A successful preparation returns directly to the caller's terminal sleep path.
  Shutdown releases its CPU lock at the original point, disables touch wake, and
  never re-enters persistence, mounts, consumer suspension or graph preparation.

The operation tables are internal synchronous C++ callbacks, not a public ABI or
grant. They allocate nothing and retain no state. Missing callback tables fail
closed before side effects. Existing callbacks own physical rollback/retention;
the helper does not reinterpret their results or claim hardware recovery.

## Verification and limits

Run `bash test/run_sleep_orchestration_test.sh` and repeat with `SANITIZE=1`.
The production helper executes 64 boolean matrices for ordinary preparation,
shutdown and retry, plus nine missing-callback failures. The Reader differential
fixture compiles the actual three current entrypoints and the exact comparison
source, then compares 65,536 side-effect traces each for T5 and non-T5 builds.
This covers settings/UI/clock decisions, deferral, lock scope, all callback
results, cancellation/drain short-circuiting, hard-power return, and wake flags.
Fixtures never enter production builds.

Existing production PMU bridge, display/storage failure and desk-clock tests
remain separate gates. The focused workflow fetches only the exact comparison
commit and runs normal and ASan/UBSan matrices. Standard target/integration CI is
also required on the published head. Host traces model callback outcomes, not
physical rails, PMU timing, sleep current, or wake reliability. Hardware remains
unqualified. No firmware version, package payload, release pin or release feed is
changed, and no merge, release, device operation or paused work is included.
