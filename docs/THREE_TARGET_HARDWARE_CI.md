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

X4 provider builds use Espressif `esp-14.2.0_20260121` with the explicit
`esp14-no-relax` profile (`-O2 --no-relax` for the board providers). The older
8.4 linker asserted in `elf32-xtensa.c:3299`; GCC 14 alone also asserted for
the panel at `-Os`. The panel additionally disables induction-variable
optimization: that transformation emitted a relative pointer 12000 bytes before
the framebuffer, outside the loader's mapped sections. Builds now verify every
relative pointer in the generated provider ELFs before embedding them; loader
checks remain unchanged. The clock explicitly selects `_USE_LONG_TIME_T` and
asserts its 32-bit time ABI, matching firmware libc. GCC 14's default 64-bit
`timespec` linked but failed clock startup on hardware. Firmware
itself retains PlatformIO's pinned compiler. CI downloads the official Linux
toolchain archive and verifies its fixed SHA-256 before extraction.
For an already installed matching compiler, run:

```sh
X4_ELF_TOOLCHAIN=/path/to/toolchain/bin bash test/hardware/x4/build_candidate.sh
```

Run different PlatformIO configurations serially and freeze candidate images
outside `.pio` before switching: PlatformIO can remove another configuration's
build directory. Generated provider imports must match the actual ELF exactly;
the panel may have no imports under GCC 14 or only `memset` under the older
compiler. Unknown, missing and extra declared imports remain rejected.

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

Both CAMs now have a single factory app at 0x10000 with size 0x1f0000. The SD
CAM changed from its earlier OTA layout when the owner installed MicroPython;
the same chip and replacement table were read back and verified on October 3.
X4 retains its OTA layout. Exact reviewed partition-table digests, active app0 and bounded
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
Failure evidence retains only fixed provider identifiers, phase enums and
numeric boot facts; arbitrary serial messages and user file contents are not
written into the journal.

Provider build/ABI changes advance their provisional, unpublished package
versions in this branch: platform-clock-v1 0.1.0 → 0.1.1; x4pro-i2c 0.1.0 →
0.1.1; x4pro-panel 0.1.12 → 0.1.13; x4pro-gt911 0.1.1 → 0.1.2;
x4pro-buttons 0.1.3 → 0.1.4; x4pro-frontlight 0.1.1 → 0.1.2;
x4pro-battery 0.1.0 → 0.1.1; x4pro-sd 0.1.8 → 0.1.9. Reconcile concurrent
PR350 provider changes before consolidation; this does not publish packages.
