# Reader BUG-67: OPDS relative references

## Identity and ownership

- repository_full_name: `michaelrolphone-cmyk/T5S3-Reader`
- canonical_key: `michaelrolphone-cmyk/T5S3-Reader::BUG-67`
- owner: `fix_small_bug_0030`; claim published 2026-10-04 00:34 UTC
- source_ref: `master`
- baseline_sha: `cff6ef0c11b79634dfa2c688d880df393d8c153c`
- target_branch: `master`
- repair_branch: `fix/bug67-opds-relative-urls`
- target_pr: <https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/391>
- file_paths: `src/util/UrlUtils.cpp`, `src/util/UrlUtils.h`, `test/util/url_resolution_test.cpp`,
  `test/util/opds_url_flow_test.cpp`, `test/util/url_resolution_test.py`,
  `test/run_springboard_test.sh`, `platformio.ini`, this checkpoint
- Claim: active for publication/exact-head CI. The PR body and terminal coordinator comment
  record final remote SHA, checks and claim release without changing the tested source head.

## Selection and reproduction

The current production helper resolves `books.xml` against
`https://example.test/opds/root.xml` as
`https://example.test/opds/root.xml/books.xml`. Expected sibling path:
`https://example.test/opds/books.xml`. The helper regression fails with those
exact values on the baseline. A separate negative control executes the original
browser navigation method and fails its first nested-feed URL assertion.

`HomeActivity::loop` reaches `ActivityManager::goToBrowser`, which constructs the
existing OPDS browser. Its navigation and acquisition methods already provide
the current feed as the base. Only those browser paths call `buildUrl`.
The existing default-ELF Reader entry retains that UI; this repair does not change
its entry point, the OPDS app settings package, or any independently distributed app.

Selection checked all 390 PR records and 47 open/closed-unmerged changed-file
inventories at 00:33 UTC, including PR350/384/386 and historic PR69. All 339
source-bearing remote refs had identical UrlUtils source/header blobs. Relevant
issue and coordinator-claim searches found no duplicate repair. Existing fixes
remain owned and untouched. No general scan or new canonical report was created.

Both default branches were rechecked. RiscRTE `main` remains the README-only seed
`88a29aee6fb414613802448f83f5f19a470ac59c`. PR1 is open at
`e27d3d089086d79f06edeff4c2bd35f6e6243444`; the authorized PR2 implementation
source/base remains `test/watch-app-handoff` at
`a3d23da9cdc1b3a66c6429f29781856fa7fc8f75`. PR3 remains owned at
`fe9d3c877ac98e52670d458f45b59fda8011a4fd`. Runtime tree/provenance/source-map
inspection establishes no OPDS helper migration. No runtime source was edited.

## Repair

The existing helper now splits URI components before reference resolution.
Document-relative paths replace the final base segment; directory, root and
network-authority references retain their defined meanings. Literal path dot
segments normalize without decoding escaped bytes or collapsing repeated slashes.
Absent query/fragment components remain distinct from explicit empty delimiters.
An empty reference drops the base fragment; fragment-only references retain its query.
Bare-host server configuration retains the existing HTTP default.

The implementation follows [RFC 3986 section 5.2](https://www.rfc-editor.org/rfc/rfc3986#section-5.2).
Input is consumed through string views, with no front-erasing/rescanning of the
remaining string or recursion. Work and owned string storage are proportional to
input length. No network, filesystem or asynchronous operation is introduced.
No URL admission, redirect, authentication, TLS, parser lifecycle or search-template
policy changes are included. This resolver does not validate a URL for transport.

Firmware source advances `1.3.82` to `1.3.96`, above the live PR350 reservation
`1.3.95` and PR384 `1.3.94`. No separately distributed package changes.

## Verification

- `python3 test/util/url_resolution_test.py`: PASS, actual production helper
  linked under strict C++17 warnings and actual browser methods extracted verbatim.
- `ASAN_OPTIONS=detect_leaks=0 python3 test/util/url_resolution_test.py --sanitize`:
  PASS for both suites. Local LeakSanitizer is unavailable under the executor's
  ptrace; this does not disable ordinary CI leak checks.
- Both `--test helper` and `--test flow` fail against the original baseline with
  the expected sibling-link defect; compilation succeeds before the failure.
- Helper coverage: canonical sibling navigation/acquisition, all RFC3986 section5.4
  normal/abnormal vectors, trailing-directory/root/bare-host/IPv6 bases, query and
  fragment delimiters, URLs inside queries, literal dot segments, repeated slashes,
  percent-encoded separators/dots, UTF-8 bytes, repeat calls and 2000 dot pairs.
- Production browser-method coverage: initial/nested feed, query-only pagination,
  relative download, download refusal/retry, fetch refusal/retry, parser-invalid
  result/retry, Back history, Home, empty-server error and exit cleanup.
- Full `ASAN_OPTIONS=detect_leaks=0 bash test/run_springboard_test.sh`: PASS,
  including the newly integrated sanitized URL suite.
- `scripts/check_changed_package_versions.py --base cff6ef0c11b79634dfa2c688d880df393d8c153c`,
  Python/shell syntax and `git diff --check`: PASS.
- Independent read-only review: normal and ASan/UBSan tests passed; no implementation
  defect found in component semantics or proportional processing.
- Native-app aggregate and hosted exact-head target/host builds: pending at this
  checkpoint; final PR body records terminal results.

HTTP, parser outcomes, UI, cache and networking shutdown are fixture boundaries in
the browser harness. Real XML parsing, remote OPDS servers, redirects, device UI,
physical SD/network behavior and hardware were not tested. No local full firmware
build is claimed. The focused helper and production method tests are not a complete
browser or network integration qualification.

## Coordination

Durable claim: [PR332 comment](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/332#issuecomment-5975029264).
The shared `automation/bug-ledger` head remains
`13f6c55cf482738f583efc58cd922e0afe835108`, with recorded writer
`task11_wrap42_20261003` untouched. Sole-writer reconciliation is queued; this
per-bug checkpoint is not another inventory. Publish only with captured current
repair parent and a non-force fast-forward update. No default-branch write, merge,
release, deployment, device action or unrelated branch change.
