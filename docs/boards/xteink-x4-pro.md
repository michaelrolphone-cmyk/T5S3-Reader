# Xteink X4 Pro integration

## Shared Reader software

X4 uses the same `HomeActivity`, `RecentBooksActivity`, `SettingsActivity`, native Springboard/app launcher and Reader dispatch as T5S3. There is no X4-specific Home selection branch or TXT application. Recent books opens the shared recents screen; Apps enters the normal verified Springboard resolution/launch flow; Settings enters the normal Settings workflow, including its required-app install/retry UI. Pinned apps and OPDS use the same Home logic. This describes connected software paths, **not successful app installation or execution on X4 media**.

`X4DiagnosticSetup` remains the board composition entry point. It attaches the embedded display, navigation, touch and storage providers, initializes the shared power-lock service, and calls the shared display/font and Reader-state setup. It loads settings, language, theme, recents and app state through the normal storage facade. It no longer selects a fixed theme, registers a reduced font set, or scans a TXT file during boot. The provider display remains portrait 480×800 over the panel's 800×480 physical raster. A bounded startup present remains distinct from the first Home present.

Home and native apps call the same `MappedInputManager::update`. On X4 that consumes the boot-owned navigation/touch providers without polling the legacy GPIO facade or applying the external-controller preference to physical buttons. `ActivityManager` runs the ordinary activity stack and shared render/power lock. X4 still skips the T5S3 startup animation and hardware setup. `HalSystem::begin()` and legacy SdFat/SPI initialization remain outside the X4 boot path. The MONO1 Home-card and absent battery-indicator adaptations remain board-specific pending capability abstraction.

Firmware version: `1.3.63` → `1.3.74` for the cumulative shared Home/filesystem correction. The separate CI work uses `1.3.70`, PR372 uses `1.3.72`, and consolidation reserves `1.3.73`; integration must preserve monotonic versions. Provider `x4pro-sd` changes from `0.1.8` to `0.2.0`. No app package changed. JPEGDEC's existing abbreviated pin is expanded to the same full commit `86282979224c8a32fd51e091ed5a35b0c699a52b` for clean dependency fetches.

## Provider filesystem and shared storage

`x4pro-sd` now owns FatFs R0.15 and native one-bit SD reads/writes inside its ELF. Firmware does not implement X4 filesystem or card protocol. FatFs supports FAT12/16/32 superfloppy and MBR media, UTF-8 long names, ordinary file creation/update/truncation/append/sync, directory enumeration/rewind, mkdir/remove, and rename without replacement. CMD24 validates data acceptance, bounded busy completion and CMD13 status; uncertain writes are never retried automatically. Formatting and exFAT are unsupported.

`storage.volume@1` retains its original prefix and adds a size-checked extension for ordinary file modes, direct seek/info/sync, checked directory close/errors, mkdir and rename. The generic `HalStorageVolume.cpp` adapter uses that contract for every shared `HalStorage`/`HalFile` operation. X4 selects it; T5S3 retains its existing SdFat backend and SPI ownership. This removes the prototype's read-only 8.3, four-component, single-cursor and 128 KiB seek restrictions without cloning app or package logic. Ordinary inventory, app manifest parsing, ELF file access and package staging use the same existing consumers.

Bounds are explicit: 12 independent file handles, 8 directory handles, 511-byte absolute paths with at most 24 components, 127 UTF-16-unit long names (directory entries must fit the existing 127-byte UTF-8 API field), and 4096-byte provider reads/writes. Seek supports existing offsets through the FAT file-size range; callers extend files by writing, not by seeking into uninitialized holes. Each provider operation has a 15-second, 2048-sector and 1,048,576-traversal-step budget with scheduler yields. HAL reads/writes are capped at 16 MiB/20 seconds per call; recursive removal at 6 levels/4096 entries/20 seconds. Larger streams continue incrementally.

Generation-tagged handles reject stale references. Remount and provider unload refuse live handles. CRC/integrity or uncertain write failure invalidates the volume; failed writable ownership remains retained until reboot. Directory iteration distinguishes error from EOF. Renames reject existing destinations, open descendants (including case/SFN aliases), and moves into the source's own subtree. The shared package transaction engine continues to own staging, backup and recovery; FAT is not journaled and this is not a power-loss atomicity claim.

Host tests compile the actual provider, FatFs and HAL against an in-memory native SD wire model for superfloppy and MBR layouts. They exercise long paths/names, settings persistence, 256 KiB files and random seeks, concurrent inventory/manifest/ELF handles, the real app manifest parser, mode/handle limits, staging/backup rename, removal, remount, CRC failure and retained failed writes. This is software evidence, not proof of Springboard or Settings ELF execution on physical X4 media.

## Remaining device limitations

Frontlight stays off at startup. Battery/RTC integration, sleep/wake and shutdown policy remain incomplete. No hardware validation, device access, flashing, resets, serial probes or hardware tests were performed for this software change. Hardware qualification is intentionally deferred to the separate CI/hardware task.

## Provider and board facts

Embedded load dependencies are `platform-clock-v1`, `x4pro-panel`, `x4pro-buttons`, `x4pro-frontlight`, `x4pro-sd`, `x4pro-i2c`, and `x4pro-gt911`. Modules are hash-verified by `ProviderModuleV2::loadVerifiedBytes` and remain owned by the board boot composition. Drivers publish existing capability ABIs with `t5_driver_get`; peripheral logic stays in the providers.

The GT911 0.1.1 provider controls master enable GPIO1 and active-low touch power GPIO2, performs the INT/RESET selection on GPIO10/4, and probes 0x5D/0x14 through the I2C provider. It publishes portrait single-contact DOWN/MOVE/UP and Home-key events to the shared capture task. Multiple contacts invalidate the gesture stream. The SD provider alone owns active-low power GPIO5 and native one-bit CMD42/CLK41/DAT0 40, with 80 ms off/120 ms on sequencing.

Pinned FreeInk source `111fdcc7f0176c3ee38391a160ee296bf492dbd8` supplies SSD1677 full refresh and UC8279 800×480 visible/800×600 addressed, 120-gate-offset GC waveform sequences. The provider requires a repeated UC8279 `VER[2]=0x68` identity and idle BUSY before issuing UC commands. Other UC81xx variants remain unsupported. Panel transfers have bounded waits and terminal failure rather than automatic retransmission.

Partition layout remains app0 `0x10000`/size `0x640000`, app1 `0x650000`. The build emits an app image and a merged image; no image was installed in this task. Historical owner observations established visible UC8279 text, and the earlier `5e1f8343` merged image produced mounted-storage/Home/heartbeat logs but a malformed landscape Home. Later source corrected portrait layout and input routing; those observations do not validate this revision. The separate hardware task owns the recovery evidence, USB stability investigation, controller state and future qualification.
