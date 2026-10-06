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

## Candidate preserved, dominant-cause investigation continues — 07:52 UTC

Four functions in three production files now use direct regular-file handles only on X4/T5S3 volume backends. `SdVfs.cpp` remains **unchanged**: the exploratory stale-destination test exposed a pre-existing correctness issue whose safer rejection would alter this repair's failure behavior. That evidence is retained separately for the existing bug workflow. Both receipt routines and source-stage `sizeOf` also remain unchanged.

The exact four-function candidate and original baseline passed 17 actual-provider scenarios on each of X4 native-SD and T5 SPI: **68 ASan/UBSan executions**, with LeakSanitizer disabled for ptrace. Both original baselines independently fail the direct-stat cost assertion. Normal/missing/wrong-type/header/digest/size/read rejection, handle exhaustion, lock/open errors, real CRC failure/remount retry, regular-close retention, managed pin uncertainty, existing receipt states and generation/coherency controls are covered. Non-volume fallback source branches remain byte-equivalent; existing legacy managed/executable admission suites pass. This is focused host evidence, not a full firmware/aggregate or hardware pass.

Reproduce the focused candidate:

```sh
python3 test/storage_volume/direct_read_semantics_test.py --require-direct-budget
python3 test/storage_volume/direct_read_semantics_test.py --spi --require-direct-budget
```

The runner accepts `--baseline-root` and `--baseline-must-fail-budget` for an independently compiled original-source negative control.

A separate diagnostic used 37 real app pairs from exact-head artifact 11396135417 (archive SHA256 `157cf10990cce9f7c53a1196af96529920441e73e9294f94079ec65be9dfb46e`) and nine real drivers from artifact 11394962908. All 40 published app-pair hashes match compact artifact 11396260209; the 37-pair subset was a workload, not an assertion about the owner's installed card. Selected Home-pin, recovery, Springboard resolution/admission/inventory and Model Viewer selection/admission stages fall from **3,776 to 3,496 sectors** for loose installs and **4,888 to 4,800** for canonical installs. Read calls/bytes are unchanged. At an injected 3 ms/sector, modeled savings are **980 ms / 308 ms** respectively. These partial path sums exclude display, physical card, target relocation and actual app execution; they are not complete device timings.

The improvement is too small to establish the owner's main cumulative delay is solved. Integration into PR350 and full target CI are deliberately deferred while the dominant end-to-end investigation continues. Preserve this candidate; do not describe it as integrated or ready for hardware.

The known delivered 1.3.127 PR350 handoff identifies source `d84001460fbc52e9b989e7d5e5b72ea846f26248`. AppPackageInstaller, InstalledAppPath, ManagedAppAdmission, PackageExecutableAdmission, InstalledCapabilityResolver, InstalledProviderGraph, BootstrapModuleStore, SdBootReader, NativeTouchInput and X4 SD driver are byte-identical between that source and 1.3.155. The installedRefresh/runNativeSpringboard function bodies also match. These remaining basic-path costs therefore persist on the known delivered source line; what the owner actually installed remains unverified.
