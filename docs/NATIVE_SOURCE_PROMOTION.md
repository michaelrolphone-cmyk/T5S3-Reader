# One source-only SDK 6.1 promotion

This promotion carries shared source and qualification fixes into master. It
does not release the historical standalone Reader firmware, apps or hardware
drivers merely because their versions are ahead of the independent index.

The manifest `native-sdk61-source-promotion.json` pins the current master
`b5a9887e16ce04baecdfa8366ff7d67a2a22c3cc` and the publicly verified source
`40ee294e8d16280f14bbe56a92103e6038a316ea`. Suppression applies only to a push
to master from that exact old head to a direct, single-parent successor of the
source. That successor must change exactly the six workflow/helper/test/doc
files listed in `scripts/check_source_only_promotion.py`, all regular files.
The manifest, ancestry, event identities and clean checkout are checked offline.
Any mismatch for that designated first promotion fails before release planning.

The plan job saves an empty plan plus an auditable source-only result; the
publish job explicitly skips that result. Native shared-component qualification
continues normally. No tag, release, catalog or release-index mutation occurs.
The manifest is not a persistent disable switch: subsequent master pushes have
a different `before` SHA and follow the unchanged normal planner. Manual
dispatch also retains normal standalone release behavior and its usual
publication consequences. Rewinding master to the old head is not authorized.

The source-only MSC exclusion is separate. It continues to validate the complete
module graph while leaving the explicit native/historical MSC builder available
for qualified product composition. This guard does not change its package ABI,
versions, source files or product ownership.
