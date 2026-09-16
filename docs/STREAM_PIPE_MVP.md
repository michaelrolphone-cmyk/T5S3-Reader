# RiscRTE Stream/Pipe MVP

## Authority and target contract

This document describes the implementation subset and migration status of the RiscRTE Stream/Pipe architecture. Read [RISCRTE_PLATFORM_SPEC.md](RISCRTE_PLATFORM_SPEC.md), Part III of [PLATFORM_CAPABILITY_ROADMAP.md](PLATFORM_CAPABILITY_ROADMAP.md), [STREAM_PIPE_ARCHITECTURE.md](STREAM_PIPE_ARCHITECTURE.md), and, for application ownership, [APPLICATION_EXECUTION_CONTEXT_ARCHITECTURE.md](APPLICATION_EXECUTION_CONTEXT_ARCHITECTURE.md). Those specifications govern new functionality; this file is not a replacement for the target architecture.

The target data plane connects applications, services, devices, providers, and storage without either endpoint depending on the other's implementation or lifetime. Streams use bounded buffering, explicit backpressure, EOF, disconnect, errors, and execution-context ownership. Typed records, transforms, structured IPC, and semantic capability acquisition remain part of the target even when missing from the original byte-stream MVP.

## Full-architecture implementation: record foundation on PR #65

**Target:** Sections 7–12, 21, 26, 30–32 and 41 of [STREAM_PIPE_ARCHITECTURE.md](STREAM_PIPE_ARCHITECTURE.md) require typed records with versioned schema identities, indivisible delivery, bounded memory, schema-aware pipe validation, source ordering and backpressure. Record producer/consumer lifetimes must use the same execution-context and generation-safe handle model as byte streams.

**Current branch implementation:** `src/runtime/streams/RecordQueue.h` adds a firmware-owned, bounded record FIFO. It copies a validated versioned ASCII schema name into runtime storage; allocates up to eight records of up to 512 bytes each, capped at 4096 payload bytes; and atomically enqueues/dequeues each record. Full queues return `AGAIN` rather than dropping a record, and short receive buffers never consume a prefix. Zero-byte records have a distinct successful status. Buffered records drain before EOF, disconnect or error. It reports per-record and byte counters, high-water marks and explicit cleanup.

`src/runtime/streams/StreamRuntime.h/.cpp` now embeds these queues inside the **same** registry slots used for byte streams, with the same owner identity and generation-safe handle lifecycle. `Registry::recordBuffer`, `readRecord`, `writeRecord` and `recordInfo` are currently **firmware-internal** methods. Type metadata is copied to caller-owned output. `Registry::connect` refuses record-to-byte and unequal-schema links before allocating a pipe. Its existing rotating scheduler transfers one whole record per turn; if the destination is full, the complete record stays in its bounded staging buffer. A separate `recordPending` bit preserves even zero-byte records. Pipe leases, pause, cancellation, EOF/failure propagation, close and owner release work through the shared registry. `test/streams/record_queue_test.cpp` and `test/streams/record_registry_test.cpp` exercise queue semantics and actual registry pipe integration through `test/run_stream_test.sh` with sanitizers. Tests or CI are only validated after the corresponding job reports success.

**Not yet exposed:** Native ELFs still obtain only `t5_stream_get_api(1)` and cannot open, read, write or inspect records. The current provider attach vtable is deliberately byte-only; typed external providers require an explicit record adapter rather than interpreting their bytes as records. Next: add an append-only versioned record API while preserving v1 layout, validate source maximum-record size against destination acceptance at connection, add packet-provider registration and at least one real semantic producer, and test application unload/hotplug with native callers. Do not call record streams publicly functional before this work. Schema-compatible pipes currently require exact identity; alternate schemas need a declared transform, not an implicit conversion.

**Later full-spec phases:** declaratively typed transforms and unload-safe provider lifecycle; tee with explicit coupled/independent policies; ordered record merge with source identity/timestamps; additional overflow policies and drop counters; event-driven readiness/watermarks; stable discovery and quotas; optional zero-copy, replay/ranges and persistent topology. Neither PR #65 nor the byte MVP claims these are complete.

## Current byte-stream compatibility implementation

The deployed additive ABI is `t5_stream_get_api(1)` in `lib/NativeApps/include/T5StreamApi.h`; the `T5*` name is a historical compatibility identifier, not the platform name. The C API uses `api_version` and `struct_size`; ELFs must validate the members they use and set an appropriate manifest firmware floor. Handles encode slot and generation, belong to the active invocation, and must not be saved as durable IDs or reused after a keyboard/system-UI unload-and-resume handoff.

`src/runtime/streams/StreamRuntime.cpp` owns at most 12 streams and four pipes. Byte FIFO sizes are 1–4096 bytes; staging and individual byte transfers are bounded to 512 bytes. The deployed ABI exposes only byte streams and the lossless `BLOCK_PRODUCER` pipe policy. A full destination applies backpressure rather than dropping bytes. Each active pipe exclusively leases the source read side and destination write side; self-pipes and cycles are rejected. `AGAIN`, EOF, disconnect, timeout, permission failure, stale/invalid handle, busy, cancellation, resource exhaustion, and I/O errors are distinct results.

One firmware scheduler advances bounded work across active pipes with rotating fairness, not a task per pipe. Pipe pause/cancel/close affect the connection, not endpoint ownership. Cancel discards pending pipe bytes. Destination completion is separate: require `T5_PIPE_DONE`, release the write lease through `pipe_close`, and check `finish(destination)` before considering a staged file complete. Source EOF or partial transfer is not success.

### Existing providers

- `open_buffer`: bounded FIFO.
- `open_file(FILE_READ)`: sequential SD input with bounded seek within initial length. `open_file(FILE_CREATE_NEW)`: exclusive new file that never truncates existing content. `finish` reports close/commit-to-file errors.
- `open_usb`: compatibility access to a borrowed duplex USB service; closing the stream does not stop VBUS or the USB host. New serial consumers should instead acquire semantic `serial.port` and use its separate RX/TX streams; DTR/RTS and line coding remain control-plane operations.
- `open_http`: one asynchronous HTTP response-body stream through a 4096-byte FIFO. A transient firmware network task uses the existing blocking HTTP client, redirect behavior, and TLS policy; the shared pipe scheduler remains a separate task. HTTP v1 has a single-request `BUSY` restriction and no Basic Auth input.

File streams do not install packages, verify manifests, atomically rename files, or roll back updates. Those are higher-level installer responsibilities.

### Ownership and semantic serial status

`NativeAppHost` starts a fresh stream execution owner per ELF invocation and tears it down before loader cleanup. Execution-context cleanup releases serial leases, pipes, and stream handles. Provider vtables are firmware-only and no app callback or pointer may survive ELF unload. Granular per-manifest capability authorization is not implemented by the byte-stream compatibility API.

The Serial Monitor initially migrated terminal RX/TX and passive baud/framing detection to byte streams. Subsequent PRs #61 and #62 introduced semantic `serial.port` leases, a provider resolver, generation-safe device and lease lifecycles, and split RX/TX streams. The Serial Monitor now acquires `serial.port` instead of treating `open_usb` as the target architecture. The ESP ROM provider also consumes a serial lease and borrowed firmware file stream; the Flasher ELF keeps file selection and UI, not USB/protocol ownership. Hardware acceptance for these paths is still outstanding.

## App Store migration: merged via PR #63

**Merged implementation, not a claim that it has been released or physically validated.** The App Store ELF (`Apps/app_store.c`) already uses framework list/navigation UI and delegates catalog/installation to `NativeAppHost.cpp`. Its catalog policy, aggregate index fallback, manifest/version compatibility checks, existing `.part`/`.bak` install/rollback flow, and UI are preserved. No public app ABI or app ELF source change is required.

The source still calls `HttpDownloader::fetchUrl` and `HttpDownloader::downloadToFile` in the app-host code. Those calls select the runtime data plane when made from an active native-app invocation without Basic Auth. The HTTP worker itself executes on a different firmware task and retains the established `HTTPClient` implementation, avoiding a recursive attempt to open an HTTP stream. Firmware callers without an active invocation and requests requiring the unsupported HTTP-stream Basic Auth parameters retain the original compatibility implementation.

### Metadata/data path

`HttpDownloader::fetchUrl(url, Stream&)` obtains `t5_stream_get_api(1)`, opens an HTTP body stream, and reads in at most 512-byte chunks into the existing caller-supplied parser. The release-assets parser still consumes incrementally rather than constructing a second release-sized string. The aggregate catalog and individual manifest `std::string` fetches take the same stream route, capped at 64 KiB for native metadata strings; existing aggregate/manifest schema validation applies afterwards. A zero-byte response, read failure, partial parser write, cancellation, or 60-second period without transfer progress fails rather than masquerading as EOF. The aggregate fast path and per-manifest fallback remain intact.

### Binary/staging data path

For native-invocation downloads with a new `*.part` destination, `HttpDownloader::downloadToFile` calls the reusable, Arduino-independent `src/runtime/streams/HttpStreamTransfer.h` helper. The helper acquires an exclusive `FILE_CREATE_NEW` destination and an HTTP source, connects them using `BLOCK_PRODUCER`, observes `pipe_info` with cooperative 5-ms yielding and watchdog service, and treats only `T5_PIPE_DONE` without errors as success. It then closes the pipe to release the write lease and requires successful destination `finish`. The wrapper reopens the staged SD file and checks that its size equals `bytes_transferred`; the existing app-host installer independently compares that size with the GitHub release asset's declared length when present. Zero-byte transfers fail. Failure destroys both stream handles and any pipe, reports an error, removes the staged file, and does not advance the installer to rename/commit.

The host validates the selected manifest and re-fetches its sidecar before downloading the ELF; the original safe-name and compatibility requirements remain. The host still removes abandoned staging paths, stages validated JSON, recovers old `.bak` files, and commits/rolls back the final ELF/sidecar pair. The transport helper does not grant permission, choose packages, or perform renames. Native `.part` operations never use the legacy download path as a fallback after a stream failure. Other firmware download destinations retain compatibility behavior until separately migrated.

The implementation does **not** introduce an additional per-pipe task or whole-ELF allocation. The only HTTP worker remains the existing one from `open_http`, with its 4096-byte FIFO; the pipe stages at most 512 bytes. A closed stream handle can precede completion of a blocked network operation, so immediate retry may briefly encounter the existing `BUSY` restriction. HTTP security/TLS settings are inherited from the original client; PR #63 did not add certificate validation or new URL permissions. The historical installer rollback behavior is preserved rather than replaced by a new package manager.

### Validation and acceptance

`test/streams/http_transfer_test.cpp` exercises the **production** transfer helper against `StreamRuntime::Registry` with fake providers: 10,000 ordered bytes across a slow sink and repeated backpressure; bounded metadata chunks and max-size enforcement; source failure, partial destination failure, exclusive-create conflict, destination `finish` error, stalled transfer timeout, cancellation, and handle release. `test/run_stream_test.sh` invokes it under AddressSanitizer/UndefinedBehaviorSanitizer alongside existing registry and bridge tests. PR #63 passed the host suite and both board firmware/app builds before merge. The test is not a full end-to-end SD transaction test; original installer rename/rollback and full HTTPClient behavior also require regression and board acceptance.

On-device acceptance must cover real saved Wi-Fi connection; latest-release and aggregate catalog load; fallback when `app-catalog.json` is absent; manifest version matching; ELF download larger than the FIFO; chunked and content-length responses; TLS/redirect errors; slow-SD backpressure; unplugging the SD card; interrupted download/retry; partial rename recovery; and checking the final ELF/JSON pair is launchable. Capture state, transfer counts, errors, and memory/resource recovery without marking tests passed unless actually run.

## Subsequent architecture work

Application-facing record streams and typed providers, transforms, tee/merge, explicit drop policies and diagnostics, zero-copy pools, range/replay, discoverable logical topology, provider-ELF registration, per-manifest permissions, unified resource accounting, package-manager installation and stronger HTTP worker cancellation remain outstanding. Stream payload transfer does not itself supply package integrity/hash verification or certificate trust; those belong to the appropriate platform services. The App Store still has a separate UI limitation: firmware indexes up to 128 release ELF assets while its native UI renders at most 64 rows.

This document describes implementation and outstanding acceptance; where it conflicts with the master specification, roadmap, or full Stream/Pipe architecture, the authoritative target documents govern new work.
