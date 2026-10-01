# Installed ESP32-S3 camera increment

This is the camera-driver continuation of draft PR #344. It depends on the
unchanged U1 provider/stream ABI at `480bf345906344ce55eef3415e7180d6f6a631fb`.
It does not claim complete U2, U3, U4 provisioning, dynamic graphical UI, or
physical acceptance. No camera-specific firmware import or loader exception is
introduced. The runtime branch remains separate from U1 implementation history.

## Packages and boundary

New, unpublished stable identities: `camera-esp32s3-ov3660` **0.1.1** and
`cam-ov3660-profile` **0.1.0**. The latter provides installed wiring data through
`board.camera.esp32s3.profile@1`; the former consumes it and publishes generic
`camera.capture@1`. New identities have no earlier published camera lineage.
Existing clock, ZIP, USB and other package versions are unchanged.

`RiscCameraCaptureV1.h` provides bounded capture, status, read, cancel and release.
One generation-scoped job at a time, JPEG 800x600, quality 4–40, deadline 0.5–15s.
Frames never cross the module boundary as pointers. The producer publishes into
an existing bounded 1024-byte provider stream, at most 512 bytes per owner tick.
The camera lease authorizes its bounded read API, which consumes that same
stream. Direct cross-context access to the returned endpoint separately needs
the runtime's normal stream grant. EOF and error terminals remain distinct.
Backpressure cannot spin or extend the absolute deadline. Job tokens do not
survive the owning provider lease/module generation.

Hardware code is linked into the driver ELF: OV3660 registers, native bit-banged
SCCB on dedicated pads, LCD_CAM clock and capture setup, finite GDMA RX4 descriptor
chain and 96KiB internal DMA allocation. No worker, interrupt callback or firmware
camera/GPIO/I2C/LEDC driver is borrowed. No PSRAM cache ownership is needed for
these internal DMA buffers. DMA park is checked before buffers are inspected or
freed. A pending stop retries on subsequent owner calls; uncertain quiescence
retains the mapped ELF, buffer and dependencies through existing graph rules.
Poll performs one bounded transition or chunk; the existing owner scheduler
supplies cooperative yield. SCCB startup uses finite transactions, clock-stretch
limits, per-transaction yields and an absolute five-second deadline.

The qualified profile records the previous native witness's OV3660 pin map:
D0..D7 11,9,8,10,12,18,17,16; PCLK13, VSYNC6, HREF7, XCLK15 at20MHz;
SDA4/SCL5/address0x3c. It reserves LCD_CAM and GDMA RX4 exclusively in the
explicit closed CAM experiment deployment. The driver also refuses enabled
LCD_CAM/occupied RX4 and duplicate/conflicting profile pads. This is not a
universal SoC resource broker: extending deployment to other concurrent hardware
requires trusted opaque resource admission rather than treating this profile as
a grant. Normal headless profile continues to authorize only clock/ZIP.

Exact imports are existing CPU/libc primitives: clock_gettime, usleep, vTaskDelay,
three ROM GPIO matrix/pad primitives, three heap_caps primitives, memcpy, memset,
strcmp and strlen. No CPU ABI addition or shared U1 source change was needed.
The independent build audits imports against the private loader inventory,
not ordinary application exports. The ordinary package builder checks every
relocation target and generates the unchanged provider/import metadata plus one
`.rte.zip` per package. It discovers both conventional builders automatically.

## Build and consumer

In the isolated U1+PR344 integration checkout, use the existing pinned ESP32 SDK:

```
python scripts/build_cam_ov3660_profile.py
python scripts/build_camera_esp32s3.py
python scripts/build_installed_usb_stack.py --ids cam-ov3660-profile camera-esp32s3-ov3660
sh test/run_camera_provider_test.sh
pio run -c platformio.cam-runtime.ini -e cam-camera-experiment
```

The Python interpreter must contain the existing pyelftools dependency. From the
focused hardware branch, set `RISCRTE_PROVIDER_SDK_ROOT` to the pinned integration
checkout; do not copy U1 runtime code into this PR. `PLATFORMIO_CORE_DIR` and
`RISCRTE_ARDUINOJSON_INCLUDE` retain the isolated build rules documented in
`CAM_HEADLESS_RUNTIME.md`. Rebuild explicitly after any source edit: the generic
bundle command can reuse an already linked ELF.

The opt-in `cam-camera-experiment` environment embeds the two ordinary ZIPs as a
lab transfer medium, writes them with exclusive-create/exact-existing checks to
`/Inbox/camera-elf-02`, and invokes the existing ordinary ZIP installer. This is
an explicit one-shot lab deployment, not complete first-run/reapply provisioning.
SD bootstrap checks dedicated CAM MAC `28:84:85:4b:57:98` before filesystem I/O.
The normal recovery/inventory/graph/owner loop then activates installed modules.
A generic consumer writes `/camera-elf-02.jpg.partial`, enforces bounded chunks
and deadline, waits for EOF/declared length, closes the stream/job and file,
reads back SHA-256, then renames to `/camera-elf-02.jpg`. Existing output is never
overwritten; incomplete bytes remain marked partial. No image tuning, display,
networking, RF or other board work is part of this increment.

## Checks and physical evidence

Host provider tests cover exact byte delivery, request bounds, BUSY, stale jobs,
backpressure, capture/output deadline, cancellation, stream revoke/finish failure,
partial start and uncertain-stop retention. Boot/SD and existing private-import
checks remain independent. Builds and host checks do not prove sensor operation.
The single camera physical batch used source `836f6adc` on integration base
`5cb9367f`, firmware `f5614d6dd45d13b5c753aee5572234e403a62efc86cf576c68b12bb9c40975d7`
(521568 bytes at 0x10000). MAC, exact previous597f88dc image, complete erase-range
backup, readback and partition/OTA/NVS preservation passed. Both ordinary packages
installed; the real loader relocated the camera ELF, its OV3660 initialization
succeeded, and the normal graph granted camera.capture and reached Running.
Capture failed after approximately15s; the generic diagnostic reported
`stage=provider cleanup=0`. That is successful JOB cleanup, not evidence of full
provider unload. No complete JPEG was produced. A partial output is preserved.
The runner closed the serial port/device lock; no second flash was performed.

The subsequent source audit found VSYNC matrix inversion missing relative to the
working native witness (`cam_hal.c` sets `vsync_invert=true`). Source now matches
that polarity and copies a bounded timeout-phase diagnostic into capture status.
This is a concrete correction, **not proof that it resolves the physical timeout**.
Because camera0.1.0 was actually installed, changed bytes are packaged as0.1.1;
profile0.1.0 is unchanged. The updated experiment uses fresh camera-elf-02 paths
to preserve earlier partial output. The0.1.1 candidate remains unflashed.

Local broader CPU resolver tests cannot link weak provider logging symbols on
this macOS host; the separately compiled production private-import preflight
passes. Camera/boot/SD tests pass. Initial camera CI failed a GCC test indentation
warning; test-only commit d956ed7b fixes it and its CAM contract CI passes.
Remaining physical gate: successful frame completion, retrieval/decode and safe
full-provider stop/restart for the corrected candidate. Full provisioning and
wider U3/U4 acceptance remain separate, unimplemented scope. The PR stays draft.
