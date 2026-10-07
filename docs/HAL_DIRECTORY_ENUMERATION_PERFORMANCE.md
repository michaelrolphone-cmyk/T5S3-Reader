# Provider-volume directory enumeration without a duplicate stat

## Scope

Firmware 1.3.95, focused follow-through to PR350 `082f1f97` (firmware 1.3.93).
The delivered 1.3.93 candidates remain unchanged. No provider/app package,
public HAL or storage.volume API, FatFs implementation, release, or deployment
changes accompany this repair. Independently versioned payloads are unchanged.

`HalStorageVolume.cpp::openNextFile` previously obtained name, size and type
from the existing `dir_next`, discarded that type, then called `Storage.open`.
That call performs `stat` followed by `dir_open` or `file_open`, walking FAT
parent directories twice after enumeration. `storage_fatfs/volume.c::dir_next`
already copies the real `f_readdir` type into `risc_storage_dirent_v1`.
The canonical storage.volume API has no open-relative or open-from-dirent
handle operation. No new one is needed to remove the redundant stat.

The repaired method retains the HAL lock and selects the existing `dir_open`
or `file_open(READ)` directly from the observed type. Both still open an actual
provider-owned child; FatFs verifies the current path and rejects the wrong
actual type. Missing entries and type swaps fail the iterator rather than
fabricating metadata-only handles. Same-type replacement remains path-based,
as before; this change does not promise stable identity across external edits.
Child sizes/content come from the opened object, not cached directory metadata.

The explicit HAL unavailable admission boundary is retained even when the
provider still reports ready. This was caught and repaired during independent
review. Parent sticky errors, EOF discrimination, child generation accounting,
checked close, uncertain-close retention and remount exclusion remain intact.
The HAL lock is not recursively reacquired.

The healthy `firmware-v1.3.53` reference (`d2d5a9a1`) used SdFat's own
`openNextFile` and retained its actual child object without this extra path
stat. This repair restores that no-extra-stat property while preserving the
newer provider, generation, lifetime and integrity capabilities.

## Operation-count evidence

An isolated host fixture compiled current PR350 production provider, FatFs,
HAL, app inventory/admission and provider/CDC enumeration code against the
existing X4 SD-wire and T5 SPI-card models. The source delta was this HAL repair.
Fixtures use the four delivered essential app packages, or those four plus the
optional Wi-Fi Settings package, and eight delivered drivers. These represent
known delivered packages, not proof of the owner's actual installed inventory.

Sector requests before -> after, identical on both transport models:

| Cold operation | Before | After |
| --- | ---: | ---: |
| App recovery inventory, four apps | 18 | 10 |
| Saved app path resolution, four apps | 132 | 106 |
| installedRefresh, four apps | 420 | 332 |
| installedRefresh, five apps | 524 | 414 |
| Actual CDC prepare + provider enumeration, eight drivers | 1,188 | 892 |

The app refresh reduction is about 21%; the provider cold path is about 25%.
Bulk-read counts/bytes, metadata checks and ELF admission work are unchanged.
Ten unchanged warm provider calls still perform zero opens/reads/sectors.
Mutation and CRC/remount observations still require fresh enumeration.

A separate synthetic 100-directory scan drops from 4,062 to 2,050 sector
requests. A direct metadata-only volume scan uses 38, but does not provide the
live child-handle semantics of HalFile and is not substituted here. Path opens
remain, so this bounded repair reduces rather than removes quadratic parent
walking. No metadata-only HAL wrapper or lazy file handle is introduced.

These are deterministic host I/O counts, not physical card times or hardware
performance measurements. They do not establish that the reported two-minute
Springboard launch is fully explained or fixed. Existing lower/upper-layer
cooperative waits and operation limits are preserved.

## Focused regression

The existing `test/storage_volume/run_test.py` runs the actual production
provider/FatFs/HAL for both board selections. Its directory scenarios cover:

- No redundant stat; files/directories, empty files, long names and actual
  read-only/hidden/system/archive FAT attributes; clean EOF.
- Deleted files/directories, both type swaps and changed same-type content
  between real dir_next and child open; sticky errors and fresh iterator retry.
- File and directory handle-slot exhaustion; retained parent and recovery.
- Parent close while a child remains live; remount refusal and later success.
- Explicit markUnavailable while the provider remains ready; no fresh child
  admission and successful cleanup/rebegin.
- Real CRC transport failure between next/open; failed iteration and retry
  after checked cleanup/remount.
- File/directory checked-close failure retains ownership, blocks remount and
  quiesce, permits checked close retry, and never certifies a reusable snapshot.

The unchanged original HAL independently fails the no-extra-stat assertion.
Local runs retain ASan/UBSan; LeakSanitizer is disabled in the existing runner
because it cannot trace the sandbox. Original full provider/mutex scenarios
remain included. The first direct storage-generation aggregate was blocked by
LeakSanitizer/ptrace; its retry with ASAN_OPTIONS=detect_leaks=0 retains the
same tests and address/undefined sanitizers. Hosted exact-head
build results belong in the PR; no physical validation is implied.
