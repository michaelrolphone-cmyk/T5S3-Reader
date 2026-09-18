# Serial Monitor startup incident — September 18, 2026

## Change contract

Objective: opening Serial Monitor must promptly present an interactive waiting view, acquire installed `serial.port` without repeating catalog-wide SHA-256 work, and continue discovery when a device is connected after launch. The installed ELF provider chain remains the only normal hardware implementation. Preserve directory inventory/hash, privileged-import and loader validation, exclusive physical leases and safe teardown.

Required: avoid repeated integrity scans during a single provider start; bound/coordinate long reads; report each startup/failure stage; recover capability acquisition without requiring app relaunch; bump any changed app manifest version. No firmware-side USB hardware implementation or compiled fallback, no signing policy change, no new package format. Hardware acceptance requires a disconnected launch followed by hot-plug and a connected launch, neither of which is established by CI.

## Observed behavior and source investigation

The firmware reports `SERREF action=context-begin` on app launch, then produces no acquisition result for minutes. An eventual Serial Monitor view reports inability to acquire `serial.port` with or without a connected ESP32-S3-CAM. A missing `solid:f287` glyph is a separate rendering issue.

The legacy serial provider registration invokes `T5UsbApi.supported()` from its availability callback; the provider selector calls availability again; serial acquire calls `supported()` again; and opening the stream pair invokes `supported()` yet again. The implementation of `supported()` runs `installedCapabilityVersion("serial.port")`, which recursively resolves a newly hashed installed-directory snapshot per call. After this, `InstalledProviderGraph.prepare()` constructs another snapshot and verifies and registers each package. This duplicates expensive SD traversal and hashing in the UI task before `acquire` returns. The existing app initially called acquire before drawing its first screen and exited to an error view on failure rather than continuing an acquisition retry.

## Required diagnostic differentiation

Serial logging and user-visible state must distinguish capability availability, snapshot/graph registration, host activation, stream creation, and device enumeration. An absent physical device must be a WAITING state after successful host startup, not a serial capability acquisition error. Unknown provider/graph failure must report an error, not fabricate a connected state.

## Verification

Host checks: native Serial Monitor reconnect test, provider-graph import/integrity and package verification regression, both board firmware + app ELF build and structural validation. Only on-device tests can validate USB host and ESP32-S3-CAM enumeration, VBUS behavior, and real-time launch behavior.
