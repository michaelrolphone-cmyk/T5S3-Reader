# X4 app-only hardware CI

The cloud `X4 hardware candidate` workflow checks out the **raw PR head**,
builds only `env:xteink-x4-pro`, and uploads `firmware.bin` plus a manifest
containing the checkout SHA, workflow run/attempt, image size and SHA-256.
GitHub's ordinary PlatformIO PR matrix may build a synthetic merge commit;
its artifact is not substituted for this exact-head candidate.

The personal Mac has no self-hosted GitHub runner. A separately installed,
reviewed and hash-pinned local controller polls GitHub every 60 seconds using
the existing CAM scheduler and narrowly scoped status credential. It accepts
only an open, owner-authored, same-repository PR with one successful exact-head
X4 candidate run and a bounded matching artifact. External/fork PRs and
ambiguous runs fail closed. It reads only the app image and manifest from the
ZIP; repository Python and workflow edits do not execute on the Mac until a
reviewed controller update is installed. The owner account and its repository
branch remain the source trust boundary for firmware that runs on the X4.

The device suite identifies USB serial/JTAG MAC `84:c7:bb:79:e2:ac`, location,
16 MiB flash, partition table hash and active app0. It verifies the existing
private 16 MiB recovery backup, takes a private prewrite app backup, and writes
only app0 at `0x10000` when the rounded erase range fits below `0x650000`.
It verifies the app image and unchanged bootloader, partition table, OTA and
NVS hashes. A failed candidate check attempts to restore the prewrite app,
verify it and observe steady boot. The X4 and CAM use shared device lock keys;
an occupied X4 lock defers the run. A private pause file lets the owner hold
the X4 for interactive work. No relay, hub power, RF, buzzer or other board is
actuated. Serial reset is not a true power-cut test.

The bounded boot check requires one diagnostic entry, board marker, mounted
storage, startup present, 480×800 Home geometry with positive menu height,
one Home present and at least three ready heartbeats without panic. It does
not claim the panel is visually correct, that buttons work, or that TXT
browsing succeeds; those still need physical qualification. Only numeric and
hash evidence is retained privately, and the separate commit status
`X4 hardware / trusted owner SHA` reports this narrow result on the exact PR
head. Images and arbitrary SD/serial content never enter Git, Actions
artifacts or the status. The status is advisory unless the owner separately
configures a merge rule.

Each SHA has one private journal. A terminal status is not run twice. An
interrupted transaction fails closed and requires inspection; only a lock-busy
preflight is safely retried. The active Mac copies of controller/device code
are pinned by SHA-256 in the scheduler, separate from the PR checkout.
