# Reader network-link ownership: handoff assessment

Status: source-pinned assessment, not an implemented cutover or acceptance.
The existing hardware ownership specification remains authoritative; this
assessment introduces no new runtime, hardware exception, or provider policy.

## Reviewed source

Reviewed on 2026-10-22 against the public branch heads:

- Reader master: `9c8783d6f20659a514bfaeb3847a77c7213dc4f6`.
- Runtime main: `13eca114cb58469cf6004fafc3da84fbd9d29beb`.
- Drivers main: `a7ae39994603c4545a3c014f09051ecfc12084d2`.

Only public source at these commits informed the boundary. No unpublished
storage successor, Runtime owner work, hardware operation, merge, or release
is part of this assessment.

## Existing implementation boundary

Reader's [NetworkService header][service-header] explicitly describes a
compiled-in provider and defers discovery, handles, and manifest gating.
Its `InterfaceApi` exposes only copied state and an unchecked `void shutdown()`;
its Wi-Fi table has no consumer context or operation token.
[NetworkService.cpp][service] directly obtains both tables from
`Esp32NetworkProvider`. These tables are not installed-provider grants.

The [legacy provider][legacy] owns Arduino Wi-Fi initialization, scanning,
connection and AP setup. Shutdown ignores teardown results, while scan
unconditionally powers Wi-Fi off and back on. The wrapper therefore does not
establish exclusive consumer custody or prove a transferable idle state.

Reader already has the necessary **generic provider lifetime mechanism**:
[InstalledProviderGraph][installed] selects an admitted capability and returns an
exact lease. Release clears the caller's lease only after success.
[GraphV2][graph] revokes a pending-release grant, retains its generation for
retry, and does not force-unmap a provider whose quiescence fails. A successful
release with another live consumer does not assert physical radio idle.

These generic mechanisms should be reused rather than replaced. However,
neither the Reader SDK/provider manifests nor the reviewed Drivers source
supplies an installed `network.default` or `network.wifi.control` implementation
with the checked link/power lifecycle needed here. The names in NetworkService
comments are not evidence that these capabilities are published.

## Concrete consumer custody gaps

The migration cannot end at replacing NetworkService's table lookup:

- [NativeNetworkBridge][http] reads/restores ESP Wi-Fi power-save mode around
  synchronous HTTP requests. This is protocol-owned hardware policy, not a
  shared provider activity grant.
- [CrossPointWebServer][server] directly checks Wi-Fi mode, disables Wi-Fi sleep,
  and enables auto-reconnect. Its server lifetime must be represented by a real
  link-consumer lifetime before radio power can be arbitrated below it.
- [WifiSelectionActivity][picker] starts scans through the raw Wi-Fi table.
  The provider's scan restart can disrupt another active consumer.
- [NativeKOReaderBridge][koreader] powers Wi-Fi off directly at authentication
  session end, outside NetworkService.
- [NativeWebServerBridge][native-server] owns another direct shutdown/AP path.
- Activity exits such as [CalibreConnectActivity][calibre] stop their server and
  then call unchecked global network shutdown, rather than releasing an exact
  consumer grant.

These are actual production consumers, not only synthetic fixture cases.
Existing OTA power-save cleanup is a separate change and is not duplicated here.

## Why a NativeRadio adapter is not an established handoff

The independent Runtime already provides [copied asynchronous radio requests,
operation IDs and owner service leases][radio-async]. Those semantics are not the
Reader `InterfaceApi`, and copying their implementation into Reader would create
another owner rather than binding a shared capability.

At the pinned Runtime source, [NativeRadio begin][native-radio] requires
`esp_wifi_get_mode` to report `ESP_ERR_WIFI_NOT_INIT` and creates its own default
event loop. Reader's unchecked Arduino `WiFi.mode(WIFI_OFF)` does not prove that
Arduino callbacks, event-loop state and SDK ownership have been relinquished.
The Runtime's short owner service lease also cannot simply be held across a
long-running HTTP/server operation: its contract says to finish in the same
owner turn without a wait or yield.

Consequently, no code here attempts to start NativeRadio after the legacy
Reader provider, imports a new per-radio firmware bridge, or treats a software
lease as physical ownership. The [hardware-agnostic boundary][boundary] already
rules out those substitutes.

## Minimal integration seam and missing contract

The coherent Reader integration seam is the existing generic installed-provider
graph, not a new singleton radio/power manager:

1. A real installed provider supplies the link interface through the existing
   manifest/API-version/struct-size boundary.
2. The Reader-side consumer holds that exact graph lease for its operation or
   service lifetime, drains its own protocol work before release, and preserves
   failed-release custody instead of discarding the lease.
3. Link/radio activity, shared power policy and checked hardware teardown remain
   within that provider and its declared lower-capability dependencies.
4. Old Arduino admissions stop before ownership transfer; replacement admission
   follows verified legacy quiescence, never a delay or inferred success.

This is an implementation boundary derived from the existing specification,
not an implemented ABI. The exact unresolved input is **which installed
provider package and versioned link contract Reader will consume**. That
contract must make the following existing requirements implementable:

- Per-consumer lifetime and revocation, including request/server cancellation
  and shutdown retry without losing the retained grant.
- Provider-owned activity/power arbitration and checked cleanup outcomes.
- The relationship between generic link custody and optional Wi-Fi admin
  operations, so scan/connect/AP changes cannot invalidate another owner.
- A concrete, checked transition from the currently initialized Arduino stack
  to the selected provider, including SDK/event-loop/callback ownership.

The reviewed source does not answer these points. A new table of function
pointers, host-only fake provider, or bool wrapper around the existing global
shutdown would not fill the missing physical-provider contract. Production
handoff edits are therefore blocked on that contract/owner decision; generic
lease mechanics themselves are available and tested.

## Executed validation

On the unmodified pinned Reader production source:

- `bash test/run_runtime_network_test.sh`: passed. This covers the current
  legacy extraction and HTTP body budget, not installed network ownership.
- `bash test/run_saved_network_fallback_test.sh`: passed.
- `bash test/run_provider_retained_boundary_test.sh`: passed with UBSan.
- `bash test/run_provider_graph_v2_test.sh`: the first graph stage passed, then
  LeakSanitizer could not run under this executor's ptrace environment.
- Rerun with `ASAN_OPTIONS=detect_leaks=0 LSAN_OPTIONS=detect_leaks=0`: the
  complete provider-graph suite passed, including copied metadata/dependencies,
  failed-start recovery, failed-teardown quarantine and retained grant retry.
  ASan/UBSan remained enabled where the script enables them; leak detection was
  not performed.

No radio was initialized, no real credentials were used, no firmware target
build was run, and no physical or power-current qualification is claimed.
These passes establish reusable generic mechanisms, not completion of the
network-link ownership task.

[service-header]: https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/9c8783d6f20659a514bfaeb3847a77c7213dc4f6/src/runtime/network/NetworkService.h#L5-L64
[service]: https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/9c8783d6f20659a514bfaeb3847a77c7213dc4f6/src/runtime/network/NetworkService.cpp#L3-L23
[legacy]: https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/9c8783d6f20659a514bfaeb3847a77c7213dc4f6/src/providers/network/Esp32NetworkProvider.cpp#L33-L111
[installed]: https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/9c8783d6f20659a514bfaeb3847a77c7213dc4f6/src/runtime/drivers/InstalledProviderGraph.cpp#L721-L797
[graph]: https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/9c8783d6f20659a514bfaeb3847a77c7213dc4f6/src/runtime/drivers/ProviderGraphV2.cpp#L293-L335
[http]: https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/9c8783d6f20659a514bfaeb3847a77c7213dc4f6/src/native/NativeNetworkBridge.cpp#L135-L151
[server]: https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/9c8783d6f20659a514bfaeb3847a77c7213dc4f6/src/network/CrossPointWebServer.cpp#L194-L217
[picker]: https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/9c8783d6f20659a514bfaeb3847a77c7213dc4f6/src/activities/network/WifiSelectionActivity.cpp#L80-L105
[koreader]: https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/9c8783d6f20659a514bfaeb3847a77c7213dc4f6/src/native/NativeKOReaderBridge.cpp#L84-L90
[native-server]: https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/9c8783d6f20659a514bfaeb3847a77c7213dc4f6/src/native/NativeWebServerBridge.cpp#L88-L104
[calibre]: https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/9c8783d6f20659a514bfaeb3847a77c7213dc4f6/src/activities/network/CalibreConnectActivity.cpp#L57-L65
[radio-async]: https://github.com/michaelrolphone-cmyk/RiscRTE/blob/13eca114cb58469cf6004fafc3da84fbd9d29beb/sdk/driver/GardenRadioAsyncV1.h
[native-radio]: https://github.com/michaelrolphone-cmyk/RiscRTE/blob/13eca114cb58469cf6004fafc3da84fbd9d29beb/src/ports/esp32s3/NativeRadio.h#L250-L265
[boundary]: https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/9c8783d6f20659a514bfaeb3847a77c7213dc4f6/docs/HARDWARE_AGNOSTIC_DRIVER_BOUNDARY.md
