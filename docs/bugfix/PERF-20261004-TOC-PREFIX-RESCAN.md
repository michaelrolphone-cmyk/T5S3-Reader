# TOC prefix-rescan repair checkpoint

- repository_full_name: michaelrolphone-cmyk/T5S3-Reader
- stable source alias: michaelrolphone-cmyk/T5S3-Reader::PERF-20261004-TOC-PREFIX-RESCAN
- canonical_key: pending sole-ledger-coordinator assignment
- owner: fix_reader_performance_0610
- source_ref / target_branch: xteink-x4-pro-boot
- baseline_sha: 60e5a78b79e5a196e2265aa8bad3200af5a82479
- source_pr: https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/350
- repair_branch: perf/toc-prefix-rescan
- target_pr: pending draft creation
- file_paths: lib/Epub/Epub/BookMetadataCache.cpp/.h; test/epub_toc_index/; test/run_springboard_test.sh; platformio.ini
- report: https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/332#issuecomment-5977145950
- durable claim: https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/332#issuecomment-5977265265
- firmware reservation: 1.3.106 (1.3.105 reserved independently by the PR350 owner)
- phase: implementation and local verification; exact-head hosted CI pending

PR350 and PR348 remain open; master is cff6ef0c11b79634dfa2c688d880df393d8c153c. The approved temporary working-branch base applies. Neither owner branch is modified by this repair. Current source deduplication covered 742 branch/PR-head refs with nine distinct BookMetadataCache blobs; all retained the small-spine prefix loop. Fresh claim/report and all-state TOC/updated-PR searches found no competing repair. Existing BUG173 TOC fallback and BUG261 path resolution remain distinct.

The shared ledger stays at13f6c55 with its recorded writer. This per-repair checkpoint queues consolidation without changing canonical IDs or taking over that writer. No merge, release, deployment, firmware validation, device operation or hardware claim.
