# X4 hardware CI target

The GitHub-to-Mac pilot merged in #347 covers CAM. This increment adds an
independent Xteink X4 Pro target for MAC `84:C7:BB:79:E2:AC`, without changing
CAM's scripts, timer, firmware restoration, or hardware status. It does not
claim coverage for every other attached ESP board. T5S3 remains excluded and
its backup/restore exception is preserved.

## Current readiness

**Software/dry-run support only. X4 live execution is disabled.** Automatic
execution review rejected an actual X4 firmware write in PR #350 despite the
owner's authorization. That execution approval path has not been resolved.
This controller cannot activate X4, open/reset a port, flash through a different
worker, or post an X4 hardware success. No credential scope, permission,
guardrail, installed service or repository setting is changed.

Current master `3722a3f44a3294ba5e8adab830807a2523df3b03` does not contain the
`xteink-x4-pro` environment. Its firmware/providers/UI belong to #350/#348.
`x4-hardware-build.yml` runs host contracts on each PR and, when that exact
head includes the environment, builds it on GitHub-hosted Ubuntu. An absent
environment produces an explicit summary and **no artifact**, never an EPD47,
T5S3 or CAM substitute. A green workflow without an X4 candidate is not X4
firmware readiness. This PR changes no distributable package and needs no
firmware/app/driver version bump.

## Trust and artifacts

Install only a reviewed, pinned copy of `test/hardware/trusted_targets.py`,
`x4/contract.py`, and its existing CAM dependencies. Never execute a PR checkout
or scripts supplied in the artifact on the Mac. This change does not install
anything. The existing separately pinned CAM controller remains operational.

The scanner reuses CAM's owner-authored open same-repository exact-head policy,
and verifies the X4 workflow ID/path, repository IDs, owner run actors, PR
association, successful run, run attempt, artifact name, bounded ZIP entries,
manifest and firmware SHA-256. It checks the head again after download.
Artifacts contain only `riscrte-xteink-x4-pro.bin` and `manifest.json`; duplicate,
extra, traversal and symlink entries are refused. The ESP32-S3 app header and
X4 board marker must match. Offset is app-only `0x10000`, bounded by `0x640000`;
this is **not proof of the attached device's partition layout**. No merged
image, partition rewrite or recovery redesign is included.

The scanner requires `--dry-run` and accepts `--targets cam x4` (both by default),
`--pr NUMBER` or `--scan`, plus `--evidence-root PRIVATE_DIRECTORY`. It uses an
already supplied `GH_TOKEN`; it does not obtain, print or change credentials.
No setup command in this document enables device testing. Optional `--report`
posts `X4 candidate / dry-run` or `CAM candidate / dry-run`, with explicit
“hardware not run” wording. These contexts must not be confused with
`CAM hardware / trusted owner SHA` or used as physical qualification.

## Scheduling, identity and evidence

At most 20 owner PRs and two targets are inspected in one scan, with a 180-second
between-item deadline and bounded HTTP calls inherited from the CAM controller.
Each dry-run target has its own exact-SHA journal below `dry-run/<target>/<sha>`.
A target failure does not skip the other target. An exclusive scheduler lock
prevents duplicate concurrent passes using that evidence root. Completed
journals retry only a missing status post, never device work. Network/rate-limit
failures before acceptance retry later. An incomplete journal stops for
inspection. A rejected terminal artifact stays rejected for that SHA; after a
corrected same-SHA cloud rerun, a maintainer may explicitly archive its dry-run
journal after inspection. No automatic deletion of live evidence occurs.

The reusable lock primitive uses the **same existing lab directory and hashed
`port:` / lower-case `mac:` keys** as CAM and the X4 owner, including macOS
`cu`/`tty` aliases. Tests exercise cross-process contention and partial cleanup.
It is prepared for a future reviewed adapter; dry-run scanning needs no device
lock and performs no device access. Before future port access, locate a unique
native USB descriptor (`303a:1001`, serial MAC), hold the shared locks, then
independently verify ESP32-S3 chip/MAC and the privately mapped X4 Pro model.
A port path, USB product name or ESP32-S3 chip alone cannot prove board model.

The bounded serial parser consumes an already-open stream with a required
one-second-or-shorter read timeout; a future live adapter must enforce that
transport contract and overall termination. It retains only test states/counts,
commit and firmware digest, never raw serial, storage data, secrets or image
bytes. It observes up to 90 seconds / 256 KiB / 2,000 rows / 512 bytes per row,
rejects panic/reboot/disconnect/overflow and reports boot, panel, storage and
input independently. Panel completion is **serial/provider evidence**, not
optical quality. No camera visibility prerequisite exists.

The boot/panel/storage patterns follow #350 diagnostics. The input event
contract (`input.navigation event=left|right|confirm|back sequence=N`) is not
emitted by the inspected #350 firmware; input stays missing until firmware
integration supplies actual event evidence. Provider load alone is not an
input pass. `storage.volume mounted=0`, absent storage, and absent physical
input cannot pass. CAM heartbeats/capture cannot satisfy any X4 assertion.
Parser fixture success is only a host test; supplied SHA/digest must eventually
be bound by the live adapter to verified candidate readback and device identity.

## Remaining physical integration boundary

After the real execution approval path is resolved, a separately reviewed,
pinned adapter must bind identification, shared locks, unchanged partition/NVS/
OTA guards, exact candidate readback and bounded diagnostic capture to the
controller. No live adapter is shipped here. The owner allows test firmware
overwrite without mandatory backup/restore for X4 and other ESP test boards,
**except T5S3**; this does not change CAM's existing restoration behavior or
authorize destructive partition changes. The current result is dry-run
readiness, not physical boot/panel/storage/input qualification.

## Verification

Run:

```sh
python3 -m unittest discover -s test/hardware -p test_targets.py -v
python3 -m unittest discover -s test/hardware/cam -p test_trusted_controller.py -v
```

These checks use in-memory artifacts, fake GitHub/serial responses and temporary
lock directories. They do not access attached devices or build firmware locally.
