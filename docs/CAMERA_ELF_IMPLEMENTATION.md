# Installed ESP32-S3 camera increment

This is the camera-driver continuation of draft PR #344. It depends on the
unchanged U1 provider/stream ABI at `480bf345906344ce55eef3415e7180d6f6a631fb`.
It does not claim complete U2, U3, U4 provisioning, dynamic graphical UI, or
physical acceptance. No camera-specific firmware import or loader exception is
introduced. The runtime branch remains separate from U1 implementation history.

## Packages and boundary

New, unpublished stable identities: `camera-esp32s3-ov3660` **0.1.0** and
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
`/Inbox/camera-elf-01`, and invokes the existing ordinary ZIP installer. This is
an explicit one-shot lab deployment, not complete first-run/reapply provisioning.
SD bootstrap checks dedicated CAM MAC `28:84:85:4b:57:98` before filesystem I/O.
The normal recovery/inventory/graph/owner loop then activates installed modules.
A generic consumer writes `/camera-elf-01.jpg.partial`, enforces bounded chunks
and deadline, waits for EOF/declared length, closes the stream/job and file,
reads back SHA-256, then renames to `/camera-elf-01.jpg`. Existing output is never
overwritten; incomplete bytes remain marked partial. No image tuning, display,
networking, RF or other board work is part of this increment.

## Checks and physical evidence

Host provider tests cover exact byte delivery, request bounds, BUSY, stale jobs,
backpressure, capture/output deadline, cancellation, stream revoke/finish failure,
partial start and uncertain-stop retention. Boot/SD and existing private-import
checks remain independent. Builds and host checks do not prove sensor operation.
The physical baseline remains firmware SHA256
`597f88dcf4a3fb2aaff20fb24bea457e3089ba8bf3b6d667166ec4e2f7aa2da5`
until a new guarded physical evidence checkpoint records otherwise. Preserve
all earlier images/backups and separate the tested image from published source.
