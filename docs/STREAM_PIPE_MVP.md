# RiscRTE Stream/Pipe MVP

## Target platform contract

This document records the first implemented subset of the RiscRTE Stream/Pipe architecture. The normative target is `STREAM_PIPE_ARCHITECTURE.md`, under `RISCRTE_PLATFORM_SPEC.md` and Part III of `PLATFORM_CAPABILITY_ROADMAP.md`.

The target is a generic RiscRTE data-movement primitive connecting applications, services, devices, providers and storage without either endpoint depending on the other's concrete implementation. Streams participate in unified resource ownership, use bounded buffering/backpressure, expose explicit EOF/disconnect/error semantics, and eventually support byte and typed-record flows, transforms and structured IPC integration.

The implementation below is deliberately narrower. New work should extend it toward the architecture rather than treating MVP omissions as permanent design constraints.

## Current compatibility implementation

The byte-stream MVP exists primarily to enable App Store and serial-monitor migration. Neither application was converted by the MVP itself. The deployed additive ABI is `t5_stream_get_api(1)` in `T5StreamApi.h`; the `T5*` name is a compatibility identifier, not the platform name. Applications using it must set an appropriate firmware floor.

The API uses a versioned C table with `api_version` and `struct_size`. Handles contain a slot and generation and are never durable IDs. Firmware currently owns at most 12 streams and 4 pipes. Memory streams are bounded to 1–4096 bytes; a pipe retains at most 512 pending bytes and transfers at most 512 bytes per operation. `AGAIN`, `EOF`, disconnect, timeout, permission/direction failure, resource exhaustion and I/O failure are distinct results.

Only byte streams and `BLOCK_PRODUCER` pipes are implemented. A full source/sink returns backpressure rather than silently dropping pipe bytes. One pipe exclusively leases the source read side and destination write side. Self-pipes and cycles are rejected.

A single firmware scheduler advances bounded work across active pipes with rotating fairness. There is no task per pipe. `pipe_pause`, `pipe_cancel` and `pipe_close` affect the connection rather than endpoint ownership. Cancellation explicitly discards pending pipe data. Destination completion remains explicit through `finish(destination)` so callers can control borrowed endpoints and file commit decisions.

### Implemented providers

`open_buffer` provides a bounded FIFO. `open_file(FILE_READ)` provides sequential SD reads and bounded seek within initial length. `open_file(FILE_CREATE_NEW)` exclusively creates a new output and never truncates an existing file. `open_usb` borrows the existing USB service as a duplex endpoint while configuration/control remains in the compatibility USB API. `open_http` provides one asynchronous HTTP response stream with a bounded 4096-byte FIFO.

File streams do not implement atomic installation: callers stage, require successful pipe completion and `finish`, validate, then use the installer to commit. USB stream close does not stop host/VBUS. HTTP currently uses the existing blocking client on one transient firmware network task and inherits existing network/TLS policy.

### Ownership and unload

`NativeAppHost` creates a fresh stream owner for each ELF invocation and releases all streams/pipes after the ELF returns or loader failure occurs. Public API access is restricted to the active application session. Worker access is firmware-serialized; provider vtables are firmware-only. No ELF callback or executable pointer is retained after unload. Handles must not be persisted across keyboard/system-UI unload-and-resume handoffs.

This is the current implementation of the broader RiscRTE resource-ownership invariant. Granular manifest capability authorization remains future work.

## Roadmap migration

The App Store should move HTTP/download data through streams into staged storage while keeping catalog/install policy above the stream layer. The serial monitor should retain device/control-plane configuration while moving serial payload data through streams. Neither application should own a private buffering/transport subsystem once the common stream capability satisfies its requirements.

Subsequent architecture work adds typed records/schema identity, transforms, tee/merge, explicit dropping policies, zero-copy pools, range views, replay, stream discovery, persistent logical topology, provider-ELF registration, revocation and manifest capability permissions. Device registry and capability resolver integration should allow consumers to request semantic endpoints rather than concrete transport implementations.

## Validation/current limits

`bash test/run_stream_test.sh` validates the portable registry and bridge behavior; native-app tests validate loader integration. CI builds both supported boards and released applications. Physical USB disconnect, SD removal and large/chunked HTTP transfers still require on-device acceptance before application migration.

This document is implementation-state documentation. Where it conflicts with the full Stream/Pipe architecture or platform roadmap, the target architecture governs new work and this document describes only the compatibility state that must be migrated.