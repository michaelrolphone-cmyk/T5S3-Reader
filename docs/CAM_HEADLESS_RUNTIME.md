# CAM offline core runtime increment

This hardware branch targets Reader master and depends on **unmerged U1
480bf345906344ce55eef3415e7180d6f6a631fb**. It does not reproduce U1's package
engine, graph, verification, streams or scheduler. The CAM build intentionally
fails on master alone until that dependency is integrated. Do not merge this
increment as a claim of complete U3/U4.

`src/main.cpp` selects `RuntimeBoot::setup/loop` for the explicit headless build.
The default T5 source body is unchanged. The CAM build excludes GUI allocations,
T5 peripheral startup, radio/network consumers and foreground app hosting. The
full U3 requirement to retain compiled graphical UI and select it dynamically,
T5 physical regression, provisioning and hardware-owning camera ELF are still
separate work. This is a persistent offline provider/service runtime, not the
older `HeadlessCoreCheck` or camera witness.

Normal boot runs on the Arduino owner task: identity-guarded module-store mount,
existing SD VFS, existing ordinary transaction recovery for the two selected
profile packages, installed capability snapshot, existing installed-provider
graph preparation, then named capability leases. The independent owner loop
calls `nativeProviderOwnerTick` with a 20 ms cooperative delay, without GUI ticks.
The graph, stream/resource host, serial bridge, app allocation implementation,
package verification/admission and ELF loader are the real U1 implementations.
Foreground-app API and HTTP fail closed because this profile does not host them.

The trusted profile authorizes `platform-clock-v1` / `platform.clock` v1 and
`archive-zip` / `archive.zip` v1. Installation grants nothing else. Recovery uses
U1's mutation and package-use gates, validation and selective purge. Unknown
stage data is retained; there is no automatic provisioning, inbox install or
activation of unrelated packages. Missing storage/package or failed admission
ends in low-work idle; no repeated scans, retries or hash loops. Explicit stop
releases in reverse order, shuts down the graph, then releases bootstrap storage.
Any uncertain teardown retains mappings/storage and forbids restart.

## Explicit CAM port

Only CAM MAC `28:84:85:4b:57:98`, 16 MiB flash / 8 MiB PSRAM; verified one-bit
SDMMC wiring CLK39/CMD38/D0=40. Module-store bootstrap is an isolated port
exception and publishes no runtime storage/hardware capability. It must never
coexist with an ELF owner of the same controller. No formatting or erase API.
All FAT operations execute on the mounting task under bounded disk operations;
limits include 16 handles, 255-byte paths, 2048 directory items, 4 KiB chunks,
20-second/8192-sector operation maxima and 250 ms command timeout, with actual
scheduler yields. Failed close or uncertain raw storage quarantines the mount.
U1 generation stamps track mount, mutation and live writers; read handles do not
invalidate them. Off-owner access fails without touching FAT.

The build pins espressif32 6.5.0 / Arduino 2.0.14 / IDF 4.4.6, the previously
qualified local CAM toolchain. A conditional PSRAM publication range adjustment
uses that SDK's external-RAM bounds; other targets keep their original bounds.
NVS is explicitly unsupported in this offline profile. A link-only wrapper
returns `ESP_ERR_NOT_SUPPORTED` to prevent Arduino's automatic NVS erase on
newer-format data. It neither claims successful initialization nor edits the SDK.

## Build and checks

Use a separate integration checkout of pinned U1 plus source-aware current
master integration; never edit PR96 or replace newer master features wholesale.
Apply this hardware branch's diff there. Set `PLATFORMIO_CORE_DIR` to an isolated
existing tool cache and `RISCRTE_ARDUINOJSON_INCLUDE` to installed ArduinoJson 7.
Run `pio run -c platformio.cam-runtime.ini -e cam-headless-runtime`.
No dependency installation, SDK mutation or automatic upload is in this build.
The selected hardware qualification environment is `cam-headless-qualification`.
It uses exactly the same normal boot and leases, checks the eight existing
installed file hashes and actual clock/ZIP/stream behavior, performs one clean
stop/remount/restart, repeats, then remains Running. It installs no packages and
contains no camera code. The normal runtime environment omits this batch.

`bash test/run_cam_headless_test.sh <U1-checkout>/lib/hal` checks boot rollback,
no automatic retry, explicit restart, retained teardown, actual SD port identity,
ownership, mutation generations, failed close and failed metadata I/O. Hardware
proof requires the locked, MAC/current-image-guarded external runner and preserved
full erase-range backups. CI's focused contract job uses exact U1 API headers;
it is not a substitute for the integrated firmware build or physical evidence.

## Observed physical checkpoint (2026-10-01)

The single planned physical batch used hardware commit `a640d3ca`, integrated
with pinned U1/master as `0be8530a`. Firmware was 477216 bytes, SHA-256
`597f88dcf4a3fb2aaff20fb24bea457e3089ba8bf3b6d667166ec4e2f7aa2da5`,
at app0 `0x10000`. Exact prior-image guard, full 561152-byte backup, flash
readback, partition/OTA preservation and NVS preservation during writing passed.
Both ordinary recoveries returned InstalledVerified, actual installed clock and
ZIP acquired through the normal graph, and both boot cycles reached Running.
The intervening normal shutdown/remount released all grants and file handles.
All eight installed files matched their pre-existing hashes in both cycles;
clock advanced 20 ms and ZIP/production stream/resource/revocation checks passed.
The final 31 heartbeats held heap 306924, PSRAM 8046391 and zero file handles with
live service grants. This is bounded operation evidence, not a long-run leak or
full T5/U3/U4 qualification claim. No camera, display or radio ran in this batch.

The original host runner recorded failure because an adjacent dependency's
`Logging.h` won include resolution and emitted `DIAG_RUNTIME` instead of
`RUNTIME`; the first boot label also followed a partial SDK log line. Preserve
that original result. `test/hardware/cam/verify_capture.py` validates the saved
raw capture with either prefix, and separate analysis establishes the above
physical results without opening a port or flashing again. Six malformed
capture variants were rejected. The follow-up gives local port headers priority,
separates the initial boot line and removes stale last-error text from successful
grant diagnostics. These logging/build corrections are software-built, not
reflashed. The T5 choreography test now explicitly examines its graphical branch.

Local immutable evidence: `lab-evidence/cam-runtime-artifacts-1` (physical build,
source archive and actual dependency logger), `lab-evidence/cam-runtime-flash-1`
(raw capture, original runner failure, separate qualification analysis and all
backups). Original-camera cumulative recovery hash remains
`92a4400ab9f81b6f5741f27595f0731503a2c51bf11722f947fe06457a3c7bd0`.
Current pre-write camera-image recovery range hash is
`c710e66ef0e42d27f0ae8411288d24844110f3f3286e2d10306f8b8737f40bcb`.

## Configured default app entry (PR 344 follow-up)

The later configured-app increment is described in [DEFAULT_APP_ENTRY.md](DEFAULT_APP_ENTRY.md).
It supersedes the initial statement above that this profile has no foreground
app hosting. The no-selector behavior remains the same offline provider idle.
This change does not imply general U4 provisioning or GUI extraction.
