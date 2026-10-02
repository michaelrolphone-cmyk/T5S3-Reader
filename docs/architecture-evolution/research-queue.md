# Focused research queue

Analysis only, under the existing U1–U4 design and the owner's current order: baseline → actual X4 → T5S3 performance → hardware-specific cleanup and contingent driver/PaperSpace extraction. No new implementation, device operation or milestone acceptance is authorized.

## 1 Actual board bootstrap and compatible recovery — R1

Reuse merged verified-byte loading, SD installed-app resolution and CAM headless runtime. For the actual X4, distinguish its embedded diagnostic entry from an ordinary installed app/device path. The current public display/input/clock headers match master; reconcile the older branch against the chosen baseline before calling anything an ABI defect. Verify actual controller/layout, first provider bytes, required storage and memory classes, physical owner and exclusive handoff. Do not create another loader, manager or permanent firmware hardware fallback [E22](evidence.md#e22-integrated-bootstrap-is-a-firmware-and-package-contract-not-an-abi-rewrite).

PR347's old-firmware/new-package restore failure makes the compatible **firmware + installed package/dependency generations + startup selection** the recovery baseline. Preserve existing files and prove the restored combination boots; firmware readback alone cannot establish that. Its later reported pass remains CAM-specific, and its merge/publication awaits explicit approval.

The SD/shared-SPI manual-reboot retention policy is approved and implemented in merged U1. Retire the pending policy request; retain physical fault coverage and ownership limits as separate evidence. Do not edit closed PR96. No-SD and zero-PSRAM remain separately unproven, not automatic new scope. The LittleFS host result still supports reuse but does not close its failed-close/VFS or physical-backend questions.

**Evidence that changes R1:** one declared actual-board path works and recovers with coherent firmware/packages and exclusive ownership; a named dependency, controller/layout or memory failure identifies the remaining gap. Repeating CAM's completed execution stages adds little.

## 2 Localize the reported T5S3 regression — R3

After baseline/X4, compare exact flashed image and installed package identities against the owner's working reference. Record cold boot-to-splash separately from Home/clock updates and retained timer wake. The approximately one-minute splash and three-minute clock update are owner reports; this review has no verified cause.

Paper default-app selection occurs after HalSystem, storage, settings/RTC, display and startup UI work. Identify time spent before first presentation versus admission/SD, rendering, queue wait and physical settling. Preserve accepted-flip semantics, owned buffers, overlap, no-copy opportunities, input response and scene quality. Do not prescribe removal of integrity checks, controller recovery, GUI extraction or PaperSpace as a diagnosed fix.

**Evidence that changes R3:** a reproducible stage accounts for the delay, and a scoped correction improves that path without losing ownership, visual behavior or daily app/clock/tool flows. CAM CI and host rendering throughput are not T5S3 timing evidence.

## 3 Retained external developer round trip — R4

Keep one clean build → canonical package → ordinary install/launch → newer update → failed-update recovery with exact toolchain/runtime/dependency/source identities. Reuse merged U1 machinery and completed CAM evidence. This remains useful for U2 independent delivery but does not outrank the owner's present device/performance sequence or justify a second installer.

## 4 Product boundary and CrossPoint behavior — R2

Preserve the [pinned feature comparison](crosspoint-parity.md), reading state and actual book/render/page/save/exit/resume and transfer/settings flows. Keep U3 GUI compiled but optional; defer full driver/PaperSpace/product extraction to its contingent owner scope. A thin adaptation preserving behavior is preferable to an unsupported wholesale rewrite.

## Maintenance rule

Check live PR state and ownership before writing. Keep three to five ranked ideas, separate roadmap design, implemented source, exact-head build evidence, physical observations and owner acceptance. Historical entries remain dated. Only docs/architecture-evolution belongs to this task.
