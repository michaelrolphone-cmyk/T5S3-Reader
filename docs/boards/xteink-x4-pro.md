# Xteink X4 Pro integration

## SD driver placement correction

The owner rejected the internal `/bootfs` placement at `ec0c099`. X4 and T5S3
now use an isolated read-only SD bootstrap, load verified ordinary `/Drivers`
packages into the existing graph, release bootstrap filesystem/controller
ownership, and only then activate normal external providers. No internal-flash
package path or embedded ELF fallback remains. The previous embedded and flash
store boot results are historical, not validation of the corrected SD handoff.
See [SD driver bootstrap](sd-driver-bootstrap.md) for source flow, exact SD
layout, bounds, failure retention, package visibility and update limitations.

The board builder stages installed generations plus matching Inbox archives and
board configuration onto a local `sdcard` tree. The existing artifact CLI now
freezes a firmware/SD bundle, without a flash module-store image. No device
write or deletion of old internal-flash contents is performed.

The T5 SPI-SD implementation now lives behind the same `storage.volume@1` contract. T5 display extraction behind `display.output@1` is connected locally with one shared SDK DMA reservation authority; see `t5s3-external-storage.md`. A firmware chip proxy is not accepted as the final extraction. Frontlight uses `display.frontlight@1`. Existing external T5 I2C/touch/navigation stay external; shared UI and loader logic remain shared. The optional external battery integration is described below.

Follow-up versions: firmware `1.3.87`, Springboard `1.3.1 → 1.3.2`, X4 SD `0.2.0 → 0.2.1`. Frontlight Settings now reflects a bound provider and restores the saved preference (zero off, nonzero on; current X4 provider is on/off, not PWM). Springboard requests fast display takeover only when a suitable provider and frame geometry exist; otherwise it uses static ordinary UI pages.

### Side-by-side follow-through (1.3.85)

The manual baseline delivered from `67d0fd3` remains the unmodified production
1.3.79 app plus its exact 50-file SD tree. Its checksum-bound archive is immutable;
the following source changes do not retroactively qualify or replace that image.

- X4 now rotates the provider submission by 180 degrees when Flip UI is enabled,
  while retaining the original software raster. Orientation changes require a
  clean presentation request; the current X4 panel uses its existing absolute
  full-frame refresh rather than a separate waveform for that intent. Shared
  Settings applies display and touch orientation together
  instead of changing only the persisted input preference until reboot.
- Settings → Network resolves the ordinary installed Wi-Fi Networks package,
  offers the existing required-app install/retry/cancel workflow when absent,
  and retains queued child navigation. It no longer requires a duplicate loose
  `/Apps/wifi_settings.elf` beside `/Apps/wifi_settings/wifi_settings.elf`.
- These changes affect firmware adapters/wrappers only. No app or external
  driver payload is changed or reissued under an existing package version.

For a useful baseline, check Settings 1.0.1, Springboard 1.3.2, App Store 1.0.8,
Driver Manager 1.0.8 and, for network setup, Wi-Fi Networks 1.0.3 through the
ordinary package manager. The driver-only SD tree does not install those apps.
Use board-specific driver/profile trees; never swap X4 and T5 firmware or drivers.

Manual checks when the owner is ready: record both firmware/package identities;
cold boot to shared Home; open/reopen Apps, Settings and each manager; compare
buttons/touch and Back/Cancel; open the same book and check saved progress;
toggle Flip UI off/on/off and exercise all four corners; reopen Network through
Settings; verify active boot providers remain protected against replacement.
On the immutable 1.3.79 baseline leave Flip UI off and use Apps to launch an
installed Wi-Fi Networks package until these repairs have their own verified build.
X4 uses static ordinary Apps pages without fast display takeover. Its frontlight
is on/off. Automatic sleep/shutdown remains incomplete; those paths must not
be reported as passing parity tests. Battery integration below is source-tested,
not physically qualified. Physical validation is
FAILED/unavailable until the owner performs and reports actual device tests.

The real SD provider/HAL wire-model profile for a 256 KiB read performs 517 sector reads. The prior code makes 4,653 waits; the revised 4 KiB-or-4 ms cooperation makes 65. A deliberately coarse 10 ms scheduling model gives 46,594 versus 714 modeled milliseconds; these are not measured device timings. The current target SDK uses 1 kHz ticks. The coarse baseline later exceeded a recursive-removal budget; the revised full four-case storage suite passes. This X4 overhead is not established as the shared post-U1 regression root cause. U1 merge boundary is `f7f006f7` (PR96), first parent `ca66db29`. Shared launch phase logs and transaction-only recovery reduce/identify repeated metadata work without removing selected-app admission.

## Optional battery follow-through (1.3.87)

The delivered production `1.3.85` board BINs and manual ZIPs remain immutable at
`e58ac310`. This change does not alter or requalify those assets. Its changed
`x4pro-battery` payload is independently versioned `0.1.1 → 0.1.2`; a future test
must use the matching ordinary package, never silently reuse old driver bytes.
No application package is changed; Battery Status is maintained independently.

The existing `board.battery@1` capability is acquired through the ordinary
installed-provider graph. It stays optional and is not added to the mandatory
boot package list. Missing/invalid packages leave Home usable with an explicit
unavailable indicator. The X4 owner input loop (including native-app polling)
reads at most once per five seconds and publishes a small copied snapshot;
Board/Settings/render readers never load a provider or perform hardware I/O.
Acquisition and pending-release cleanup attempts are spaced by 30 seconds.
Bad reads invalidate the copy immediately; a 15-second-old sample is unavailable
on the next read. Sampling does not force an e-paper refresh: an idle Home frame
retains its previously drawn value until the ordinary UI redraws.
Exact failed-release grants stay retained with revoked interfaces cleared. A
failed start whose own cleanup remains unsafe stays pinned by the existing graph;
there is no global shutdown of unrelated touch/display/storage providers.

The read-only external ELF validates the bus ABI, running gauge state, voltage
and 0–100 SOC before publishing. Charge GPIO21 handling follows the recovered
OEM input/no-pull, active-high semantics. All chip/pin behavior stays in that
ELF. No reset, wake, profile/BATINFO, charge policy, VBUS inference or full-charge
claim is added. A gauge still asleep or without usable resident configuration
reports unavailable. See [driver evidence and lifecycle limits](../../Drivers/x4pro_battery/README.md).

Base, Lyra and RoundedRaff show `--%` plus a crossed battery outline for unavailable
telemetry, distinct from a genuinely measured 0%. Unsupported current, capacity,
full and USB/VBUS fields remain unknown; a noncharging sample is not proof of
Discharge. T5/EPD47 keep their existing percentage-cache and power-icon behavior.

The current X4 bit-bang bus has finite byte/bit loops for each of the battery's
four fixed small reads, but ignores the supplied timeout. This patch claims
finite work and slow polling, not an enforced wall-time transaction deadline or
physical timing verification. The bus contract remains separate follow-up work.
Automatic X4 sleep/shutdown is still deferred. Hardware validation remains
FAILED/unavailable until an actual device result establishes it.

## Shared Reader software

X4 uses the same `HomeActivity`, `RecentBooksActivity`, `SettingsActivity`, native Springboard/app launcher and Reader dispatch as T5S3. There is no X4-specific Home selection branch or TXT application. Recent books opens the shared recents screen; Apps enters the normal verified Springboard resolution/launch flow; Settings enters the normal Settings workflow, including its required-app install/retry UI. Pinned apps and OPDS use the same Home logic. This describes connected software paths, **not successful app installation or execution on X4 media**.

`X4DiagnosticSetup` remains the board composition entry point. It binds independently installed display, navigation, touch and storage providers, initializes the shared power-lock service, and calls the shared display/font and Reader-state setup. It loads settings, language, theme, recents and app state through the normal storage facade. It no longer selects a fixed theme, registers a reduced font set, or scans a TXT file during boot. The provider display remains portrait 480×800 over the panel's 800×480 physical raster. A bounded startup present remains distinct from the first Home present.

Home and native apps call the same `MappedInputManager::update`. On X4 that consumes normal navigation/touch provider leases without polling the legacy GPIO facade or applying the external-controller preference to physical buttons. X4 now enters the shared main loop after its display readiness check: default-app selection, provider polling, activity dispatch, app-return handling, serial screenshot handling and cooperative loop delays are shared. `ActivityManager` runs the ordinary activity stack and shared render/power lock. X4 still skips the T5S3 startup animation and hardware setup. `HalSystem::begin()`, legacy SdFat/SPI initialization, raw T5 buttons/tilt and unimplemented X4 sleep/shutdown remain outside its path. The MONO1 Home-card adaptation remains board-specific; battery painters now use the shared validity-aware interface.

The original shared Home/filesystem correction used firmware `1.3.74` and SD provider `0.2.0`; the active external-driver milestone versions are listed above. JPEGDEC's existing abbreviated pin is expanded to the same full commit `86282979224c8a32fd51e091ed5a35b0c699a52b` for clean dependency fetches.

## Provider filesystem and shared storage

`x4pro-sd` now owns FatFs R0.15 and native one-bit SD reads/writes inside its ELF. Normal filesystem and card operations live in the ELF; the isolated firmware boot reader is read-only and releases ownership first. FatFs supports FAT12/16/32 superfloppy and MBR media, UTF-8 long names, ordinary file creation/update/truncation/append/sync, directory enumeration/rewind, mkdir/remove, and rename without replacement. CMD24 validates data acceptance, bounded busy completion and CMD13 status; uncertain writes are never retried automatically. Formatting and exFAT are unsupported.

`storage.volume@1` retains its original prefix and adds a size-checked extension for ordinary file modes, direct seek/info/sync, checked directory close/errors, mkdir and rename. The generic `HalStorageVolume.cpp` adapter uses that contract for every shared `HalStorage`/`HalFile` operation. Both X4 and T5S3 select it; T5 normal SD protocol remains in its ELF behind the shared SPI provider. This removes the prototype's read-only 8.3, four-component, single-cursor and 128 KiB seek restrictions without cloning app or package logic. Ordinary inventory, app manifest parsing, ELF file access and package staging use the same existing consumers.

Bounds are explicit: 12 independent file handles, 8 directory handles, 511-byte absolute paths with at most 24 components, 127 UTF-16-unit long names (directory entries must fit the existing 127-byte UTF-8 API field), and 4096-byte provider reads/writes. Seek supports existing offsets through the FAT file-size range; callers extend files by writing, not by seeking into uninitialized holes. Each provider operation has a 15-second, 2048-sector and 1,048,576-traversal-step budget with scheduler yields. HAL reads/writes are capped at 16 MiB/20 seconds per call; recursive removal at 6 levels/4096 entries/20 seconds. Larger streams continue incrementally.

Generation-tagged handles reject stale references. Remount and provider unload refuse live handles. CRC/integrity or uncertain write failure invalidates the volume; failed writable ownership remains retained until reboot. Directory iteration distinguishes error from EOF. Renames reject existing destinations, open descendants (including case/SFN aliases), and moves into the source's own subtree. The shared package transaction engine continues to own staging, backup and recovery; FAT is not journaled and this is not a power-loss atomicity claim.

Host tests compile the actual provider, FatFs and HAL against an in-memory native SD wire model for superfloppy and MBR layouts. They exercise long paths/names, settings persistence, 256 KiB files and random seeks, concurrent inventory/manifest/ELF handles, the real app manifest parser, mode/handle limits, staging/backup rename, removal, remount, CRC failure and retained failed writes. This is software evidence, not proof of Springboard or Settings ELF execution on physical X4 media.

## Remaining device limitations

Frontlight restores the saved level at startup (on/off only). Battery/RTC integration, sleep/wake and shutdown policy remain incomplete. No hardware validation has been performed for the external-file milestone. The earlier embedded route passed automated boot and some owner interactions, but those do not qualify this corrected route. Device ownership remains with the separate CI/hardware task.

## Provider and board facts

Provider builds use the pinned Espressif `esp-14.2.0_20260121` compiler with `RISCRTE_X4_LINK_PROFILE=esp14-no-relax`: `-O2` and `--no-relax`, plus `-fno-ivopts` for the panel only. The clock explicitly selects the firmware's 32-bit time ABI and asserts the `timespec` layout. Every provider build validates relative relocation targets against mapped sections before packaging; the runtime loader's rejection behavior is unchanged. The software workflow uses the digest-checked installer in `scripts/install_x4_provider_toolchain.sh`.

These selectively integrated fixes originate in CI commits `2c585242`, `484cd741`, and `4b4daea0`. All provider bytes are rebuilt from this branch; no old SD binary or manifest is imported. Package versions: clock `0.1.1`, panel `0.1.13`, I²C `0.1.1`, buttons `0.1.4`, frontlight `0.1.2`, GT911 `0.1.2`, battery `0.1.1`, storage `0.2.1`; firmware is `1.3.77`.

Selected external packages are `platform-clock-v1`, `x4pro-panel`, `x4pro-buttons`, `x4pro-frontlight`, `x4pro-sd`, `x4pro-i2c`, and `x4pro-gt911`. Modules are verified by the ordinary package reader and `ProviderModuleV2::loadVerifiedBytes`, then owned by the normal provider graph. Drivers publish existing capability ABIs with `t5_driver_get`; peripheral logic stays in the providers.

The GT911 provider controls master enable GPIO1 and active-low touch power GPIO2, performs the INT/RESET selection on GPIO10/4, and probes 0x5D/0x14 through the I2C provider. It publishes portrait single-contact DOWN/MOVE/UP and Home-key events to the shared capture task. Multiple contacts invalidate the gesture stream. The SD provider alone owns active-low power GPIO5 and native one-bit CMD42/CLK41/DAT0 40, with 80 ms off/120 ms on sequencing.

Pinned FreeInk source `111fdcc7f0176c3ee38391a160ee296bf492dbd8` supplies SSD1677 full refresh and UC8279 800×480 visible/800×600 addressed, 120-gate-offset GC waveform sequences. The provider requires a repeated UC8279 `VER[2]=0x68` identity and idle BUSY before issuing UC commands. Other UC81xx variants remain unsupported. Panel transfers have bounded waits and terminal failure rather than automatic retransmission.

Partition layout remains app0 `0x10000`/size `0x640000`, app1 `0x650000`. The build emits an app image and a merged image; no image was installed in this task. Historical owner observations established visible UC8279 text, and the earlier `5e1f8343` merged image produced mounted-storage/Home/heartbeat logs but a malformed landscape Home. Later source corrected portrait layout and input routing; those observations do not validate this revision. The separate hardware task owns the recovery evidence, USB stability investigation, controller state and future qualification.

## Shared T5 extraction follow-through

The T5 storage/frontlight cutover now uses independent SPI, SD/FatFs and native
LEDC frontlight ELFs. X4 and T5 share the provider-backed filesystem adapter and
FatFs implementation; neither active board has a firmware SD filesystem fallback.
See [T5 extraction and sleep ownership](t5s3-external-storage.md) for lifecycle,
legacy-import compatibility, separate package artifacts and remaining M3 work.
The X4 SD package remains the cumulative unreleased `0.2.1` update.
