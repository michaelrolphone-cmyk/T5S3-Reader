# Historical architecture snapshots — NON-NORMATIVE

These files are **verbatim copies** of specifications before the hardware-agnostic driver-boundary correction. They are retained to preserve complete technical detail, former acceptance matrices, migration history, and identifiers that still refer to actual legacy code.

**Do not implement these documents as current architecture.** Their requirements for a framework-owned USB core, USB class proxy, UART/bus manager, hardware resource manager, Wi-Fi driver, display driver, or hardware-specific provider loader conflict with the current contract.

Read [../../RISCRTE_PLATFORM_SPEC.md](../../RISCRTE_PLATFORM_SPEC.md), [../../HARDWARE_AGNOSTIC_DRIVER_BOUNDARY.md](../../HARDWARE_AGNOSTIC_DRIVER_BOUNDARY.md), and the updated version of each named specification in `docs/` before using any details here. Use these archived documents only for legacy implementation identification, test behavior, compatibility, and regression planning. Hardware/device implementation and physical ownership belong to installable provider ELFs; the compiled runtime knows capability IDs only as opaque strings.

The archived files were copied directly from their original Git blobs; no content was paraphrased, truncated, or otherwise edited during archival. Architecture changes in this branch affect documentation only, not executable code or published firmware.