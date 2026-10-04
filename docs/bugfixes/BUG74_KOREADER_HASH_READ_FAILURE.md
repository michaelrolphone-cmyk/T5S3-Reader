# BUG-74: refuse incomplete binary document hashes

- Repository: `michaelrolphone-cmyk/T5S3-Reader` (ID `1367546328`).
- Canonical key: `michaelrolphone-cmyk/T5S3-Reader::BUG-74`.
- Baseline / target: `master`, `cff6ef0c11b79634dfa2c688d880df393d8c153c`.
- Branch: `fix/bug74-koreader-hash-read-failure`.
- Owner: `fix_small_bug_1230`.
- [Isolated claim](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/332#issuecomment-5979976490).
- Firmware: `1.3.82` → reserved `1.3.117`; no distributable package changed.

## Defect and repair

`KOReaderDocumentId::calculate()` skipped failed seeks and accepted partial reads
of required samples. A nonempty partial digest could therefore become a different
remote document identity. A negative `HalFile::read()` result was also widened to
`size_t` before the hash-builder call.

The repair returns the existing empty failure result on any unsuccessful seek or
read count other than the requested sample length. The read result remains signed
until validated. No digest is finalized or published after an incomplete sample.
The existing sync caller presents `STR_HASH_FAILED` before requesting progress.

Healthy offsets, bytes and filename matching stay unchanged, including empty-file
behavior and the final sample ending at EOF. The existing loop remains limited
to 12 samples, at most 12 KiB total and a 1 KiB stack buffer, with no new retries.
The production `HalFile` destructor retains its existing cleanup semantics; the
repair introduces no handle ownership or transport policy change.

## Verification

- Original production source independently fails seek, positive-short, zero and
  negative-read regressions.
- 471 normal and ASan/UBSan cases compile the complete production hash unit and
  exercise the verbatim sync dispatch/failure guard. They verify healthy exact
  sample input/offsets at all boundaries through a synthetic file above 1 GiB,
  every reachable sample failure, immediate stop, repeated failure, recovery,
  handle cleanup, stale-ID removal, no progress request after failure, and
  unchanged filename mode.
- The source also compiles to a 32-bit Xtensa object.
- Package source-version guard, Python/shell syntax and whitespace checks pass.
- Full local aggregates, independent review and exact-head hosted checks are
  recorded in the PR and final coordination checkpoint when terminal.

Commands:

```sh
python3 test/koreader_document_id/document_id_test.py
python3 test/koreader_document_id/document_id_test.py --sanitize
bash test/run_native_app_test.sh
bash test/run_springboard_test.sh
```

Storage, MD5 and UI/network endpoints are fixtures. Exact MD5 input and finalization
control are checked; the MD5 algorithm itself is not tested by the host fixture.
Local LeakSanitizer is disabled by the caller for ptrace, while ASan/UBSan remain
active; CI defaults are unchanged. No physical SD, account/network request, full
local firmware link or hardware qualification is claimed. No default-branch write,
merge, release, deployment or device operation is part of this repair.

The shared ledger branch and its recorded writer remain untouched. Publication
stays **Awaiting merge** until integration is independently established.

Rolled into [#384](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/384) on `fix/consolidated-bugs-20261003-c`.
