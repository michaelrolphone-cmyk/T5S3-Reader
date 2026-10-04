# BUG179: Read XTC metadata from its advertised location

## Identity and scope

- Repository: `michaelrolphone-cmyk/T5S3-Reader`
- Canonical key: `michaelrolphone-cmyk/T5S3-Reader::BUG-179`
- Source and target: `master`, baseline `cff6ef0c11b79634dfa2c688d880df393d8c153c`
- Repair branch: `fix/bug179-xtc-metadata-offset`
- Owner: `fix_small_bug_0930`
- [Durable isolated claim](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/332#issuecomment-5978624026)
- Firmware: `1.3.82` → `1.3.111`, beyond coordinated parallel reservation `1.3.110`. No separately distributed package changed.

The canonical ledger at `13f6c55cf482738f583efc58cd922e0afe835108` remains under its existing single writer. This repair changes no shared ledger or other owner's branch. Integration status is awaiting the repair PR and exact-head hosted checks; see the PR and its terminal coordination comment for the latest status. No merge, release, deployment or physical-device action is authorized by this record.

## Defect and repair

`XtcHeader::metadataOffset` already identifies the optional metadata block. The old complete production parser instead read title and author at absolute offsets `0x38` and `0xB8`, ignored short reads, and could retain partial metadata after an unsuccessful open. A generated otherwise accepted XTC with its metadata at `0x100` returned `OK` with deliberately different decoy strings from the fixed addresses. The regression fails against unchanged baseline source with `metadata title ignored advertised offset`.

`readMetadata()` now validates the 192-byte title/author region actually consumed, using subtraction before addition to prevent 64-bit overflow. It seeks with `seek64()` to the declared location, performs exact 128-byte and 64-byte reads into fixed buffers with extra terminators, and publishes neither string unless both reads succeed. It preserves the existing string-capacity trimming. It does not inspect or require unused trailing metadata fields. The legacy 48-byte header floor remains supported. No-metadata files ignore the metadata offset.

Every new open resets previous partial state, and open errors close/reset the parser while retaining the actual error. Ordinary page-table, page and streaming behavior are unchanged. `Xtc::load()` reaches this parser and exposes its metadata through `getTitle()` and `getAuthor()` to Reader/Recent Books; those caller paths were source-traced, not executed by this test.

## Verification

`bash test/run_xtc_metadata_test.sh` compiles the **complete production** `XtcParser.cpp` and production headers with deterministic storage/logging endpoints. `XTC_SANITIZE=1` enables ASan/UBSan. The existing Springboard aggregate invokes the sanitized regression; it does not disable hosted leak detection.

Covered cases:

- Original relocated-metadata failure with fixed-address decoys; contiguous and legacy placement; metadata at EOF; offsets above 32 bits; XTC and XTCH.
- Unchanged normal page bytes/dimensions, streaming chunks, empty and absent metadata, UTF-8 and maximum-width non-NUL fields.
- Zero/header-overlapping/out-of-file/near-`UINT64_MAX` offsets, truncated title/author regions, failed seeks, zero/short/negative header/metadata/page-table reads.
- Failure after success, repeated failure, metadata-free retry, explicit repeated close and balanced source handles.

Locally passed: focused normal and ASan/UBSan; original-source negative test; independent read-only review and rerun; full Springboard aggregate; package-source version guard; shell syntax and whitespace; 32-bit Xtensa production-parser translation-unit compile with fixture endpoints. NativeApp aggregate and hosted exact-head firmware/host checks are recorded in the final PR checkpoint when complete.

Limits: the storage fixture models read/seek/open failures and sparse 64-bit addressing; it is not a physical SD or provider-volume test. No full local firmware link/build, real-card corruption test, real book-library sample or device rendering qualification is claimed. Local LeakSanitizer is disabled through caller environment due to the executor's ptrace restriction; ASan/UBSan run. Existing unrelated page/chapter/buffer validation findings are outside this repair.

## Deduplication

The selection pass refreshed 400 PRs across all states, 350 branches, 52 coordination comments, current canonical inventory/progress/workflow and issue searches. Across 543 distinct heads, all 542 source-bearing heads had identical XTC parser/header/types blobs; one release-index head had no source. Grok's protected `4785443e62ca050facf38b3f68982f321c7afda8` was checked read-only with exact file retrieval. No matching implementation or claim was found. Closed-unmerged prior fixes, current CSS/ZIP optimization and Runtime native-radio work remain independently owned.
