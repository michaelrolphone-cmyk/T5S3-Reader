# Regular-file admission without duplicate pathname stats

## Active per-issue claim — 2026-10-06 07:44 UTC

- Owner: `trace_basic_reader_latency`.
- Repository: `michaelrolphone-cmyk/T5S3-Reader`.
- Existing evidence alias: `PERF-20261004-SPRINGBOARD-PATH-PROBES`; canonical number remains unassigned in the current shared ledger.
- Baseline: PR350 `xteink-x4-pro-boot` at `1cb042921630b7d2f6613c790ee546cf4c26c560`, firmware 1.3.155.
- Isolated branch: `perf/app-launch-direct-read`. Reviewed integration target is the existing PR350, with a freshly captured parent and non-force update only. No new repair PR, master merge, release, deployment or device operation is authorized here.
- Version reservation: firmware **1.3.156**, above all source-bearing live candidates inspected. No app/driver/provider payload changes.
- Live master: `c765a9931e2f1772d1dd3b5870362c18a08a9e57`; PR350 is not integrated. PR348 is closed unmerged and absorbed into PR350.

This continues the residual described in [the original report](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/332#issuecomment-5977926551). PR408's backup-stat reduction and its [released claim](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/332#issuecomment-5980772250) remain unchanged. The preceding BMP owner [released its claim](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/332#issuecomment-6011308508).

Three unchanged attempts to publish the shared PR332 claim returned cancellation-only tool errors. Fresh comment readback showed no claim. The parent confirmed no user cancellation and directed this isolated per-issue checkpoint while shared-record reconciliation is queued. No shared ledger file or coordinator ownership is changed.

## Planned scope and evidence boundary

Use the existing `openFileForRead` only for regular-file admission on `BOARD_XTEINK_X4_PRO` and `BOARD_T5S3_PRO`, matching the volume HAL guard. Candidate functions are `AppPackageInstaller::existingRegularFile/verifyBytes`, `ManagedAppAdmission::readMetadata`, `PackageExecutableAdmission::admitInstalledExecutableSnapshot` and `SdVfs::openFile`. Preserve regular-file/type/size/header/digest checks, captured metadata, generation/coherency, independent authorization, pins, checked close, errors, retry and retained ownership. Legacy/CAM call paths remain unchanged.

Receipt reads are excluded: Missing, Invalid and CloseUncertain differ; the last is a hard admission failure. Directory opens, positive ELF existence probes, provider readFile and already-direct metadata readers are unchanged. No cache, stale dirent substitution, reduced validation, timeout increase or storage/provider ABI change.

Fresh deduplication covered 389 branch refs, 436 all-state PRs and 583 distinct heads; all candidate file variants were available and inspected. There were six AppPackageInstaller variants, one managed-admission variant, one executable-admission variant, one receipt variant and two SdVfs variants. None already implements these direct opens. New PR436 concerns Hollow app/evidence paths, not these production files. The latest coordination ledger remains `101e2f7f58a8daabbccd3227ad878105e3847320` under its existing sole coordinator.

## What remains unresolved

The existing real SD/FatFs/HAL fixture at 37 loose apps requests 1,260 sectors for 4,697 metadata bytes; 1,160 sectors are stat/open pathname work. Unchanged refresh repeats all work. Those particular positive ELF stats and sidecar opens are retained, so this repair alone cannot remove the dominant inventory term or establish that the owner's cumulative ten-minute experience is solved. Counts are host-model work, not physical timings.

Before publication, compare original/final normal/error/retry/coherency behavior and realistic startup/admission work, run applicable checks, then record exact remote integration and CI. Hardware is unavailable and remains unrun.
