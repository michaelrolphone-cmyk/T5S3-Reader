# Reader BUG-67: OPDS relative references

Per-bug claim — 2026-10-04 00:34 UTC

repository_full_name: michaelrolphone-cmyk/T5S3-Reader; canonical_key: michaelrolphone-cmyk/T5S3-Reader::BUG-67; owner: fix_small_bug_0030; source_ref: master; baseline_sha: cff6ef0c11b79634dfa2c688d880df393d8c153c; target_branch: master; repair_branch: fix/bug67-opds-relative-urls; target_pr: pending draft creation. Paths: src/util/UrlUtils.cpp/.h, test/util URL regression and existing aggregate, platformio.ini; durable checkpoint docs/bugfix/BUG-67.md.

Production-source baseline reproduces ordinary valid OPDS sibling links resolving beneath root.xml: expected https://example.test/opds/books.xml, actual https://example.test/opds/root.xml/books.xml. Existing browser callers already supply the current feed as base. Repair is confined to URI-reference resolution in the existing helper, including containing directory, authority/root references, dot segments, query/fragment semantics and focused original-fails/fixed-passes tests. No network, authorization or parser-lifecycle changes planned.

All390 PR metadata,47 open/closed-unmerged changed-file inventories,339 source-bearing UrlUtils remote refs and relevant issue/claim searches show no competing fix; UrlUtils blobs are identical on every source-bearing ref. Existing PR350/384/386 and migrated app work remain untouched. Runtime main88a29aee6fb414613802448f83f5f19a470ac59c remains a seed; authorized PR2 source a3d23da9cdc1b3a66c6429f29781856fa7fc8f75 and PR3fe9d3c877ac98e52670d458f45b59fda8011a4fd contain no OPDS URL ownership.

Reserve Reader firmware1.3.96 beyond live PR3501.3.95. No independently distributed app/driver/provider changes. Shared automation/bug-ledger13f6c55cf482738f583efc58cd922e0afe835108 and recorded task11_wrap42_20261003 writer/claim remain unchanged. No competing tracker, default-branch write, merge, release or device action.
