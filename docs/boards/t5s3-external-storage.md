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

The full architecture correction is still in progress: T5 parallel display,
DMA and waveform extraction remains M3, followed by the shared pre/post-U1
performance investigation. The old display ELF still proxies firmware and is
not accepted as the final extraction. No hardware validation, flashing,
formatting or provisioning was performed for this milestone. Physical validation
and review of the paired deployment remain separate owner steps.
