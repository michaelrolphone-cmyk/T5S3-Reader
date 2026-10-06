# Basic boot and input scan, 2026-10-06 08:40 UTC

**Result: zero newly confirmed defects.** This checkpoint preserves useful negative controls and one small, fixed cleanup-cost observation. It does not explain the owner's severe boot/input/app-loading delay, declare the device healthy, create a repair claim, assign a new canonical bug number, or supersede any existing report.

## Baseline and coordination

Repository `michaelrolphone-cmyk/T5S3-Reader`, ID 1367546328. Live default is master at `c765a9931e2f1772d1dd3b5870362c18a08a9e57`. The tested source is open, unmerged [PR350](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/350), `xteink-x4-pro-boot`, commit `1cb042921630b7d2f6613c790ee546cf4c26c560`, firmware 1.3.155. Actual comparison is diverged, 219 ahead / 31 behind, merge base `21ce3b5b720e106815c8f3a7778bc7003294e0e2`. Master does not contain this integration. PR348 is closed/unmerged; its head `108daf2050065076346ae016b3b030b1e7ec3e2a` is ancestral to the tested source, 1026 ahead / 0 behind.

The navigation repair remains exclusively with `trace_basic_reader_latency`, [claim 6012329134](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/332#issuecomment-6012329134), reservation 1.3.157. Its authentic 19-provider T5 closure, recursive eight-directory-slot exhaustion, graph/module 16-slot ceiling and enumeration capacity work were excluded. This scan does not rerun or duplicate that repair. The unpublished direct-open 1.3.156 candidate and released FTDI claim 6012227494 were also left alone.

Shared ledger is `automation/bug-ledger` at `101e2f7f58a8daabbccd3227ad878105e3847320`; sole coordinator remains `resume_scan_reconcile_ledger`. Current bugs/progress/workflow were freshly read from that exact remote commit. This isolated report is queued to PR332 for reconciliation; it does not edit canonical files or claim their ownership.

## Production path traced

- Cold graphical boot initializes display/built-in fonts, starts loading feedback, executes `setupReaderState`, captures input capability health, then runs `MappedInputManager::update` before preparing the Reader application. Provider admission remains synchronous on the owner task. Existing bootstrap-wait evidence [6011918353](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/332#issuecomment-6011918353) is retained, not rediscovered as a new defect.
- T5 foreground update performs GPIO/device discovery, navigation admission and then touch observation before ActivityManager dispatch. The independently scheduled touch worker captures and drains one bounded provider report per turn. Having a live touch sampler does not prove the owner task is reaching UI consumption promptly. The known navigation/FTDI paths remain the material identified synchronous candidates.
- Native applications call the same owner input update from `pollInput`. ActivityManager and native-app presentation fences prevent taps captured on an outgoing/loading view from being replayed on the destination. These deliberate stale-input rules are not newly reported defects.
- Home pinned-app resolution and its filesystem probe multiplication are covered by existing pathname work. Font discovery's failure/partial-snapshot semantics overlap canonical 188; no new performance claim was inferred from a large synthetic font tree. No image, Model Viewer or EPUB parser investigation was undertaken.

## Warm GT911 and touch-consumer operation counts

`touch_work_counts.py` compiles the unchanged production GT911 C driver with the actual consumer gesture/service/getter bodies. Only the physical I2C endpoint, clock, RTOS wrappers and API counters are fixtures. The ordinary T5 contact coordinates are 100, 200 on the driver's 540×960 surface. The provider starts once; no package scan, target ELF relocation, physical bus arbitration or panel operation is modeled.

| Case | Capture polls | Queue next calls | Snapshot calls | Bus transactions | Delivered taps |
|---|---:|---:|---:|---:|---:|
| Start/probe |0|0|1|2|0|
|1000 idle capture turns|1000|1000|0|1000|0|
|100 healthy down/up taps|200|400|0|500|100|
|100 taps with 400 injected bus failures|600|800|0|1250|100|

Healthy taps perform 200 status reads, 100 point reads and 200 ACK writes. The fault case injects three failed point reads and one failed ACK for every tap; half the failed ACK writes nevertheless reach the modeled controller. It records 600 status reads, 400 point reads and 250 ACK writes, with no duplicate/missing delivered taps or resnapshot loop. Teardown releases the subscription and physical claim. This demonstrates bounded source work in those selected cases, not physical latency or general fault immunity.

Normal and fatal ASan/UBSan runs produce identical JSON records. The separately run existing production capture suite also passes transient/outage/GAP/recovery/swipe/clock-rollover cases, 100 faulted actual-driver taps, delayed UI delivery, and complete consumer lifecycle tests for both T5 and X4 compile guards. The source's capture period is 5 ms; the host executes explicit capture turns and does not claim FreeRTOS scheduling fidelity.

## Small fixed native-app cleanup budget

The source has a fixed 4096-entry app-allocation ledger. `AppAllocationLedger::end` scans every slot and calls its cooperation callback after every 64 slots, even when the live count is zero. `NativeAppMemory::cooperate` maps each callback to `vTaskDelay(1)`.

The complete unchanged `Apps/default.c`, `NativeAppLauncher.c`, `NativeAppMemory.cpp` and allocation ledger are linked together in `allocation_exit_scan.py`. A loader fixture returns the real, host-compiled PaperSpace entry point, whose single firmware pump requests the ordinary child-app handoff. PaperSpace makes no app allocator call; its live allocation count is zero. The actual launcher still requests 64 one-tick waits before `dlclose` and before the next child can start. Twenty independent handoffs request 1280 ticks. A failed `dlopen`, a missing entry symbol, an empty direct ledger, one residual allocation at slot 4095, and all 4096 live slots each request 64 ticks; all owned allocations are reclaimed and repeated teardown is idempotent.

The declared PlatformIO 6.13.0 / Arduino 2.0.17 package and the T5S3/X4 board's `qio_opi` SDK selection use [CONFIG_FREERTOS_HZ=1000](https://github.com/espressif/arduino-esp32/blob/2.0.17/tools/sdk/esp32s3/qio_opi/include/sdkconfig.h). Thus 64 requests are 64 nominal tick intervals, approximately 64 ms in a full-tick artificial-clock model. This is **not measured transition time, a real lower/upper bound, or a 64 ms promised saving**. Each wait runs to the next tick; tick phase, scheduler contention and actual heap/loader/display work remain unmeasured. There is no measured denominator for its contribution to total transition latency, and this small fixed budget is not evidence explaining a multi-minute symptom.

Classification: quantified optimization observation only, zero additional confirmed defects. The existing allocation test explicitly expects cooperation for dense ledgers; do not relabel the fairness requirement itself as a bug. The only potentially avoidable part is visiting/cooperating across known-empty slots. Any later authorized change must preserve allocation ownership, reclamation, bounded item/byte/time cooperation, concurrency/error semantics and retained-failure safety. No implementation or altered cleanup policy is supplied. Canonical 190 concerns a mutex-timeout/hidden-cleanup-failure bug and remains separate, with its original status unchanged.

Normal and fatal ASan/UBSan complete-launcher counts match exactly. Dynamic-loader fixture symbols are renamed at compilation to avoid interposing on the host sanitizer's own `dlsym`. An initial logging-only host build warning and an initial sanitizer/fixture symbol collision were corrected in the harness; neither was a production failure. Target ELF machine-code execution, loader relocation, package admission, filesystem and FreeRTOS are fixtures, not hardware evidence.

## Other checks and deduplication

- Existing actual ActivityManager replace/push/pop and native launch/poll/error/retry tests pass, including 20–30-second artificial preparation/result stalls, failed presentation followed by retry, fresh input, Back/Power and single-surface dispatch. These tests preserve the intended input fences; they do not benchmark the artificial stalls.
- 25 production default-app/boot-dispatch cases pass across T5 and X4, including Home, missing/rejected/invalid/explicit default, custom destination, recovery, display failure and retained-loader paths. Reader/desk cases only check dispatch; no book parser was investigated.
- Existing T5 board-power poll test passes read/configuration throttling, failure retention and clock wrap. It does not measure the real battery hardware.
- Fresh census: 393 branches, 436 all-state PRs, 587 distinct heads, 192 PR332 comments through 6012329134, zero non-PR issues. All selected source trees/blobs were available. Checked 257 historical report/ledger/README blobs with no missing content, the current canonical inventory and recent queue reports. One allocation-ledger variant, two NativeAppMemory variants, 37 launcher variants, one default-app and one NativeReaderEntry variant were enumerated; source variants and source-bearing counts are retained in `dedup-summary.json`. No competing empty-ledger optimization was found. This does not promote the small observation into a new bug count.

LeakSanitizer is unavailable under tracing and was disabled; AddressSanitizer and fatal UndefinedBehaviorSanitizer passed. The first unconfigured capture-suite run stopped at the known LeakSanitizer/ptrace limitation; its rerun passed with leak detection disabled. No new target build, hardware measurement, package installation, device operation, master/PR350/shared-ledger write, merge or release was performed.

## Reproduction

Use an unmodified checkout at the source SHA, Python 3, C/C++ compilers and the checkout's existing fixtures:

```
python touch_work_counts.py READER_CHECKOUT OUTPUT_DIR --sanitize
python allocation_exit_scan.py READER_CHECKOUT OUTPUT_DIR --sanitize
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 python test/hal/touch_capture_behavior_test.py
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 python test/hal/activity_touch_focus_test.py
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 python test/hal/native_app_focus_test.py
python test/reader_entry/boot_dispatch_test.py
python test/hal/board_power_poll_test.py
```

The first two scripts accept separate output directories and do not modify the checkout. Source hashes, exact operation records and existing-check logs accompany this report. Keep the ongoing authentic 19-provider navigation repair as the active primary lead; this scan adds no parallel repair.
