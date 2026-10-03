# Three-target CI and default heartbeat state

The owner selected SD CAM, no-SD CAM, and Xteink X4 Pro for CI. The relay and
camera-less CAM are outside this controller's allowlist. Firmware may be
changed externally. An unknown installed app is not a corruption condition.
After every physical candidate transaction, successful or failed, the target
is returned to a separately pinned, target-specific healthy serial heartbeat.
No previous application backup or restoration is required or performed.

## Build and identity

`three-target-build.yml` builds each exact PR head on GitHub-hosted Ubuntu.
Each independent matrix target uploads candidate and heartbeat application
images and a target/SHA/run/attempt/length/hash manifest. No host script from a
PR or artifact executes on the Mac. The installed controller must be reviewed
and pinned, and uses separately pinned heartbeat bytes as its cleanup image.

| Target | Chip MAC | Firmware |
| --- | --- | --- |
| cam-sd | 28:84:85:4b:57:98 | camera utility through headless runtime and real SD |
| cam-nosd | 28:84:85:4b:a1:1c | headless runtime, real read-only SDMMC mount, safely Idle on absent card |
| x4 | 84:c7:bb:79:e2:ac | exact-head xteink-x4-pro build |

USB port/location mappings are private and must follow physical verification.
CH340 descriptors do not expose the ESP chip MAC: the host verifies the MAC
after exclusive opening, before any write. Native X4 USB identity is verified
before opening and chip identity is checked again. Shared port/tty-alias/MAC
locks prevent interference with active device jobs. A busy device is deferred.

The no-SD CAM has a single factory app at 0x10000; SD CAM and X4 have different
OTA layouts. Exact reviewed partition-table digests, active app0 and bounded
erase ranges are mandatory. Bootloader/table/NVS/OTA bytes are verified
unchanged. Only app bytes at 0x10000 are written; there is no full erase,
partition rewrite or guessed board profile. External partition changes require
review rather than silently adopting a new layout.

## Cleanup and recovery

Heartbeat 1.0.0 reports target, MAC, sequence, uptime, free heap and running app
offset every two seconds. It initializes no storage, radio, camera, display or
relay. The CAM variants use UART; X4 uses native USB. Successful cleanup needs
exact image readback, unchanged protected regions and three strictly advancing
matching heartbeats. A failed cleanup prevents a hardware-success status.

The controller checks idle heartbeat under the same locks. If external firmware
is observed, it establishes the pinned heartbeat before the next candidate.
USB disappearance leaves failed/incomplete evidence. Once the mapped device
returns, a later maintenance pass can complete heartbeat cleanup; it does not
blindly replay an interrupted candidate. Candidate journals are isolated by
target and commit. Terminal status retries never replay hardware.

Private pauses are respected. The old X4 pause must not be removed merely to
obtain a green status: validate USB stability and heartbeat write/readback/boot
on the relocated X4 before enabling its new job. The older dry-run X4 adapter
remains disabled; it is not the heartbeat-policy runner. Actual installation
and qualification results are recorded separately from implementation tests.

## What a pass means

SD CAM: one camera utility capture, zero app return and steady Running state.
No-SD CAM: exactly one actual failed read-only mount, stable Idle heartbeats,
zero handles/grants, no Running transition. Identity refusal cannot pass as
storage absence. A mounted card is a failed no-SD fixture.
X4: diagnostic/storage/boot/Home serial observations and ready heartbeats from
the pinned candidate. This is not optical rendering, button/touch, TXT reading
or full shared-system-app qualification. The shared Home integration is a
separate software task; this CI work does not certify its completeness.

Every hardware pass also requires verified heartbeat cleanup. Cloud build
success, host mocks, candidate boot and physical UI qualification remain
separate claims. Raw serial, images, storage data and credentials are never
published in statuses or artifacts.
