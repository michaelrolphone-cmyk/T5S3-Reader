# Recommended driver/provider guidance

Read root [`AGENTS.md`](../AGENTS.md), [`docs/RISCRTE_PLATFORM_SPEC.md`](../docs/RISCRTE_PLATFORM_SPEC.md), [`docs/PACKAGE_IDENTITY_VERSION_POLICY.md`](../docs/PACKAGE_IDENTITY_VERSION_POLICY.md), [`docs/DRIVER_PLATFORM_REUSE_ACCEPTANCE.md`](../docs/DRIVER_PLATFORM_REUSE_ACCEPTANCE.md), and the current [U1 milestone](../docs/NEXT_HARDWARE_TEST_MILESTONE.md) as the normal context for changes under `Drivers/**`.

This file records preferred driver/provider engineering practice. Use it as the default when the current task does not specify a different approach.

## USB and device-lifecycle recommendations

Before ordinary USB controller, power, discovery, class, or input-driver work, [USB Host Startup and Detection](../docs/USB_HOST_STARTUP_AND_DETECTION.md) is the preferred reference.

The intended ordering is to configure host PHY/role/pull-downs before VBUS, prepare event handling before attachment is possible, and test ordering at the real power-provider call. Diagnose physical attachment separately from enumeration, class binding, and reports. Prefer retaining unsafe DMA/lease/dependency ownership through failed cleanup. Gamepads normally publish current state; keyboards normally retain ordered buffered events. Preserve the owner-confirmed 0.1.14 auto-connect behavior unless the current task intentionally changes it, and generally avoid app-specific USB resets or firmware hardware bridges.

## Version and identity recommendations

For a distributable driver/provider change, the normal release practice is to increment that package's manifest `version` in the same PR or change set. Prefer comparing against both the merge base and last actually published version, using numeric `MAJOR.MINOR.PATCH`, and recording `id old -> new`.

A firmware version, `driver_abi`, capability API, source directory, release tag, or ELF hash is not normally treated as the package's own version. One version increment per cumulative unreleased update is usually sufficient. Avoid changing unrelated package versions solely because firmware is released.

Package IDs are intended to represent stable upgrade lineages. The preferred practice is to keep the same manifest `id` across rewrites, ABI changes, profile updates, and normal releases while versioning the package and independently declaring/checking `driver_abi`, architecture, requirements, and provided capabilities. Avoid `-v2`/`-v3` IDs or duplicate rows merely to bypass normal update logic. Truly separate concurrently installable products should have distinct documented functions and binding policy.

A current task can intentionally use a different convention for a prototype, migration experiment, compatibility test, recovery action, or other task-local reason.

## Preferred U1 CDC remediation

The normal U1 direction is to reconcile:
- `Drivers/usb_cdc`: `usb-cdc-acm@0.1.0`, ABI 1, firmware-proxy implementation.
- `Drivers/usb_cdc_v2`: `usb-cdc-acm-v2@0.1.0`, ABI 2, functional class ELF.

The preferred end state is a strictly higher-version replacement using canonical ID `usb-cdc-acm`, with an audited legacy-install migration. The normal cleanup includes removing the old production proxy and forked catalog/release identity, updating references and asset paths, and adding automated version/identity regression coverage. Prefer preserving installed data rather than treating a file rename or deletion as a migration. The generic release builder should ideally avoid fixed `0.1.0` assumptions and hand-maintained driver lists.

Before treating ordinary driver work as complete, compare source manifest, generated `.package.json`, provider metadata, generic catalog, release asset names, installed identity, and UI version. Prefer consistency, compatibility, no unintended same-version changed bytes, safe handling of in-use or failed-quiesce replacements, and preservation of old generations on failure. Report build-time guards as passing only when they actually exist and were exercised.

## Driver-loop recommendations

Use [Bounded, Cooperative Long-Running Operations](../docs/COOPERATIVE_BOUNDED_OPERATIONS.md) as the preferred reference for enumeration, hotplug/reconnect, device I/O, retries, descriptor processing, polling, stream consumption, and provider shutdown.

Good defaults include bounded queues/memory/work/deadlines, avoiding unbounded recursion and repeated full scans, elapsed-time and work checkpoints, genuine scheduler cooperation, throttled observable progress/state changes, and explicit failure/quiescence recovery. Treat `esp_task_wdt_reset()` as watchdog service rather than scheduler yield. Prefer retaining pinned active or unsafe-to-unload providers and dependencies after failed cleanup.

These recommendations describe normal code-completion quality rather than adding extra review checkpoints, CI gates, or compulsory intermediate hardware-test gates.
