# Display consumer ownership migration

Read [Platform Specification](RISCRTE_PLATFORM_SPEC.md) and
[U1–U4 order](FOUR_MILESTONE_STREAM_FIRST_EXECUTION_ORDER.md).

## Implementation status

This is an unfinished continuation from merged PR220 (`2dbf7e0e`), on a new
branch. It does not replace the released 1.3.49 hardware-test candidate.
Firmware 1.3.51 and display-epd-video 0.1.3 are reserved for this branch;
1.3.50 remains reserved by U1. Published lineage was rechecked at 1.3.49 /
0.1.2 before publication. No release is performed by this PR.

The first connected correction adds an append-only checked stop to the private
video API. Legacy callers retain their original offsets and `void stop()`.
The new provider requires checked stop and fails closed on older backends.
It retains its active lease after failed shutdown, failed format change or
failed startup cleanup, blocks reacquisition until cleanup succeeds, and
invalidates old presentation tokens after successful shutdown. Previously it
reported quiescence after `void stop()` even when DMA/bus teardown had failed.
The host already checked that condition separately; no physical failure or
use-after-free has been demonstrated on a device.

## Concrete cutover dependencies

The remaining consumer migration cannot be a renderer pointer swap:

- `NativeVideoBridge::video_start_format` requires host takeover and always
  performs a white wisp scrub. Provider acquire therefore cannot implement
  retained-image boot/clock startup. `main.cpp` deliberately preflights before
  `display.begin(false)` and must not lose that protection.
- `display_epd_video/driver.c` ignores presentation options. The reader's
  `HalDisplay::displayBuffer` uses quality/text/fast/fastest waveforms with
  periodic refresh escalation; grayscale and page-turn presentation also have
  existing behavior. Forwarding all modes to video would silently discard it.
- `DeskClockSleep::resumeAfterTimerWake` runs before SD mount or provider
  discovery. A safe installed-driver wake path needs available module storage
  and bounded lifecycle/power support; retaining direct normal clock rendering
  would leave a second owner. Mounting/scanning SD every minute without a
  measured power/performance plan does not preserve the current behavior.
- The scanner still controls PCA/TPS/panel resources in firmware. Physical
  extraction must reuse U1's bus/shared-chip owners in U3, not create another
  firmware device API or duplicate chip owner to get a green migration test.

These are implementation dependencies, not requests for interim device tests.
Before claiming complete consumer cutover, provide equivalent retained-start,
quality/gray/differential refresh and sleep behavior behind the selected single
provider. If that needs coordinated U3 physical ownership work, report the
scope dependency instead of labeling a forwarding shim complete extraction.

## Remaining consumer coverage and completion criteria

Route main boot/flip/sleep, GfxRenderer mono/gray reader/UI output, StartupScreen
animation, springboard video, Hollow Trail (including narration reinforcement),
NativeAppHost frame capture, NativeHardwareTakeover return/re-entry,
DeskClockSleep timer/user wake and screenshots through the same exclusive
provider lifecycle. Keep UI compiled and optional; preserve existing formats,
geometry and app ABI. Recovery may be isolated but must never contend with or
masquerade as the normal provider.

Require exact-head firmware/host/provider/app CI; executable ownership tests
covering missing/corrupt providers, failed startup, pending frames, failed
quiescence, stale generations, format switch, exit/re-entry and sleep/wake.
Compare actual boot/Home/reader gray/springboard/Hollow/clock render output.
Current focused tests cover checked teardown and unchanged boot/app behavior;
full migration, full firmware build, exact-head CI and new render validation
are not yet complete. No physical qualification is claimed.

## Recovery for released 1.3.49

Published [1.3.49](https://github.com/michaelrolphone-cmyk/T5S3-Reader/releases/tag/firmware-v1.3.49)
uses viewer 1.2.7, Strike 1.0.3 and the complete display-epd-video 0.1.2 package.
Preserve the owner's known-working firmware and SD contents before testing.
[1.3.48](https://github.com/michaelrolphone-cmyk/T5S3-Reader/releases/tag/firmware-v1.3.48)
is an available rollback candidate, not a new hardware-qualified claim.

Use `riscrte_lilygo_t5s3_1.3.48-app.bin` for SD/OTA. Its published SHA-256 is
`0348293508faf84e58ef8caacf2f366dffb0276a4d583a536e0495dd5cb96e33`.
The built-in SD recovery picker is selected by holding left-side UP + POWER
on a power-button boot; it still requires successful display initialization.
If that fails, use the board's established ROM/USB recovery procedure with
`riscrte_lilygo_t5s3_1.3.48.bin` at offset `0x0`, published SHA-256
`a92395791ba8b8450679f470de0e8cfa6131cf794dad9e51002c1e49feb8094b`.
Verify the actual board and USB-entry procedure before issuing device commands.
Never feed the merged USB image to SD/OTA; the debug ELF is not flashable.
The SD updater selects its next OTA partition; no automatic rollback is assumed.
Asset hashes here are release metadata, not independently downloaded verification.

New display apps require firmware 1.3.49; retain compatible app/manifest pairs
when rolling back (e.g. viewer 1.2.5, Strike 1.0.2). Keep unrelated SD data.
For a failure record versions, trigger, last screen, input/backlight behavior,
boot/launch/present/exit/re-entry/sleep phase and any existing logs or panic
report. The emergency X/0xD1 requires a live backend; a retained e-paper image
alone does not prove a live CPU. No device operations were performed here.
