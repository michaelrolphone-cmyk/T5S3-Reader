# Shared U1 provider-enumeration performance repair

## Scope and status

Firmware 1.3.91, based on master `cff6ef0c11b79634dfa2c688d880df393d8c153c`.
This repairs a demonstrated repeated metadata-I/O regression common to the
legacy T5 and extracted T5/X4 storage paths. It does **not** establish that the
reported two-minute X4 Springboard launch has been completely explained or
fixed. There is no hardware timing or device qualification claim.

No app/driver/provider payload changes, HAL API additions, board extraction,
verification bypass, release, merge or deployment accompany this repair.
Independently versioned packages therefore retain their versions.

## Source-grounded cause

The exact pre-U1 parent is `ca66db298c2e735f45e5029083a9bfbd7b6740bd`; the U1
merge is `f7f006f78bf1f83c28f3ce05728b8973e895956b`.

Before U1, `NativeSerialPortBridge_implementation.inc::synchronizeUsbDevice`
reconciles the existing in-memory device diagnostics. The installed-provider
path calls `InstalledSerialInventory::refresh`, which begins a new
`nextProvider` cursor on every refresh. `nativeSerialPortsBegin` reaches it for
ordinary app entry; T5 `MappedInputManager::update` reaches it while polling.
The old `nextProvider` rebuilt all installed provider metadata whenever that
cursor was zero, even if the requested capability was absent and storage had
not changed. Its static candidate array was only reused within one traversal.

This is distinct from the CDC recovery observation performed by `prepare`.
That earlier fix is reused unchanged from
[19ca5b07f281f4267046f0bb2dc5b689c0714099](https://github.com/michaelrolphone-cmyk/T5S3-Reader/commit/19ca5b07f281f4267046f0bb2dc5b689c0714099):
`PackageCdcSdMigration.cpp`, its original pre-bootstrap regression and fixture
have identical blobs. Master lacked this dependency, so omitting it would
leave the outer warm path doing directory I/O even after the new enumeration
cache. No bootstrap gate or board feature is backported.

## Repair and safety boundaries

- Retain the already bounded 16-candidate array only after a complete scan of
  all optional roots, with matching quiescent storage-generation observations.
- Rebuild after mutation, remount, active writers or uncertain raw-storage
  access. Errors, failed closes, changed generations and incomplete scans
  cannot become reusable negative observations.
- Probe once beyond the 64-entry root bound to distinguish exact completion
  from truncation; reject candidate overflow and non-directory roots.
- Keep cursors opaque and bind them to a nonwrapping snapshot serial. Rebuilds
  invalidate earlier cursors before allocation/scanning, including failed
  rebuilds. Same-snapshot interleaving is supported; epoch exhaustion fails
  closed rather than aliasing an old cursor.
- Add no metadata-cache mutex: the existing serialized provider-owner task
  owns enumeration. The reused CDC mutex protects only a stamp, never I/O.
- Continue normal provider polling and independent activation, import,
  authorization, generation-pin and executable-integrity checks. This cache
  is a metadata observation, never a right to execute or access hardware.

## Focused before/after evidence

An isolated copy of the exact delivered X4 source
`e58ac3106e5569a1271d28ab3a2f87907129a601` was compiled with its actual
`storage_fatfs`, X4 native-SD or T5 SPI provider, HalStorageVolume, metadata
verifier, manifest parser and admission code against the existing SD-wire
model. A separate legacy-T5 run compiled actual HalStorage.cpp with the
existing fault-media SdFat fixture. These are host operation counts, not card
latencies. An independent reviewer repeated the three backend runs.

The four delivered essential app packages were Springboard, Settings, App Store
and Driver Manager, plus eight X4 drivers. The later optional Wi-Fi Settings
archive was measured as a separate five-app case. Delivered packages do not
prove what was actually installed on the user's card.

For eight installed drivers and an absent serial capability, the old metadata
enumerator repeated the following on **ten unchanged warm calls**:

| Backend | Opens | Bulk reads | Read bytes | Explicit upper-layer 1 ms waits |
| --- | ---: | ---: | ---: | ---: |
| Legacy T5 HalStorage/SdFat fixture | 1,790 | 240 | 67,810 | 1,370 |
| X4/T5 volume providers | 1,790 | 240 | 67,810 | 1,610 |

The volume-provider runs additionally requested 11,470 sectors and 1,440
provider cooperation waits. After the change, those ten warm enumerations
perform **zero file opens, reads, sector requests or cooperative waits**.
A combined run including the actual CDC adapter and actual `prepare` function
with an already allocated graph likewise has zero warm metadata I/O. Cold
verification remains; a generation mutation or CRC failure followed by remount
forces it again. None of these counts is a physical elapsed-time measurement.

The pinned Arduino 2.0.17 ESP32-S3 SDK sets
[CONFIG_FREERTOS_HZ=1000](https://github.com/espressif/arduino-esp32/blob/2.0.17/tools/sdk/esp32s3/sdkconfig),
and [delay(ms)](https://github.com/espressif/arduino-esp32/blob/2.0.17/cores/esp32/esp32-hal-misc.c)
uses `vTaskDelay(ms / portTICK_PERIOD_MS)`. The inherited transport fixture's
10 ms rounding was corrected to 1 ms for this investigation; its older modeled
milliseconds must not be used as device timing or a two-minute explanation.

## Hypotheses checked, not silently conflated

- Springboard's delivered ELF is 12,208 bytes. The U1 4 KiB loader path makes
  three bulk reads and three loader waits, not thousands of tiny reads.
- Four-app installedRefresh uses 12 bulk reads / 3,065 bytes and zero scalar or
  one-byte reads. Five apps use 15 / 3,851, also zero scalar reads. ArduinoJson
  parses the captured strings, not a per-byte HalFile adapter here.
- Managed admission reads two metadata files; final snapshot admission rereads
  the package manifest. The first admission hashes the already read 12,208-byte
  ELF in memory; an unchanged warm admission hashes metadata but skips the ELF
  content hash. These checks remain intact.
- A separate volume-HAL pathname-reopen amplification is real: synthetic
  38/100-package installedRefresh runs request 12,187/73,974 sectors. Those
  synthetic counts do not describe the user's installed inventory, and that
  optimization is deliberately outside this common-U1 repair.

## Validation and remaining limits

- Captured original master independently fails the committed warm-I/O assertion.
- Normal and ASan/UBSan focused regressions pass mutation/remount, writer/raw
  windows, interleaved cursors, failures/retry, bounds and close uncertainty.
- The original CDC regression passes its negative-observation invalidation and
  recovery-boundary cases. Storage-generation aggregate passes.
- Actual X4/T5 provider models cover generation invalidation, active writers,
  CRC failure and successful remount/retry. Legacy T5 uses fault-media fixtures;
  it is not a physical SdFat/card-throughput benchmark.
- The direct provider-graph aggregate stopped at its internally overridden
  LeakSanitizer options under sandbox ptrace; this direct run is not a pass.
  A local-only runner retains the same tests with leak detection disabled.
- Local sanitizers used `detect_leaks=0` because LeakSanitizer cannot trace this
  sandbox. Hosted defaults remain unchanged. Local full target builds are not
  claimed; exact-commit hosted checks are recorded in the PR.
- Independent review found the initial overlapping-cursor flaw; the final
  serial-tagged cursor implementation and regression fix it.

The remaining two-minute symptom needs actual launch-stage observations or
further source evidence. This repair removes proven recurring work without
renaming that unresolved observation as solved.
