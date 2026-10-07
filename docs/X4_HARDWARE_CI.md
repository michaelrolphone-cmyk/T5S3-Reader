# X4 hardware CI target

The GitHub-to-Mac pilot merged in #347 covers CAM. This increment adds an
independent Xteink X4 Pro target for MAC `84:C7:BB:79:E2:AC`, without changing
CAM's scripts, timer, firmware restoration, or hardware status. It does not
claim coverage for every other attached ESP board. T5S3 remains excluded and
its backup/restore exception is preserved.

## Current readiness

**Controller and adapter implemented; X4 live execution is disabled.** Automatic
execution review rejected an actual X4 firmware write in PR #350 despite the
owner's authorization. That execution approval path has not been resolved.
The public X4 hardware controller, adapter entry and physical transport
constructor reject before device access. The shipped CLI remains dry-run-only;
there is no supported manual-enable flag or configuration. No X4 physical test
or hardware success has been produced. No credential scope, permission,
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
`device_locks.py`, `x4/contract.py`, `x4/adapter.py`, and its existing CAM dependencies. Never execute a PR checkout
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
The adapter holds it through identification, app write/readback, diagnostic
observation and serial cleanup; dry-run scanning needs no device lock and
performs no device access. Before future port access, locate a unique
native USB descriptor (`303a:1001`, serial MAC), hold the shared locks, then
independently verify ESP32-S3 chip/MAC and the privately mapped X4 Pro model.
A port path, USB product name or ESP32-S3 chip alone cannot prove board model.

The bounded serial parser consumes an already-open stream with a required
one-second-or-shorter read timeout; the adapter configures a one-second pySerial read timeout, ten-second write
timeout, exclusive port open, and a 600-second outer process alarm. It retains only test states/counts,
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
Parser fixture success is only a host test. The adapter binds the supplied
SHA/digest to accepted artifact bytes, two chip/MAC checks, exact app readback,
unchanged metadata and the same locked serial session before hardware success
is eligible. Simulation results cannot satisfy the hardware success predicate.

## Adapter and remaining execution boundary

`x4/adapter.py` implements the app-only transaction and esptool/pySerial
transport using the same pinned API versions as the CAM pilot (esptool 4.5.1,
pySerial 3.5). These dependencies are not installed or changed by this PR.
The native USB binding includes the fixed model/MAC and a reviewed exact
partition-table SHA-256 in a private non-symlink JSON file. Descriptor discovery
is bounded and unique; the port path is only a locator. Serial exclusivity is
set before opening, and DTR/RTS start inactive. Changing/disappearing descriptors
fail closed; runtime USB reconnect is bounded to ten seconds.

Before writing, the adapter requires 16 MiB flash, the exact reviewed table,
the six expected partition entries, active app0 OTA selection, correct X4 image
header/marker/digest and sector-rounded fit inside app0. It freezes the verified
bytes into a private temporary image for the sole write at `0x10000`, then
verifies exact readback and unchanged table/NVS/OTA bytes before booting.
No partition, OTA selector, bootloader, filesystem, or NVS writes are issued.
A failure after write-start requires manual recovery; no automatic reflash or
recovery/partition redesign is introduced. Cleanup failure also defeats success.

`trusted_targets.hardware_scan` stages multi-target scheduling by calling the
unchanged CAM runner and the independently gated X4 runner. A blocked X4 does
not suppress CAM. This function is not installed in the live timer, and the
CLI has no live mode. Hardware and dry-run journals are separate. The hardware
journal requires exact target/SHA/digest and all four diagnostic results;
terminal-status retry never repeats the transaction. An incomplete transaction
journal stops for manual inspection rather than flashing again. Hardware
status descriptions contain only fixed result text. No raw esptool output,
serial lines, images or arbitrary exception contents are published.

The execution gate remains unconditional because the actual approval refusal
has not been resolved. No manual enabling instructions or alternative route is
provided here. Once that real approval path is resolved, installation and
activation still require an independently reviewed pin and explicit approved
configuration. Mocked adapter/transport tests are not proof that this pinned
esptool version works on the attached X4; physical qualification remains
unperformed. Master also needs the X4 firmware environment, and the input
observability contract must land with its firmware owner. None is reported as
a hardware pass.

The owner allows test firmware overwrite without mandatory backup/restore for
X4 and other ESP test boards, **except T5S3**. X4 therefore has no mandatory
baseline restoration. CAM's existing restoration behavior is unchanged. This
allowance does not authorize destructive partition changes.

## Verification

Run:

```sh
python3 -m unittest discover -s test/hardware -p 'test_*.py' -v
python3 -m unittest discover -s test/hardware/cam -p test_trusted_controller.py -v
```

These checks use in-memory artifacts, fake GitHub/serial/esptool interfaces,
mocked complete app transactions and temporary lock directories. They do not access attached devices or build firmware locally.
