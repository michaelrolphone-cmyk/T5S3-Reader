# Avoid repeated absent-backup pathname scans

## Identity and scope

- Repository: `michaelrolphone-cmyk/T5S3-Reader` (`1367546328`).
- Stable alias: `PERF-20261004-SPRINGBOARD-PATH-PROBES`; canonical assignment remains with the sole ledger coordinator.
- Source/target: `xteink-x4-pro-boot`, captured baseline `9a209d8794d725393ddbb48d3880c03b9af29ba4` (PR350, still unmerged through PR348).
- Branch: `perf/app-backup-probe-snapshot`; firmware `1.3.105` → `1.3.118`. No distributable package or public app/provider ABI changes.
- [Existing report](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/332#issuecomment-5977926551) and [isolated claim](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/332#issuecomment-5980463491).

## Repair

An ordinary loose-app inventory previously performed two negative backup pathname lookups for every accepted app, even though its preceding recovery traversal had already seen the entire directory. Each missing FAT pathname starts a directory scan. This repair reuses that existing traversal; it adds no second traversal, directory-name container or retained cache.

`recoverAppInventory` optionally returns a storage-generation stamp only after clean EOF, checked close, successful recovery, no case-insensitive `.bak` entry, and an unchanged quiescent storage generation. Files and directories count, including unrelated backups. Output is reset before every early return. Any recovery write prevents reuse, even when recovery succeeds.

`installedRefresh` retains the positive ELF existence lookup and checks the stamp immediately before the backup probes. An absent, stale, nonquiescent or uncertain observation takes the original probes with their original short-circuit order. Every call starts with an empty observation. Managed validation, manifest fields, sorting, app/entry bounds, 30-second deadlines, progress, scheduler yields and fail-closed publication remain unchanged.

The added scan work is at most 128 filename bytes per existing entry, with no additional heap allocation, I/O or directory traversal. Existing 1,024-entry/30-second bounds and item/time scheduler checkpoints apply. Generation comparison is a bounded in-memory observation under the existing storage lock. It detects the HAL's observed writes, writer lifetimes, remounts and external-access uncertainty; it is not a content digest or a promise against unobserved media changes. No permission or validation authority is created. Sidecar pathname opens and positive ELF stats still perform their existing work; this does not make the entire inventory linear.

The valid queried names contain multiple dots (`.elf.bak` / `.json.bak`). FatFs marks embedded dots `NS_LOSS` and excludes short-name matching for these queries, so an unrelated long filename's hidden `.BAK` short alias cannot satisfy them. Actual matching LFNs retain the ASCII-insensitive suffix, including mixed-case variants. HAL metadata rejects invalid/truncated names and exposes sticky enumeration errors.

## Verification

The existing production SD/FatFs/HAL inventory fixture is used, including real manifest parsing, recovery, resolution and installed inventory. The unchanged baseline passes its normal, managed priority/ambiguity, validation, read/close failure, deadline, bounds and retry assertions. Its 37-app workload performs 3,507 sector reads, 113 stats and 12,486 modeled milliseconds with the existing synthetic 3 ms/sector setting. These are fixture operation costs, not physical timing.

The repaired 37-app workload uses **1,260 sector reads and 39 stats**, with the same 40 opens, 170 metadata reads and 40 closes as baseline. Modeled duration falls to 4,553 ms under the unchanged synthetic timing. All 37 ordered manifests match baseline across every public field. The original source fails the new cost bound. Normal regression covers mixed-case backup files/directories, actual FAT aliases, 15 scan/end/probe mutation/writer/remount/external combinations, permanent uncertainty, recovery writes/failure, clean and failed close, entry/candidate/deadline bounds, safe fallback, retry and balanced handles.

Final repair regression results and exact-head hosted checks are recorded in the PR and terminal coordination comment. A pending CI record is not a pass. No physical SD latency, rendered UI/device qualification, full local firmware link, release, deployment or hardware operation is claimed. Local LeakSanitizer is unavailable under ptrace; ASan/UBSan remain active with caller-level `detect_leaks=0`.

The executor workspace was replaced during implementation. The exact captured baseline and reviewed production patch were reconstructed; final checks run on the restored tree. Interrupted runs are not claimed as completed.

## Coordination

Selection refreshed 407 all-state PR records, 357 live branches and 551 distinct heads, 122 historical bug-ledger blobs, both progress/workflow variants and 78 prior PR332 comments. The four observed inventory implementations retain the original backup probes. Eight unavailable local source heads were checked by exact-ref connector reads. The existing report is the provenance, not a newly discovered duplicate.

The shared ledger remains `13f6c55` with its recorded writer; Grok PR384 and other branches remain untouched. Separate image-source duplication remains queued. A safe image-source repair must retain per-occurrence pixel identity because dithering depends on absolute position; no image parsing, scene, dimension or cache-format changes are included here.

Awaiting integration, not fixed on master. No merge or release is authorized by this implementation.
