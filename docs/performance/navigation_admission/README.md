# T5 navigation admission: bounded directory ownership

Claim/report: [PR332 comment6012329134](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/332#issuecomment-6012329134), stable alias `PERF-20261006-NAVIGATION-ADMISSION-DIRECTORY-EXHAUSTION`. Original source is PR350 `1cb042921630b7d2f6613c790ee546cf4c26c560`, firmware1.3.155. This repair reserves firmware1.3.157; the unrelated direct-open1.3.156 candidate is excluded. No driver/app payload source changes.

## Confirmed cause and reachability

T5 foreground input calls device discovery, navigation and touch before dispatching UI input. External navigation is enabled by default (`CrossPointSettings::externalInputNavigation = 1`). `NativeNavigationInput::ready()` synchronously acquires `input.navigation`, retrying failed acquisition on its existing one-second schedule. Actual current packages provide this legitimate closure:

- Eight bootstrap modules: platform-clock-v1, i2c-esp32s3-v2, pca9535-gpio, tps65185-power, spi-esp32s3-v1, t5s3-sd, t5s3-frontlight, display-epd-video.
- Ten navigation dependencies: t5s3-usb-power-profile, board-power-t5s3-v2, usb-controller-esp32s3, usb-host-v2, usb-hid, usb-hid-keyboard, usb-hid-text-input, usb-hid-gamepad, usb-xinput-gamepad, usb-ui-navigation.
- GT911 touch is the nineteenth module.

Original `registerCapability` held its root directory cursor while `registerOne` recursively registered dependencies. The eight-level navigation chain fills `Drivers/storage_fatfs/volume.c`'s eight-directory pool, then cannot inspect the installed USB power-profile directory. The shared volume implementation is included by both T5 SPI and X4 SD drivers. This is directory ownership exhaustion, not missing/corrupt installed metadata. Every failed attempt cleans up, but the next foreground attempt repeats the same unsuccessful work. GT911 registration works independently, so functional touch/Model Viewer does not exclude this failure.

Separately, topological registration avoiding the directory-depth failure stops at the original 16-module/pin ceiling. The complete navigation closure needs18 modules and touch needs19. The original metadata enumeration also faults above16 installed candidates, though serial discovery latches that fault instead of repeating its I/O.

The relevant installed registration/resolver source is unchanged from the known delivered1.3.127 source `d84001460fbc52e9b989e7d5e5b72ea846f26248`. The user's installed firmware/inventory is not established. This conditional production failure is not proof of the user's exact ten-minute hardware duration. X4 normally uses boot-selected navigation/touch providers, so these T5 results must not be attributed to ordinary X4 input.

## Repair and behavior

`registerCapability` collects only matching IDs/root references in enumeration order, closes and checks all root cursors, then invokes existing `registerOne`. The local list is discarded at return; it is not a retained cache or content proof. `registerOne` still rereads the package, validates identity/kind, digest-bound profile/source/imports and requirements, captures the ELF and its declared digest, and enters the same trusted executor. Existing executable integrity/admission before mapping remains unchanged. No stale directory entry replaces a validation check.

Maximum candidates are3 roots ×64 ordinary entries. Maximum registration depth remains16. Each allocation is nothrow and checked; list destruction is iterative. Matching candidates are admitted in the same order until two actually succeed, preserving the ambiguity criterion. Missing/unparseable/nonmatching profiles remain skipped; directory read/close/entry-limit failures remain failures. Complete discovery now precedes admission, so a late root failure no longer leaves a newly admitted prefix for that capability. Prior independent registrations/pins are preserved. Mutable/raw-storage discovery remains fresh per capability; no new reuse/coherency promise is introduced.

Module/discovery capacity is24, supporting the confirmed19-provider set and four additional serial classes while retaining eight of the32 shared package-use slots for app/package users. Dependency arity and ancestry depth stay16 independently, avoiding quadratic tables or bigger recursive workspaces. Full capacity refuses a new provider while permitting reuse of an existing one. This does not claim support for every broad-catalog package simultaneously. Navigation retry policy is unchanged.

## Reproducible operation counts

The portable test uses86 authentic files,19 complete packages and exact versions/hashes recorded in `test/installed_provider_registration/fixtures/manifest.json`. It compiles production graph/module/executor/bootstrap, registration, resolver, package inspection, HAL and shared FatFs volume. The X4 host sector model is a transport boundary, not T5 hardware timing. Host tests stop before target ELF execution.

| Case | Root/profile scans | Profile reads | Total file reads | Explicit upper-layer tick waits | Peak directory cursors | Result |
|---|---:|---:|---:|---:|---:|---|
| Original one navigation attempt | 10 | 197 | 226 | 743 | 8 | Fails; graph remains8 bootstrap modules |
| Original ten more attempts | 100 | 1,970 | 2,260 | 7,430 | 8 | Same repeated failure |
| Candidate cold navigation | 16 | 314 | 2,080 | 3,123 | 1 |18 modules admitted |
| Candidate ten forced warm registrations |10 |190 |190 |580 |1 |Success, no ELF reread |
| Candidate touch after navigation |3 |58 |147 |295 |1 |19 modules admitted |

Cold success does more useful work than the original early failure: it reads903,379 bytes including the real dependency ELF snapshots. This is a correctness fix removing repeated failed admission, not a claim that cold loading became instantaneous. Registration itself peaks at one directory; cold inventory or generation rebuild can peak at two. All return paths tested leave zero directory handles. The final fixture's original attempt uses1,946 sectors; an earlier layout used1,858. Root/profile/read counts are stable; sector counts depend on card layout and are informational.

Successful `NativeNavigationInput` retains its API. The existing production input lifecycle test now verifies1,000 foreground ticks without another acquisition/release. That is separate from the real-package admission test; target provider activation and physical USB readiness are not emulated as a proven hardware success. A forced warm registration still scans19 profiles; it is not falsely reported as zero-I/O. Serial metadata discovery now accepts this19-package set, with ten unchanged refreshes at zero I/O.

## Memory and bounds

`python3 scripts/measure_provider_graph_layout.py /path/to/xtensa-toolchain/bin` compiles actual structure declarations with the target compiler and reads object symbol sizes; no target execution is claimed. Pass `--source-root` to measure the original checkout.

| Target structure | Original | Candidate |
|---|---:|---:|
| GraphV2 |19,128 B|28,472 B|
| Package pin table |1,536 B|2,304 B|
| Discovery table |2,112 B|3,168 B|
| Serial inventory |1,864 B|2,792 B|
| Registration ancestry |27,392 B|27,392 B|

Capacity-related fixed/graph increase is12,096 bytes. Each matching node is72 bytes, and the stack list owner12 bytes. The actual eight-level unique chain retains about576 node bytes plus allocator overhead. The conservative192×16 node bound is221,184 bytes plus overhead; checked allocation refusal is required, not a promise that pathological input fits memory. Existing per-depth5,088-byte package plans are unchanged. The19 actual retained ELF payloads total1,202,956 bytes, plus373,008 bytes of existing owned/import metadata, excluding mappings, temporary copies and allocator overhead. Existing PSRAM-aware payload/import allocation is retained.

## Checks and reproduction

Run with an installed ArduinoJson include path, without network access:

```sh
python3 test/installed_provider_registration/run_test.py /path/to/ArduinoJson/src
python3 test/installed_provider_registration/run_test.py /path/to/ArduinoJson/src --sanitize
python3 test/installed_provider_registration/run_test.py /path/to/ArduinoJson/src --source-root /path/to/original --expect-original
```

Original-control mode requires the known original failures; it is never a candidate-success mode. Running candidate assertions against the original is a separate negative control. The main T5 CI build invokes the sanitizer candidate test after firmware compilation. The fixture archive and every file's length/hash are checked before use; no target binary is executed by this regression.

Completed checks and remaining target/CI results are recorded in the claim checkpoint. Local sanitizer runs set `ASAN_OPTIONS=detect_leaks=0` because LeakSanitizer is unsupported under tracing; this is not a leak-check pass. Normal and ASan/UBSan original/candidate tests cover navigation-first/touch-first, directory read/close, profile/ELF short reads, generation changes, retry/cleanup, exact capacity and existing-provider reuse. Existing provider-enumeration, storage-generation, graph ownership/admission/recovery and USB/input lifecycle suites supplement these package tests. Final normal and ASan/UBSan cases additionally cover checked matching-node allocation refusal at nodes1/2/4, same-length profile/header mismatch refusal, valid-header ELF payload mismatch refusal at real pre-mapping admission, and successful repair/retry. A separate leak-enabled attempt reached all assertions but LeakSanitizer failed under ptrace; its failure is preserved and is not a leak-check pass. `original-rejected-by-candidate.log` records the original failing the candidate runtime assertion. Full NativeApp, Springboard, storage-generation, graph, and USB/HID aggregates pass locally; final target/CI results remain pending the integration checkpoint.

No hardware, release, flash, master merge or user-device attribution is part of this repair.
