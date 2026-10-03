# T5 shared storage and frontlight cutover

This software milestone selects independently installed `spi-esp32s3-v1@0.1.0`,
`t5s3-sd@0.1.0`, `t5s3-frontlight@0.1.0` and `platform-clock-v1@0.1.1` from the
read-only `/bootfs` store. There is no T5 SdFat/SDFS fallback. Missing or invalid
packages stop composition and use the existing Reader error path. Ordinary
Reader, application and `/sd` VFS consumers use the same `HalStorageVolume`
adapter as X4. SD protocol and the shared FatFs implementation live in the SD
ELFs. The SPI bus ELF alone imports the temporary raw firmware controller port;
the resident LoRa adapter shares its existing SPIClass transaction lock.

The T5 frontlight ELF owns GPIO11 and LEDC timer0/channel0. It preserves the
0–10 quadratic brightness curve, 5 kHz frequency, 8-bit resolution and full-on
endpoint. It uses native register operations, with no firmware PWM calls. Other
active channels/timers are preserved, and a conflicting timer0 user or unsupported
clock source is rejected. The shared board facade only forwards the saved level.
Register-model tests are not physical PWM validation.

## Sleep ownership

Input shutdown drains all providers except the exact platform storage/frontlight
lease generations and their healthy dependency closure. A pending release,
unrelated active grant, failed lower dependency or failed cleanup refuses the
barrier. Persistent capabilities remain mapped; this is not a claim that their
physical hardware has quiesced.

Immediately before display/board teardown, the storage provider checks that no
writer or uncertain operation remains, completes synchronous media work, and
blocks new I/O. Read handles may remain as RAM metadata, with their provider and
dependencies pinned until reset. Cancellation before any pin/rail change restores
those handles without remounting. Actual deep sleep restarts the MCU and acquires
fresh generations. A missing/unmounted medium with no uncertain transfer can
also cross the barrier. Writers and failed writes cannot.

Hard power-off alone reserves the power provider while storage is still usable.
If storage refuses sleep, reservation cancellation retains a failed exact release
for retry and never calls its revoked interface. Once the one-way barrier is
crossed, fallback goes directly to deep sleep; it does not save files, remount,
or start another graph shutdown. Timer clock sleep uses the same checked display
boundary. Its existing early timer wake still avoids SD mounting.

## Compatibility

The global `SD` and six Arduino SDFS methods are intentionally retired from
ordinary app imports. Admission rejects them before allocator/table/module
fallback and reports an explicit update message. No dummy object or second
filesystem owner is supplied. Rebuild such legacy applications against
`t5_storage_get_api` or the shared `/sd` VFS. Unknown user packages are untouched.
Generic File/FS lifetime accounting remains conservative.

An audit of 11 published archives (including Settings 1.0.1 and Springboard
1.3.1) verified every archive against its GitHub release digest and inspected
both ELF symbol tables. None imported these retired symbols. Exact identities,
URLs and hashes are in `released-storage-import-audit.json`; this is a sample,
not a claim about all historical or user-built applications. The read-only
`scripts/audit_storage_imports.py` can inspect additional archives.

## Artifacts and validation

`stage_t5s3_packages.py` emits ordinary independent package archives and a T5
boot-store tree. The `T5 external software pair` workflow builds both firmware
and providers, round-trips the LittleFS image, and publishes a separate
`t5-external-deployment-<sha>` bundle. Its deployment manifest marks provisioning
unauthorized. The X4 and T5 profiles are distinct; never interchange their images.
Both retain the existing partition geometry, offset `0xc90000`, size `0x360000`.
Prior device contents are not inferred from these build artifacts.

Local checks cover both firmware targets; real provider/FatFs/HAL operations on
native one-bit and SPI card models; ELF imports/relocations; released import
auditing; missing providers; retained read handles; writer refusal; lower-provider
shutdown failure/retry and fresh acquisition; power reservation cancellation;
frontlight duty/register ownership; shared Home dispatch and clock sleep wiring.
Host fixtures do not execute Xtensa code or prove device timing. The graph suite
passed with UBSan locally; the host ASan run stalled during startup, so no local
ASan pass is claimed.

The full architecture correction remains in progress. The current local M3
cutover and its shared DMA reservation boundary are described below. The pre/post-U1
performance investigation remains subsequent work. No hardware validation,
flashing, formatting or provisioning was performed. Physical validation and
review of the paired deployment remain separate owner steps.

### M3 expander prerequisite (software integration)

The T5 bootstrap profile now also contains `i2c-esp32s3-v2@0.1.6` and
`pca9535-gpio@0.1.0`. The latter claims address 0x20 once and owns PCA9535
register transactions. Board startup resolves that provider before expander
controls; absent or partially started providers fail closed. Early desk-clock
startup loads the immutable bootstrap inventory but does not activate SD.

The board holds two nonoverlapping, generation-bound pin grants: radio enable
IO00 and button IO12. `tps65185-power@0.1.0` owns the display IO10/11/13–17
grant and the sole I2C claim for TPS65185 at 0x68.
Output grants initialize safe low levels before direction changes. Button and
power-good/interrupt pins remain inputs. Writes outside a grant and stale
handles are rejected; release restores only that grant's safe output bits.
Unconfirmed writes retain chip ownership and dependencies until reboot rather
than retrying or restoring a stale bank. Retained platform sleep grants include
the expander and its I2C dependency, including partially acquired grants.

Both existing display engines now consume the same external `display.power`
capability. OE/mode/PWRUP/VCOM drop before WAKEUP, preserving the existing
shutdown order. A failed write retains the power grant and dependencies;
failed power teardown cannot free the fast engine buffers or complete sleep.
The quality worker refuses to scan without confirmed power. Register/power
sequencing is external. No physical pin, waveform, sleep or timing validation is
claimed. The expander/power prerequisite expanded the profile from four to seven
ordinary packages; older paired artifacts do not represent that source.

### M3 display extraction checkpoint — not complete

The local `display-epd-video@0.1.5` package now contains the existing M5GFX
0.2.20 Reader waveform/history engine, Wisp path and the existing fast mono/gray
engine. Reader firmware holds a software canvas and forwards presentation;
`NativeVideoBridge` retains the takeover check and forwards legacy video calls.
The provider dispatcher excludes simultaneous Reader/fast ownership and retains
failed partial starts/stops. Platform composition pins its exact lease, including
timer boot before storage starts. This is the eighth T5 bootstrap package. It replaces the old ABI-1
0.1.4 video proxy, which cannot run against this firmware after its private
`t5_video_get_api` import exception is removed. Firmware and the independently
built module-store package set must be paired.

Pinned IDF 4.4.7 LCD code is staged into the provider. Its TX mechanics use
OS/CPU ABI 3 to reserve a channel and trigger from the resident SDK allocator,
which also serves SPI and hardware crypto. No second GDMA allocator, global DMA
clock/reset owner, or copied SDK peripheral clock table is linked into the ELF.
The provider owns LCD-only clock control, descriptors, channel configuration and
transfers. Shutdown removes its LCD IRQ on the allocating core, stops TX with
100-poll/100-ms bounds, then releases the shared reservation. Failed cleanup
retains tokens, buffers, code and dependencies. Worker completion precedes frees.

ABI 3 adds only two reservation functions to frozen ABI 2; ABI 1/2 cannot resolve
them. No firmware `t5_video_get_api`, LCD or GPIO implementation is imported.
The original generic display.output prefix remains intact; a checked T5-specific
extension carries Reader history operations and the legacy fast API. Reader/fast
handoff invalidates stale frame tokens, including portable display clients.

The independent GCC14 package, structural loader validation and relocation map
checks pass locally. Canonical sections preserve IRAM literal ordering; an opt-in
build profile compacts the observed all-zero GCC14 linker placeholders without
relaxing runtime validation. Local firmware builds pass for X4 and T5S3. The
actual T5 firmware ownership audit finds no LCD/EPD engine and confirms the shared
SDK DMA reservation boundary. Host checks cover dispatcher handoff, retained
shutdown, CPU reservation generation/context/failure handling, simulated mixed
SPI/crypto ownership, scrub, retired imports and shared Home dispatch. Exact ABI
admission and eight-package T5/seven-package X4 bootstrap fixtures pass.

The full ASan graph script stalled on this macOS host after its first graph test
and was stopped; targeted manager/spec/snapshot tests run separately. The generic
USB catalog test needs its full generated catalog and was not runnable with the
board-only package staging. CI remains the check for those complete build inputs.
These results do not establish hardware timing, display quality or acceptance.

The five old raw LCD entry points are retired from ordinary app imports before
fallback lookup. Applications use display.output or the retained legacy video
consumer. Eleven preserved release archives were rechecked against their prior
release digests and neither raw SD nor LCD imports were found; exact results are
in `released-display-import-audit.json`. This sample does not establish safety
for every historical or user-built application.
