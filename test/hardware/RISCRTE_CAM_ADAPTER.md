# RiscRTE CAM adapter

`runtime_cam_adapter.py` is pinned host code used by `three_target_controller.py`.
It accepts only repository `michaelrolphone-cmyk/RiscRTE` (ID 1402583471), open
owner-authored same-repository PR heads, and successful exact-head runs of
`.github/workflows/cam-hardware-build.yml`. No artifact code runs on the host.

The workflow publishes `cam-app-candidate-<fullSHA>` with exactly five files:
`firmware.bin`, `default.elf`, `board.json`, `boot.json`, `manifest.json`.
The schema-1 manifest records target `cam-nosd`, source_sha, numeric run_id and
run_attempt, boot_backend `embedded-readonly`, flash_bytes 16777216 and
memory_type `qio_opi`. Its firmware object contains file, bytes, sha256 and
offset 65536. Its payloads array contains three objects with file, bytes,
sha256 and image_offset (byte offset within firmware.bin). The adapter verifies
all hashes and the actual firmware slices, rejects overlapping payload ranges,
and limits the sector-rounded application image to 0x1f0000 bytes.

This immutable runtime boot-store fixture is distinct from X4 production driver
packaging. It does not authorize embedding X4 hardware drivers or changing SD
contents. The CAM's bootloader, partition table, NVS and filesystems are never
written. Physical MAC and partition checks remain in heartbeat_policy.py.

The private controller configuration opts in with `runtime_cam`:

```json
{"enabled":false,"unavailable_reason":"CAM USB control transfers time out"}
```

The adapter uses the existing cam-nosd binding and pinned cleanup heartbeat.
The runtime enabled flag controls runtime execution independently of Reader CAM
jobs; leave it false until transport has been verified. Disabled execution still
publishes terminal failure on each eligible head without opening a serial port.
It shares the scheduler lock and device locks with Reader, so there is one
physical owner. `candidate_hold` on a Reader job retains heartbeat maintenance
while preventing candidate loads, for example while external-driver packaging
is being corrected.

The status context is `ESP32-CAM hardware / heartbeat cleanup`. Missing artifacts,
disabled execution, identity/layout failures and unsuccessful device tests report
terminal failure with a reason. A cloud poll gate must also time out to failure
if the host is offline or GitHub status publication fails. Pending is used only
while an accepted candidate is actively executing. The same head is not flashed
again after an attempted transaction. Interrupted cleanup is recovered before
further execution when the runtime job is enabled. Artifact-only failures may
be retried on subsequent scans; terminal statuses do not impose merge rules.

Success requires candidate readback, at least three advancing exact-target/MAC
`RTE_HEARTBEAT version=1.0.0` lines with positive heap, no observed panic/watchdog,
unchanged protected flash and verified restoration of the separately pinned
heartbeat image. The app profile verifies runtime heartbeats rather than the
Reader no-SD mount diagnostic. The build owns proving its default ELF uses the
real loader path; hardware acceptance proves the exact accepted image runs.

## Independent new-runtime X4 scheduling

The same pinned adapter accepts an explicit `x4` profile, using
`x4-hardware-build.yml`, `x4-app-candidate-<sha>`, the X4 app0 limit 0x640000,
its native USB/MAC binding, and status `X4 hardware / runtime heartbeat cleanup`.
Opt in through private `runtime_x4: {"enabled": true}` configuration. Journals
and recovery records live under `runtime-x4`, separately from `runtime-cam`.
This does not override Reader PR350's external-store deployment hold. The
immutable heartbeat fixture must have empty drivers/buses/devices; no hardware
driver packages are embedded or provisioned by this profile.

Device absence fails promptly. A still-building exact-head artifact is pending
for at most ten minutes, then terminal failure; the independent cloud gate also
has a deadline. An accepted head is attempted once, with separately pinned
heartbeat cleanup. Prior X4 evidence is imported only for its exact source SHA,
never treated as a pass for a newer head. A failed cloud polling job predating a
new valid receipt must be rerun to reconcile its own job conclusion.
