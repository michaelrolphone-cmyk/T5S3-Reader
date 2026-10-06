# X4 boot admission: stacked scheduler waits, quantified

Stable evidence ID: `michaelrolphone-cmyk/T5S3-Reader::PERF-20261006-X4-BOOT-STACKED-WAITS`.

## Classification

This is a newly quantified performance optimization candidate in the already documented boot cooperation policy. It is **not a new correctness regression, not a confirmed explanation of the owner's total boot delay, and not counted as a newly confirmed defect**. No canonical bug number, implementation claim, repair PR, or version reservation is assigned. The current documentation explicitly says bootstrap reads yield per sector/chunk and filesystem traversal yields per 256 steps or 8 ms. Preserve that provenance: [existing boot policy](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/1cb042921630b7d2f6613c790ee546cf4c26c560/docs/boards/sd-driver-bootstrap.md).

## Exact source and integration

- Repository ID 1367546328; source/base `xteink-x4-pro-boot`, PR350, SHA `1cb042921630b7d2f6613c790ee546cf4c26c560`, firmware 1.3.155.
- Live default branch is master at `c765a9931e2f1772d1dd3b5870362c18a08a9e57`. PR350 is open/unmerged and diverged, 219 commits ahead/31 behind master with merge base `21ce3b5b720e106815c8f3a7778bc7003294e0e2`; these fixes are not on master.
- PR348 is closed/unmerged. Its head `108daf2050065076346ae016b3b030b1e7ec3e2a` is an ancestor of the source (0 behind / 1,026 ahead).
- The current BMP, text-view and other performance integrations leave `SdBootReader.cpp` unchanged: blob `094ed6f80bcb4344b8a5ee3695374fef8adc54f4`. No PR350/master source write was made for this evidence.
- Shared ledger remains `automation/bug-ledger` at `101e2f7f58a8daabbccd3227ad878105e3847320`, existing sole writer/PR332 retained. This isolated report is queued for that coordinator, without editing shared ledger files.

## Real workload and synchronous boot position

The exact-head X4 deployment artifact [run37425567068](https://github.com/michaelrolphone-cmyk/T5S3-Reader/actions/runs/37425567068), artifact11394962908 (`x4-external-deployment-1cb042921630b7d2f6613c790ee546cf4c26c560`), is 4,563,399 bytes with verified SHA256 `8f3622efe551efac744c54d0b387f2c0b02ae0d72a0d54d9b97781292fdc2156`. Its nested `sdcard.zip` has 56 regular files; every extracted file was compared byte-for-byte with that archive. All file hashes accompany this report.

Although nine driver packages are present in the artifact, its actual boot.json selects **seven**: platform-clock-v1 0.1.1, x4pro-panel 0.1.14, x4pro-buttons 0.1.5, x4pro-frontlight 0.1.3, x4pro-sd 0.2.3, x4pro-i2c 0.1.4 and x4pro-gt911 0.1.5. Battery and RTC packages are not silently counted as bootstrap selections.

`X4DiagnosticSetup` synchronously calls `loadPlatformSdPackages()` at its Packages stage, before storage binding, navigation/display/RTC/touch activation, fonts, splash, battery sampling and Home preparation. `SdPackageBoot` has a static attempted/ready guard: this path executes **once per cold firmware boot**, not per UI frame, not on every capability query and not in the display-restore sleep path. These waits therefore lie before usable Home on the same boot critical path. Subsequent loader/relocation, provider starts, Home app admission, screen preparation and panel time are outside this measurement.

The reader opens the seven selected real packages through the complete production `BootstrapModuleStore`, production `SdBootReader`, actual read-only/private-symbol FatFs and Unicode parser, and production ordinary manifest/preflight/hash logic. Source manifest re-reads remain unchanged. The registration endpoint is a fixture that checks ELF presence/header/root then accepts the candidate; **target driver execution and full graph activation are not simulated**. Actual artifact bytes and hashes establish the package input rather than invented declaration-only drivers.

## Reproduction and counts

A deterministic 64 MiB FAT32 card is built with the production writable FatFs, inserting the exact 56 artifact files in sorted pathname order. The read phase uses production read-only FatFs. Controller/GPIO/clock endpoints are fixtures. The SDK read routines `sdmmc_read_sectors`, `sdmmc_read_sectors_dma` and `sdmmc_send_cmd_send_status` are extracted unchanged from pinned IDF4.4.7; the physical command endpoint supplies a successful sector read and immediately-ready status.

The actual SDK read path sends CMD17 and then CMD13 even when the first status is ready. Both reach Reader's `transaction()` and its unconditional `vTaskDelay(1)`. Reader then also waits once in `transfer()` after each 512-byte sector, and once in `read()` after each at-most-4-KiB chunk. The traversal checkpoint has an independent last-yield clock; elapsed time spent in lower-layer waits can trigger an additional filesystem yield.

Every run admits seven packages with **58 file-read calls, 145,544 delivered file bytes, 537 requested sectors, 1,074 SDK commands, and 85 read chunks**. The sector count includes FAT/pathname work and depends on the explicitly declared card layout, not just payload size.

| Clock fixture | Command waits | Sector waits | Chunk waits | Traversal waits | Total vTaskDelay(1) requests | Loader delay(1) calls |
|---|---:|---:|---:|---:|---:|---:|
| Zero-time counting control | 1,074 | 537 | 85 | 0 | 1,696 | 7 |
| 1 ms advanced per tick | 1,074 | 537 | 85 | 142 | 1,838 | 7 |
| 10 ms advanced per tick, sensitivity only | 1,074 | 537 | 85 | 446 | 2,142 | 7 |

Normal and fatal ASan/UBSan executions agree byte-for-byte on all three runs. The final portable published script was separately rebuilt and rerun under the sanitizers and reproduced all three records. LeakSanitizer is disabled under tracing. Earlier exploratory results used unsorted directory insertion and a one-command transport stub; those are superseded and are not the reported reproduction.

The 1,696 unconditional requests are independent of model wall-clock advancement. The 1 ms model adds 142 traversal requests and advances its artificial clock by 1,845 ms including the seven loader calls. This is **not measured boot latency, not a lower bound on real boot time, and not a claimed 1.845-second saving**. vTaskDelay(1) waits for the next tick and its real duration depends on tick phase, other task execution and I/O. Hardware/card/interrupt costs are absent. The model establishes where and how frequently the boot owner is explicitly suspended; it cannot establish a fraction of the owner's overall boot-to-Home time.

## Target tick contract

The exact source pins PlatformIO espressif32 6.13.0 and Arduino framework `3.20017.241212+sha.dcc1105b` (Arduino2.0.17). The xteink-x4-pro environment uses the t5s3-pro board definition, whose memory_type is `qio_opi`. The corresponding official [Arduino2.0.17 ESP32-S3 QIO/OPI SDK header](https://github.com/espressif/arduino-esp32/blob/2.0.17/tools/sdk/esp32s3/qio_opi/include/sdkconfig.h) defines CONFIG_FREERTOS_HZ=1000. The [IDF4.4.7 port](https://github.com/espressif/esp-idf/blob/v4.4.7/components/freertos/port/xtensa/include/freertos/portmacro.h) defines portTICK_PERIOD_MS as 1000/configTICK_RATE_HZ. One tick is nominally 1 ms for this selected SDK configuration; the 10 ms experiment is explicitly **not** the target configuration. No hardware scheduler measurement or target build was run by this scanner.

SDK read source: [IDF4.4.7 sdmmc_cmd.c](https://github.com/espressif/esp-idf/blob/v4.4.7/components/sdmmc/sdmmc_cmd.c), SHA256 `c1668f6932a12448bea2989e69497aca62cf3c4ad22bb3ef4ef8fa6efb5d3e3c`.

## Deduplication and limits

Refreshed 389 branches, 436 all-state PRs and 583 distinct current heads; all source trees were available. SdBootReader, BootstrapModuleStore and SdPackageBoot each have one source variant across the 32 heads carrying those files. There is no alternative boot-wait repair. Read current canonical bugs/progress/workflow, PR332 reports/claims and all current PR titles/bodies; checked 257 historical report/ledger/README blobs with no missing content, plus the latest STL queue evidence. The only matched historical bootstrap-read reference was storage-volume README handoff cleanup coverage. Current non-PR issue search is empty. Existing boot documentation is acknowledged above, not hidden to inflate novelty.

The runtime software-SD transport ceiling, Springboard pathname-probe report, conditional dependency-DAG stress report, T5-only legacy serial reload, and existing BMP/text/EPUB per-call waits concern different code/lifetimes. The ongoing app direct-open repair owner was consulted; that work does not claim or change these boot waits. The four manifest reads per selected package are retained as source behavior, not separately claimed as a defect: reducing them requires its own integrity/lifetime analysis.

Touch/main-loop review found no separately proven new defect in this pass. The source captures touch in an independent worker; synchronous owner work still delays consumption, with existing >2 s gesture expiration and >5 s consumer fences. Those guards are explicit stale-input policies, not new defects. Without physical timing this scan cannot conclude that these boot waits explain steady-state touch response.

## Safe bounded follow-up direction

If this optimization is selected, evaluate one operation-owned byte/item/elapsed cooperation budget across the boot command/sector/chunk/traversal layers, so a completed lower-layer yield is visible to the higher layer. Retain regular genuine scheduler cooperation, all command/file/admission deadlines and work caps, cancellation, SHA/metadata/import checks, checked close/unmount, exclusive controller handoff and failed-cleanup retention. Do not simply remove waits globally, change normal runtime driver ownership, skip integrity reads, or weaken timeouts. Recheck success, slow/failing command, cancellation, tick rollover and cleanup behavior against the original before any integration.

This report contains no implementation, repair claim, canonical count increase, default/PR350/shared-ledger write, merge, release, deployment or device operation.

## Reproduce

Obtain the exact deployment artifact above and extract its sdcard.zip, preserving hidden files. Supply an unmodified checkout at the source SHA, its ArduinoJson include directory and the pinned sdk source to:

```
python bootstrap_wait_scan.py READER_CHECKOUT EXTRACTED_SDCARD_ROOT ARDUINOJSON_INCLUDE SDMMC_CMD_C BUILD_OUTPUT --sanitize
```

The accompanying bootstrap_card_writer.cpp is required beside the script. The script asserts the pinned SDK file hash, creates its deterministic card image and runs all three clock fixtures. C/C++ compilers and OpenSSL are host prerequisites. No device or target toolchain is used.
