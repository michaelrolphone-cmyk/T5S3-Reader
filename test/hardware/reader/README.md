# CI-only Reader serial staging checkpoint

This is a test utility, not a production shell or a hardware-controller job.
Nothing here opens a port, flashes a device, or starts a network listener by
default. Normal PlatformIO profiles do not compile this directory.

## Architecture correction: deployment suspended

The first instrumented build used frozen production base
`ec0c09991f8f7babc69b3da0681b0b3f1e99c3ab`. It compiled successfully, but **must
not be deployed**: the owner subsequently required X4/T5S3 ordinary driver ELFs
on SD, replacing the previous internal module-store boot path. Retarget the
preparer, handshake and custody checks to the corrected same-head SD bootstrap
artifact before hardware use. Do not erase the existing internal store or
reuse its provisioning runner. No-SD Watch/CAM are separate targets.

The utility only calls already-initialized `Storage` and the ordinary package
installer. It has no raw controller, filesystem, provider-handoff or module-store
implementation. This keeps the transfer code reusable with the corrected route.
The handshake's store digest describes the historical expected layout, not an
on-device attestation. Its removal/replacement is part of retargeting; do not
accept it as evidence of the corrected layout.

## Implementation footprint

`prepare_x4_serial_staging.py` instruments a separate clean pinned checkout.
It adds a macro-guarded single RX hook and active-session sleep guard only in
that checkout, generates `StageIdentity.h`, and enables a local CI build profile.
`reader/build.py` links the test-only library. The production main/profile on
this branch remain unchanged. Preparation records the production base, utility
source/build ID and source hashes; the resulting firmware digest is separate.

`SerialStaging.cpp` waits for `Storage.ready()` and uses `HalFile` operations;
it never begins/rebinds storage or releases platform provider leases. Publication
uses `ScopedPackageMutation` and `installOrdinaryFromSdZip`, including its existing
use lease, ABI, capability, manifest, hash and lineage transaction checks. It does
not spoof a production manager caller or launch an application.

`x4_serial_staging.py` is a host library requiring an already-owned channel.
The surrounding device runner still needs implementation: fresh identity and
shared locks, exact app-only image readback, protected-region checks, durable
host backup, finally-path heartbeat cleanup and final evidence. There is no live
CLI or scheduler hookup yet. The host `stage` call requires a successful durable
backup callback before its first mutation.

## Wire contract

UTF-8/ASCII newline frames start `RTE_STAGE_V1 ` and contain JSON. Requests have
exact keys `session`, `sequence`, `op`, `args`. Responses have exact keys
`session`, `sequence`, `ok`, `result`. Sessions use a 128-bit host nonce; sequence
starts at 1, strictly increases and is bounded to 512. Duplicate JSON keys are
rejected using the existing `PackageJsonGuard`. The utility source/build ID is
embedded before compilation; it is not a self-referential firmware hash.

Only these operations exist:

- `hello`: require the compiled source/layout/build identity and initialized storage.
- `inventory`: fixed two app roots plus `.home_apps`, returning a snapshot token,
  exact file hashes and absent/matching/conflict state. Ambiguous transaction
  markers, extra files or legacy flat target pairs cause conflict.
- `read`: a fixed selector (`pins`, or an allowlisted app ID/file), offset and
  count. No caller-supplied filesystem path.
- `begin`, `chunk`, `install`: exactly the pinned Driver Manager 1.0.8 and
  App Store 1.0.8 archives. Fresh session-owned `.part`, contiguous 512-byte
  chunks, per-chunk SHA, checked close, whole-file readback SHA, rename to an
  absent immutable `.rte.zip`, then the normal package installer. Return the
  actual installer failure, including cleanup-pending; do not claim success.
- `pins`: verify the original presence/hash, verify both installed packages,
  append only missing exact ELF basenames, then publish through checked temporary
  file/backup renames. A durable fixed-path journal makes interrupted publication
  fail closed on a subsequent inventory. Previous pin bytes remain recoverable.

There is no arbitrary deletion, command execution, reset, driver activation or
default-app selection operation. Legacy flat app migration is deliberately not
implicit: preserve/back up and resolve it before using the serial installer.

Per-package publication is atomic under the existing transaction, but the two
packages are not a single transaction. A missing ACK stops the host permanently
for that session; it never blindly retries a mutation. Reconnect and inventory
first. Incomplete private uploads remain private; ambiguous package/pin journals
require explicit reconciliation. No unrelated files or guessed recovery state
are purged. Successful immutable inbox archives and original pin backups remain.

Bounds: 3072-byte frame, 512-byte payload, 2 MiB received per session, 512 requests,
180-second session and 30-second inactivity. RX handles at most 256 bytes or 4 ms
per loop. Files are at most 64 KiB; pins at most 16 KiB/128 entries. Hash/copy loops
yield every 512 bytes with a ten-second deadline; lower storage operations use
the existing provider's bounds. Normal screenshot dispatch remains available
outside an active session. The host requires <=1-second channel I/O timeouts and
has a 15-second response deadline. No unbounded diagnostic capture is retained.

## Validation

Run `python3 -m unittest discover -s test/hardware -p test_x4_serial_staging.py`.
Compile/run `reader/stage_protocol_test.cpp` with a C++17 compiler. Tests exercise
real framing/pin primitives and host failures: stale sessions, partial writes,
lost install acknowledgement without replay, hash mismatch, path injection,
content conflict, backup failure, pin compare-and-swap, preserved bytes and
same-package idempotency. The host fake peer is not a hardware/filesystem proof.

The initial instrumented X4 build passed. Hardware staging via this utility,
power-loss recovery, provider busy/use-lease integration and the corrected
SD-resident bootstrap remain unverified. The temporary card-reader staging is
separate evidence and does not qualify this serial endpoint.
