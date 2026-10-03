# Canonical bug 241: Footnotes viewport repair

This is the evidence/checkpoint for this repair only, not another bug inventory.
Canonical IDs and status remain owned by `automation/bug-ledger:bugs.md` and
`docs/BUG_FIX_PROGRESS.md`. Reconciliation into that shared ledger is queued for
its existing owner; this task does not edit the ledger or the old bug42 branch.

## Ownership checkpoint — 2026-10-03 UTC

- Owner: Codex task `01a101f8-0446-7200-bc2f-5fea7fe4c030`.
- Canonical ID: **241**, “EPUB Footnotes overcounts visible rows and can move
  selection below the screen before scrolling.”
- Source provenance: scan `f8b56224203ffb83b2ee445efe0226fca534918a`, local 209.
- Authoritative ledger read at `13f6c55cf482738f583efc58cd922e0afe835108`.
- Current master / branch baseline: `3d9bc4f373679f5ae8dd184db6a8d0afa5a40231`.
- Isolated branch: `fix/bug241-footnotes-viewport`; target: `master`.
- The current user instruction explicitly permits independent repair branches
  while the bug42 ledger owner is unresponsive. Only shared-ledger publication
  is deferred; no takeover of that owner or its branch is attempted.
- Checked all 44 open and closed-unmerged PR file lists, recent merged PRs,
  all fetched branch history for this activity, and all-state Footnotes search.
  No other PR or branch changes `EpubReaderFootnotesActivity.cpp` or its header.
  The activity's only history is the initial commit. Excluded #373–#376 and
  their source repairs, all active Watch/shared Springboard work, and bug64.
- Source still draws rows from y=60 (110 in inverted portrait), but calculates
  row capacity from y=0 (50 inverted). At landscape height 540 it allows 15 rows;
  row 14 starts at y=564. Production-source reproduction is the next checkpoint.
- Reserve firmware **1.3.78**: master/published firmware is 1.3.69;
  all fetched branch versions reach 1.3.77 on X4 #350, 1.3.76 on #375 and
  1.3.75 on #374. Release-index checked at
  `8093916fb6510e65b798e7218e5e8821d5314602`. No separately released package
  source changes are planned. Revalidate before publication.
- Root/platform/roadmap/workflow/cooperative instructions read. No `.agents`
  directory or applicable child instructions exist in the affected source/test
  paths. No app migration, hardware, release, merge, or master write is in scope.

## Verification and continuation

- Baseline regression FAIL: the original activity draws rows beyond the list's
  viewport (`row extends outside the Footnotes viewport`, exit 1). Test uses
  the complete production class and methods; input, renderer and framework
  boundaries are fixtures. Reader menu reachability is confirmed at
  `EpubReaderActivity.cpp`'s FOOTNOTES action and `FootnoteResult` handler.
- Fix: calculate capacity from the true list top and portrait button-hint bottom,
  share it between rendering and hit-testing, accept only complete rows, reset
  scroll on entry, and draw no rows if the viewport cannot fit one.
- PASS locally: strict C++17 regression, 48 orientation/size/list combinations,
  complete forward/back wrapping, first/last touch pixels, title/partial-row/hint
  rejection, correct target returned, cancel/reopen, display rotation, empty
  lists and tiny viewports. Package-source version guard, adjacent native UI,
  Home shortcut/global-overlay contracts, Python/shell syntax and diff checks pass.
- Local ASan/UBSan executable timed out after 30 seconds; this is not a pass.
  The regression is integrated into the existing Springboard aggregate with
  sanitizers for Linux CI. Full local board/aggregate builds are deferred to
  CI to avoid competing with higher-priority tasks. No device operation or
  physical input/display qualification is claimed.
- Next: refresh master/claims/version reservations, push this isolated branch,
  create one draft PR and inspect/repair exact-head CI. The shared ledger update
  remains queued for its existing owner.
