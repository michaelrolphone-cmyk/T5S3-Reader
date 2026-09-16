# RiscRTE Stream/Pipe MVP

## Authority and target contract

This is the implementation-state and migration document for the first RiscRTE Stream/Pipe subset. Read [RISCRTE_PLATFORM_SPEC.md](RISCRTE_PLATFORM_SPEC.md), Part III of [PLATFORM_CAPABILITY_ROADMAP.md](PLATFORM_CAPABILITY_ROADMAP.md), and [STREAM_PIPE_ARCHITECTURE.md](STREAM_PIPE_ARCHITECTURE.md) first. In that order, those documents govern new architecture; this document describes what exists, what remains compatible, and how to complete the initial migration. For application ownership and resource mediation, also follow [APPLICATION_EXECUTION_CONTEXT_ARCHITECTURE.md](APPLICATION_EXECUTION_CONTEXT_ARCHITECTURE.md).

The target is a runtime-owned, transport-independent data plane connecting applications, services, devices, providers, and storage without either endpoint depending on the other's implementation or lifetime. Streams use bounded buffering and explicit backpressure, EOF, disconnect, error, ownership, and revocation semantics. The target also includes typed records, compatible transforms, structured IPC, and semantic capability acquisition. Do not interpret an MVP omission as a permanent architectural constraint.

## Current implementation (verified against `master` after PR #62)

### Byte-stream compatibility ABI

The deployed additive ABI is `t5_stream_get_api(1)` in `lib/NativeApps/include/T5StreamApi.h`. `T5*` is a historical compatibility identifier, not the platform name. The C table has `api_version` and `struct_size`; consumers must validate the members they use and declare an appropriate minimum firmware version when their ELF begins requiring them. Handles encode a slot and generation, belong to the active invocation, and are not persistent IDs.

`src/runtime/streams/StreamRuntime.cpp` provides at most 12 simultaneous streams and four pipes. Memory FIFO capacities are 1–4096 bytes; pipe staging and individual transfers are bounded to 512 bytes. The implemented kind is byte streams only, with `BLOCK_PRODUCER` as the only accepted pipe policy. Full buffers exert backpressure instead of silently dropping bytes. Each active pipe exclusively leases the source read side and destination write side; self-pipes and cycles are rejected. `AGAIN`, EOF, disconnect, timeout, denial, invalid/stale handle, resource limit, busy, cancellation, and I/O failures have distinct results.

One firmware scheduler pumps bounded work across active pipes with rotating fairness, not one task per pipe. Pipe pause, cancel, and close do not transfer endpoint ownership. Cancel discards pipe-pending data. A successful pipe transfer does **not** implicitly finish its destination: the caller must confirm `T5_PIPE_DONE`, close the pipe to release the destination write lease, then call `finish(destination)` and check its result. Merely observing source EOF, receiving some bytes, or calling `pipe_close` is not a successful file transfer.

### Current providers

- `open_buffer` provides a bounded in-memory FIFO.
- `open_file(FILE_READ)` provides sequential SD reads and seek within the file's initial length. `open_file(FILE_CREATE_NEW)` opens exclusively and never truncates an existing file. `finish` closes the writable file and reports its result.
- `open_usb` is the legacy compatibility entry point for a borrowed USB serial byte endpoint. Closing its stream does not stop USB host mode or VBUS; line coding and DTR/RTS remain control-plane operations.
- `open_http` starts one asynchronous HTTP response-body provider, backed by a 4096-byte read FIFO and the existing blocking HTTP client in a transient firmware network task. It inherits the current network, redirect, and TLS policy; the stream interface does not itself confer additional URL authority. Its task is distinct from the shared pipe scheduler.

The provider implementation is firmware-owned. Provider vtables and direct hardware objects are not exported to ELFs. Neither file streams nor pipes perform package installation, manifest validation, atomic commit, or rollback.

### Execution ownership and semantic serial migration

`NativeAppHost` begins a fresh stream execution owner per ELF invocation and ends it before loader cleanup. The execution context releases its serial lease and streams/pipes deterministically; provider work uses firmware serialization rather than callbacks into an unloadable ELF. Applications cannot persist stream handles across keyboard/system-UI unload-and-relaunch handoffs. Granular capability authorization by application manifest remains outstanding.

The Serial Monitor's initial migration moved terminal RX/TX and its passive baud/framing detector from a second transport path onto the byte-stream API while leaving configuration and control lines separate. Subsequent USB capability work, merged through PRs #61 and #62, introduced semantic `serial.port` leases, a provider resolver, generation-safe device/lease lifecycle, and separate bounded RX/TX streams. The Serial Monitor now uses that semantic entry point rather than treating `open_usb` as its target architecture. The ESP ROM programming provider also consumes a `serial.port` lease and a borrowed firmware-file stream; the Flasher ELF retains file selection and UI rather than owning the USB protocol. These changes do not mean all transport/device providers, all stream diagnostics, or physical hardware acceptance are complete. Keep `open_usb` documented as an existing compatibility ABI, not the preferred new application acquisition path.

### App Store: still on the legacy HTTP data path

`Apps/app_store.c` uses the framework UI and delegates catalog refresh and installation through the existing application-host API. The transfer and install implementation actually resides in `src/native/NativeAppHost.cpp`, not inside that ELF. `appCatalogRefresh()` currently uses the legacy `HttpDownloader::fetchUrl` with an Arduino `Stream` release-assets parser and aggregate/per-app manifest fetches. `appCatalogDownload()` separately fetches and validates the selected JSON manifest, downloads its ELF via **`HttpDownloader::downloadToFile`**, checks the staged file's expected length, and then performs `.part`/`.bak` rename-and-recovery logic. None of those HTTP calls currently use the RiscRTE `open_http`/pipe path. The presence of a streaming Arduino parser does not count as Stream/Pipe migration.

The aggregate `app-catalog.json` fast path, per-manifest fallback for older releases, latest-release asset indexing, compatibility/version checks, and existing framework list/navigation UI are independent behavior and must remain intact. The firmware currently indexes up to 128 release assets, while `Apps/app_store.c` renders at most 64 catalog rows; that display limit is a separate UI follow-up, not a reason to bypass the stream migration.

## Next implementation slice: App Store Stream/Pipe cutover

**Status: specified here, not yet implemented.** The first deliverable is replacement of only the selected ELF's binary download. Do not rewrite the UI or change public app ABI merely to move the payload. Implement the transfer in a reusable firmware-owned streaming/install service or bridge, with the existing app-host operation continuing to apply catalog and installer policy. The service must not be an application-private downloader, and a future package manager must be able to reuse it.

### Download-to-stage algorithm

1. Retain the existing saved-network readiness check and the selected release asset/sidecar validation. Reject incompatible versions, mismatched filenames, invalid manifest or unsafe paths **before** creating the output stream. Do not widen the legacy URL or TLS trust policy.
2. Preserve the current interrupted-install recovery of `.bak` files. Remove abandoned `*.part` files before opening the destination in `FILE_CREATE_NEW` mode; exclusive creation must never overwrite the live ELF. Preserve the current staging of the validated JSON as `sidecar.json.part`.
3. Acquire a readable HTTP body stream with `open_http(asset.url)` and a new writable stream for `/sd/Apps/<safe-name>.elf.part` with `open_file(FILE_CREATE_NEW)`. Create a `BLOCK_PRODUCER` pipe from HTTP to that file. Every acquired stream and pipe must have one cleanup path for partial acquisition, timeout, disconnect, cancellation, and ordinary success.
4. Drive or observe the shared scheduler through `pipe_info` from the owning invocation. `RUNNING`/`PAUSED` are not success; `FAILED` and `CANCELLED` terminate installation and preserve their reported failure. Wait cooperatively with bounded polling and watchdog servicing rather than busy-spinning or allocating the entire ELF. Use an explicit bounded overall or no-progress timeout; do not silently retry a failed transfer as a new install.
5. Require `T5_PIPE_DONE` with no pipe error. Capture `bytes_transferred`, close the pipe, call `finish(file_stream)`, and require its success. Confirm the staged file length matches the release asset length when declared and the transfer count. A source EOF without successful destination completion is insufficient. Check every read/open/finish result instead of assuming it worked.
6. Only after the transfer and staged artifact checks succeed, invoke the **existing** `.part`/`.bak` commit and rollback sequence for the ELF/JSON pair. Any failure before commit deletes staged files without touching an intact installed pair. Any failure during commit restores the previous pair through the established recovery path. Do not rename the staged ELF into its final location before successful `finish` and validation.
7. Close the HTTP stream, file stream, and pipe on all exits, without stopping an unrelated Wi-Fi connection or USB session. Account for the existing HTTP worker's bounded shutdown and single-request `BUSY` behavior; closing a handle alone is not evidence that the HTTP task has stopped. Never retain a pointer into the App Store ELF or a session stack frame in background work.

This cutover changes the binary **data plane**, not release discovery, install policy, transaction ownership, or UI. Follow-on migration must move release JSON, aggregate catalog, and fallback manifest HTTP bodies through RiscRTE streams into bounded parsers; keep the aggregate index fast path and the fallback behavior. Only mark the *entire App Store* migrated once both payload and metadata use the runtime stream contract. A future package manager may replace app-host-specific install orchestration, but the MVP must not depend on that larger refactor.

### Required host regressions before calling the binary cutover complete

Exercise a fake HTTP source and staged-file sink through the **production** stream registry/bridge or an integration test of the production transfer service, not merely a duplicate test implementation. Assert all of the following:

- A payload substantially larger than 4096 bytes transfers in order using bounded memory and `BLOCK_PRODUCER`; a temporarily full output causes backpressure, not loss.
- HTTP failure, disconnect, zero-progress timeout, partial SD write, SD removal, stream/pipe resource exhaustion, and cancellation all fail closed; no final or half-paired app becomes launchable.
- Truncated content, declared-length mismatch, and an HTTP body shorter than the reported transfer count are rejected; no install succeeds on EOF alone.
- The destination is finished successfully **after** pipe completion and lease release; injected `finish` failure prevents commit.
- Pre-existing installed ELF/JSON remain intact after pre-commit failures, and injected rename failures restore the old pair; abandoned `.part` and `.bak` states recover deterministically on retry.
- A malformed, incompatible, mismatched, or unsafe sidecar never starts an ELF download; the app's current version/install state, aggregate catalog path and per-manifest fallback are unchanged.
- Repeated launch/exit, cancellation, and immediate retry reclaim all handles and the worker's single HTTP request slot; stale generation handles cannot access a later invocation.

The firmware build for both supported boards, `bash test/run_stream_test.sh`, native-app host regressions, and `scripts/build_all_apps.py` must pass on the exact code revision. Host testing does not establish flash/SD/network hardware parity. Add explicit test coverage for the new transfer path rather than declaring existing stream-registry tests sufficient.

## Subsequent target architecture (not part of this MVP claim)

Typed record streams and stable schema identities; transforms; tee/merge; explicit loss-tolerant policies and dropped-data counters; zero-copy pools; range/replay views; discoverable logical streams; persistent topology; runtime registration of provider ELFs; manifest permissions; unified resource accounting; and full capability-based device/provider acquisition remain architecture work beyond this subset. New consumers should ask for semantic capabilities such as `serial.port`, not infer a concrete USB driver, and keep data/control planes separate. A recorded future requirement is not evidence that it exists in the current ABI.

## Validation and outstanding physical acceptance

`bash test/run_stream_test.sh` exercises the portable registry, C ABI and provider bridge; native-app tests and CI exercise loader integration and builds. The semantic serial and ESP ROM reference work added provider and fault-injection coverage, but the USB reference specification's physical acceptance is still outstanding. Do not label a board behavior validated solely because CI or an emulator passes.

On hardware, test USB host/VBUS startup, supported USB-UART adapters, boot-only serial auto-detection, serial full duplex at multiple configurations, rapid unplug/replug and stale-lease revocation, flashing with source/target MD5 and reset, and repeat cleanup without power/resource leaks. For streams and App Store specifically, test removable SD failure during transfer/finish/rename, HTTPS redirects/TLS errors, HTTP content-length and chunked responses larger than the FIFO, slow sink backpressure, connection interruption and retry, and cold-start Wi-Fi. Capture transfer counts, state transitions and final on-disk ELF/sidecar pairs. These items are acceptance requirements, **not** claims that real-device validation has run.

This document records implementation and next-cutover requirements. Where it conflicts with the master specification, roadmap, or full Stream/Pipe architecture, those target documents govern new changes; retain compatibility details here only to explain the presently deployed ABI and migration constraints.
