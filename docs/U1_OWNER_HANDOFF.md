# U1 software candidate and one owner hardware session

This is the handoff for the existing PR #96 candidate that integrates master
`d2d5a9a132d747122b0f0b607f228132ba79e667`, including Wi-Fi #345 and
Status Bar #346 as well as earlier Text Editor/display merges. The configured
firmware candidate is **1.3.54**, reserved separately from display migration
1.3.55; Status Bar's cumulative U1 ZIP is **1.0.3** above published loose
1.0.2. Verify PR #96's current head and use only Actions artifacts whose
`head_sha` exactly matches it. The prior green 1d93550f artifacts in the ledger
are historical and are not install/flash inputs for this candidate.

The candidate is **not released, flashed or physically qualified**. Only the
owner can initiate Release Qualification, merge or flash. Camera PR #344 remains
a separate draft that depends on this shared firmware baseline; its default
app and camera behavior are outside this U1 session.

## Software disposition

| Gate | Result | Exact evidence or remaining limit |
|---|---|---|
| Four-kind ZIP build, ordinary install/update/inventory/uninstall/recovery, resources and stable CDC lineage | Connected | [PlatformIO/host run 36915387635](https://github.com/michaelrolphone-cmyk/T5S3-Reader/actions/runs/36915387635) passed four-kind lifecycle, recovery, native app/manager and release-index checks on the previous source checkpoint; the new integration must retain these in its exact-head runs. |
| Installed provider streams, rights, revocation, I²C/BQ/USB extraction, signing purge and verification receipts | Connected | The previous source checkpoint passed host and target checks; the new integration must retain these in its exact-head runs. Target reference reports record selected linked paths. Arbitrary indirect calls and physical PHY behavior are not inferred from those reports. |
| Bounded archive/loader/network work and approved SD/shared-SPI failure | Connected | `NativeOnlineRtePackageInstall.h` caps archive bytes/deadline; `HttpDownloader.cpp` bounds the selected HTTP worker; `esp_elf.c` and `SdVfs.cpp` bound loader chunks and descriptor waits. `SdSpiFault.cpp`, `HalStorage.cpp` and exact pinned per-build SDK patches park/retain the owner on an unrecoverable SD/shared-SPI stall until manual reboot. The previous source checkpoint passed host fault fixtures and both target builds; the new integration must retain these checks. No arbitrary synchronous SDK-call preemption or automatic reboot is claimed. |
| Changed-source and loose-to-ZIP version identity | Connected | The local `--zip-transition` version guard passed against new master; its new exact-head job remains to run. Model Viewer ZIP 1.2.8, Risc Strike ZIP 1.0.4 and display provider ZIP 0.1.4 exceed master's respective loose/source identities. The index updater continues to reject conflicting published same-version records. |
| Independent receipt-loader review | Unperformed verification | A prior cybersecurity review request was denied. It was not retried or replaced. This is not a demonstrated missing production path or an owner hardware prerequisite. |
| USB cable/PHY/power, real media stalls and power-loss behavior | Physical only | No U1 physical PASS is claimed by these CI runs. Record any reproducible defect against PR #96. |

No specific unimplemented U1 production path remains identified in the [acceptance matrix](U1_ACCEPTANCE_MATRIX.md).
The approved fail-closed policy supersedes older loader/HTTP notes that call
SD/SPI termination open. The HTTP dependency's DNS/socket/TLS source has finite
selected mechanisms; this is not a universal SDK interruptibility guarantee.

## Candidate inventory

Use the latest successful PR #96 **exact-head** runs of
`RiscRTE PlatformIO Build` and `Experimental USB ELF Provider v2`. Inspect the
Actions artifact `head_sha` and ZIP digest before extracting. The required
artifact groups are:

| Output | Artifact name in exact-head run |
|---|---|
| T5S3 Pro firmware, ELF, built apps and app ZIPs | `riscrte-firmware-t5s3-pro` |
| LilyGO EPD47 S3 firmware, ELF, built apps and app ZIPs | `riscrte-firmware-lilygo-epd47-s3` |
| Driver/service/provider ZIPs and linked experimental ELFs | `usb-v2-elves-and-hid-packages-unqualified-for-board` |
| Linked firmware and controller evidence | `u1-target-references-t5s3-pro`, `u1-target-references-lilygo-epd47-s3`, `u1-controller-references` |

The configured firmware version is **1.3.54**. Important source package
versions: `i2c-esp32s3-v2` 0.1.6, `board-power-t5s3-v2` 0.1.9,
`usb-controller-esp32s3` 0.1.20, `usb-host-v2` 0.1.5, canonical
`usb-cdc-acm` 0.1.8, `usb-cp210x-v2` 0.1.8, `usb-ch34x-v2` 0.1.7,
`usb-serial-witness` 0.1.3, `display-epd-video` 0.1.4 and `archive-zip`
0.1.0. App examples include Model Viewer 1.2.8, Risc Strike 1.0.4,
Status Bar Settings 1.0.3, Text Editor 0.2.3, Timecard 1.0.3, Hollow Trail
1.1.42 and LoRa 1.0.1. Use the manifests and ZIP catalog in the exact-head
artifacts for the full inventory; do not combine them with older branch or
published assets. An Actions artifact ZIP digest is not an individual firmware
image or package SHA; verify extracted assets against their contained records.

## One owner-directed hardware session

1. Preserve a full affected-range firmware/partition backup and SD contents;
   record board identity, current firmware, serial log and candidate hashes.
   Use the T5S3 Pro artifact for that board. Confirm baseline paper boot,
   display, Text Editor document opening and ordinary SD access before USB
   installation. With optional USB packages absent, confirm no USB fallback or
   unexpected VBUS.
2. Through the ordinary manager, install the I²C bus ELF, the BQ VBUS owner
   and any actually required board profile, then USB controller/host and class
   ELFs. Check capability inventory, power/role startup and absence behavior
   after withholding each dependency. Use a known ESP32-S3-CAM CDC device for
   enumeration, Serial Monitor RX/TX, baud/control, disconnect/reconnect and
   teardown. Record CP210x/CH34x results only for devices physically present.
3. Exercise the same package engine with one app, driver, service and provider
   ZIP from the candidate artifact, offline and through an approved isolated
   online test source if available. Test an update, refused downgrade/corrupt
   input, active-use refusal, interrupted stage/recovery and preservation of the
   last good generation. Check a non-USB installed witness and scoped resources.
4. Check release/revocation and safe host/PHY/VBUS shutdown. In a controlled
   fault test, induce or observe SD/shared-SPI failure, then verify new SD/LoRa
   work refuses, live owners are retained, and only a **manual** reset/power
   cycle recovers. A frozen UI during that fault is consistent with retaining
   the failed owner; do not force task/driver teardown to redraw it.
5. Record PASS/FAIL/NOT RUN per step, actual device IDs and logs, source and
   extracted-asset hashes, installed versions, power/PHY observations and any
   rollback result. A failure stays on PR #96 for repair. No camera/default-app
   result from PR #344 substitutes for this U1 evidence.

This sheet prepares a single session; it does not authorize performing it.
