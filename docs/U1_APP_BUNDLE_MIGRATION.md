# U1 app ZIP distribution closure

## Subsequent October 1 master reconciliation

Master **1e0188c1** adds Model Viewer X-button handling at loose version1.2.5.
Retain those actual source changes and advance its U1 ZIP identity to **1.2.6**.
The merged navigation driver retains its own master version and behavior.

## October 1 master reconciliation

Master `f79f72911ccb1dbd0666117370f54e040b5fc8e4` incorporates Hollow
Trail mechanics, confirmation handling and button-remap fixes. Their actual
source changes are retained. ZIP identities advance beyond those loose-package
versions: button_remap, clear_cache and ota_update **1.0.1 -> 1.0.2**;
Hollow Trail U1 **1.1.37 -> 1.1.40**, above master 1.1.38 and leaving the
separately active 1.1.39 candidate its own identity. That unmerged candidate's
source is not incorporated. Firmware inherits master 1.3.48; final U1 firmware
version reconciliation remains open. The table below is the earlier checkpoint.

## Immutable identity and version lineage

A published `(kind, id, version)` cannot change distribution format. Loose
ELF/JSON to ordinary ZIP requires a strictly newer version, even if ELF bytes
agree. Same-version replacement is rejected by the release-index updater.

Compared with integrated master `a5e2db59077cc889079668dc9cd7428b08bc32a1` and the
[published index at e495c5e1](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/e495c5e1af0029dc04e3f34eff109f3861f2cee2/release-index.json)
(2026-10-01T01:11:32Z; reread October 1). Index JSON SHA-256:
`68102238a6e079c335591dc84f25f518eaf45959c38e1232d9799f346607b216`.
App Store 1.0.8 and Driver Manager 1.0.7 already exceed published versions and
remain unchanged. All other local apps exceed both source/master and published
versions. Package Manager's stale U1 version is reconciled with published 1.1.0.
Hollow Trail includes the newly integrated master gameplay changes; its ZIP
version is 1.1.37, newer than master's loose-distribution 1.1.36.

The 36 JSON edits are version-only. No app C behavior is changed by the ZIP
closure itself; Hollow Trail runtime/content changes are inherited from master.
Existing mandatory app requirements are now copied into ordinary manifests
(currently Model Viewer input.touch.raw). The shared build/runtime changes are
separate from version-only edits.

| ID | Previous U1 | Master | Published | ZIP version |
| --- | --- | --- | --- | --- |
| battery | 1.0.1 | 1.0.1 | 1.0.1 | 1.0.2 |
| button_remap | 1.0.0 | 1.0.0 | 1.0.0 | 1.0.1 |
| clear_cache | 1.0.0 | 1.0.0 | 1.0.0 | 1.0.1 |
| esp_rom_flasher | 1.1.0 | 1.1.0 | 1.1.0 | 1.1.1 |
| file_browser | 1.3.1 | 1.3.1 | 1.3.1 | 1.3.2 |
| file_transfer | 1.0.0 | 1.0.0 | 1.0.0 | 1.0.1 |
| font_manager | 1.0.1 | 1.0.1 | 1.0.1 | 1.0.2 |
| font_selection | 1.0.0 | 1.0.0 | 1.0.0 | 1.0.1 |
| gnss_stream_diagnostic | 0.1.1 | 0.1.1 | 0.1.1 | 0.1.2 |
| gps | 1.0.0 | 1.0.0 | 1.0.0 | 1.0.1 |
| hollow_trail | 1.1.35 | 1.1.36 | 1.1.35 | 1.1.37 |
| image_viewer | 1.1.0 | 1.1.0 | 1.1.0 | 1.1.1 |
| koreader_auth | 1.0.0 | 1.0.0 | 1.0.0 | 1.0.1 |
| koreader_sync | 1.0.0 | 1.0.0 | 1.0.0 | 1.0.1 |
| language_settings | 1.0.0 | 1.0.0 | 1.0.0 | 1.0.1 |
| llm_ask | 1.0.0 | 1.0.0 | 1.0.0 | 1.0.1 |
| lora | 1.0.0 | 1.0.0 | 1.0.0 | 1.0.1 |
| mahjong | 1.0.0 | 1.0.0 | 1.0.0 | 1.0.1 |
| model_viewer | 1.2.4 | 1.2.4 | 1.2.4 | 1.2.5 |
| opds_settings | 1.0.0 | 1.0.0 | 1.0.0 | 1.0.1 |
| ota_update | 1.0.0 | 1.0.0 | 1.0.0 | 1.0.1 |
| package_manager | 1.0.1 | 1.1.0 | 1.1.0 | 1.1.1 |
| risc_strike | 1.0.2 | 1.0.2 | 1.0.2 | 1.0.3 |
| rom_manager | 1.0.18 | 1.0.18 | 1.0.18 | 1.0.19 |
| sd_firmware_update | 1.0.1 | 1.0.1 | 1.0.1 | 1.0.2 |
| sd_list | 1.0.0 | 1.0.0 | 1.0.0 | 1.0.1 |
| serial_monitor | 1.2.6 | 1.2.6 | 1.2.6 | 1.2.7 |
| settings | 1.0.0 | 1.0.0 | 1.0.0 | 1.0.1 |
| springboard | 1.3.0 | 1.3.0 | 1.3.0 | 1.3.1 |
| status_bar_settings | 1.0.0 | 1.0.0 | 1.0.0 | 1.0.1 |
| text_editor | 0.2.1 | 0.2.1 | 0.2.1 | 0.2.2 |
| time_zone | 1.0.0 | 1.0.0 | 1.0.0 | 1.0.1 |
| timecard | 1.0.1 | 1.0.1 | 1.0.1 | 1.0.2 |
| usb_debug | 0.1.1 | 0.1.1 | 0.1.1 | 0.1.2 |
| web_server | 1.0.0 | 1.0.0 | 1.0.0 | 1.0.1 |
| wifi_settings | 1.0.2 | 1.0.2 | 1.0.2 | 1.0.3 |

## Connected paths and compatibility

The existing builder/exporter produces canonical ELF, sidecar and manifest in
`dist/release-app-packages`; driver artifacts retain their separate directory.
Normal independent releases select one exact app ZIP. Offline release-plan
validation checks source sidecar, identity, mandatory requirements, CRC, hashes,
canonical archive repack and restored app/driver catalog isolation.

The index and runtime accept explicit application ZIP records, immutable per-ID
release tags and URLs. Numeric versions, legacy barriers and equal-version
conflicts use the existing common merge rules. Legacy loose app/driver input
and external GameBoy source/tag semantics remain supported. The legacy app
adapter validates and skips ZIP records instead of misusing their archive hash
as an ELF hash. The common package API handles ZIP installation/recovery.
Canonical app launch requires sidecar version to match the ordinary identity.

Compatibility means old package inputs on the new firmware. Old deployed
firmware is not claimed to consume ZIP records. Future release/catalog
transition must pair compatible firmware and policy; no live index, release,
tag or deployment is performed here. External GameBoy remains loose.

## Evidence and remaining work

Host coverage includes real packer/record/index/runtime URL round trips,
app/driver artifact restore isolation, hash/sidecar corruption, ZIP symlinks,
partial transfer, allocation failures, invalid refresh clearing and recovery,
legacy GameBoy sync and publish retry behavior. Target CI now exports and
validates every locally built app ZIP on both boards; results remain pending
for this combined change until its exact remote head passes.

Remaining U1 gaps: nested resources/scoped access, archive service, CDC
migration, generation-bound verification receipts and bounded-I/O/link evidence.
Signing removal is separately approved, audited and integrated; ordinary SHA,
permissions, import/version checks and rollback remain intact.

October1 master8157c0bb backmerge retains the owner-merged Hollow Y correction.
Hollow source lineage is master1.1.39 → cumulative U1 ZIP candidate1.1.40.
Unmerged schoolroom1.1.41 is not included; reconcile again if that lineage lands
before the final U1 candidate. No release/catalog publication is implied.

October1 version coordination reserves Timecard1.0.2 for the independent
clock-failure repair. U1's unreleased ZIP candidate advances1.0.2 →1.0.3;
master/published baseline remains1.0.1 at this checkpoint. The repair is not copied before merge; integrate it through actual master
when the owner lands it, then recheck lineage before the final candidate.

October1 subsequent master5c1284bf includes the owner-merged schoolroom1.1.41
and its Y correction. Hollow candidate advances1.1.40 →1.1.42, preserving the
merged behavior. The earlier table is the initial ZIP-transition snapshot;
these dated reconciliation entries supersede its candidate values. Timecard's
current U1 candidate remains1.0.3; its independent1.0.2 repair is not yet in this
master snapshot.

October1 masterd4df4609 backmerge integrates the actual Timecard341 app repair:
one local-datetime snapshot and a visible clock-unavailable refusal. Its1.0.2
release is published; U1 retains1.0.3 for immutable ZIP distribution. Contrary
to the earlier coordination description, the merged diff does not change
NativePlatformBridge. Native and actual sidecar/pair regressions are rerun on
this integrated source. Firmware candidate1.3.50 exceeds published/master1.3.48
and the separately reserved display1.3.49; no release is requested or performed.

**Implementation In Progress**
