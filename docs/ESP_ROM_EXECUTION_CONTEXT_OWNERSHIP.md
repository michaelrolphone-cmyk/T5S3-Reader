# ESP ROM programmer execution-context ownership

Authority: [USB Capability/Stream Reference Implementation](USB_CAPABILITY_STREAM_REFERENCE_IMPLEMENTATION.md), [Application Execution Context Architecture](APPLICATION_EXECUTION_CONTEXT_ARCHITECTURE.md), and [ESP ROM Programmer Provider](ESP_ROM_PROGRAMMER_PROVIDER.md).

## Implemented lifecycle (PR #62)

The firmware maintains one active `RuntimeResources::ExecutionContext` per native ELF invocation. It is not an ELF API or a second process. Invocation IDs are monotonic across context objects until exhaustion; the runtime refuses nested contexts and refuses acquisitions once the invocation is Stopping. All public API pointers are firmware-owned; obtaining a pointer does not authorize calls after the context exits.

The programmer remains a **single synchronous bounded operation**, not an asynchronous worker or a persistent job handle. Before invoking any ELF progress callback or acquiring `serial.port`, it registers a `Programmer` resource against the running context, retaining only a firmware-allocated operation pointer and the invocation ID. An attempt without an active context returns `DENIED`; reentrant/concurrent programming returns `BUSY` without acquiring serial.

Normal success or failure:

1. Finish the protocol or report the semantic failure and copy the final status to the caller.
2. Destroy the firmware operation, releasing its exclusive serial lease and its RX/TX streams. The app retains ownership of the borrowed firmware input stream.
3. Unregister `Programmer` using its original invocation ID. A stale ID cannot unregister a newer job.
4. Release the global programmer busy guard. The caller may close its source stream; normal app exit closes any remaining handles.

Stop or exit requested **inside a progress callback**:

1. `ExecutionContext::requestStop()` marks the context Stopping and notifies the programmer once. The notification sets a cancellation flag only; no operation buffer, ELF callback or dependent stream is destroyed from the notification.
2. `end()` sees the active programmer registration and defers all teardown; it cannot join the currently executing synchronous stack frame. Public context acquisitions are denied during Stopping.
3. On return from the in-flight callback, the provider detects the stop before any additional callback or transport operation. It also checks cancellation at every firmware stream chunk and during serial wait/read/write loops.
4. The provider reports `CANCELLED`, releases the lease, unregisters its job and completes deferred context teardown (remaining serial/stream/pipe/file cleanup) before returning. No app callback is made after stop.

A stop notification is single-shot; `end()` is reentrant/idempotent. Resources and callback memory must not be reclaimed before the operation has quiesced. This is a cooperative owner-task guarantee, **not recovery from an arbitrary ESP32-S3 hard fault or forced task deletion**. A faulty ELF running untrusted machine instructions is not a recoverable isolated OS process. Do not claim crash-safe preemption or asynchronous join for this synchronous v1 API.

## Automated verification

`test/resources/execution_context_test.cpp` verifies exclusive active context, globally monotonic IDs, nested-launch rejection, single-shot stop, deferred end with a registered programmer, LIFO dependency cleanup, stale-ID rejection, reentrant/duplicate teardown and partial startup. `test/programmer/esp_rom_provider_test.cpp` retains production-provider host protocol regressions under a real execution context. `test/programmer/esp_rom_context_test.cpp` injects stop/exit during hashing, erase and writing and asserts semantic cancellation, no post-stop callbacks, exactly-once lease release, no stale authorization and clean retry in a fresh context. The tests use a stubbed MD5 and mocked serial/stream APIs; they do not verify real cryptographic hardware behavior.

## Remaining work before spec completion

- Complete minimum `serial.port` provider registration/selection independent of USB implementation, preserving the resident drivers as fallback.
- Validate actual on-device app Back/Home while flashing, host power/enumeration, streaming traffic, MD5, target reset, unplug/replug, retries, and resource reclamation on both board variants; record original timings and supported adapters.
- Make runtime diagnostics distinguish USB device enumeration, driver binding, semantic capability acquisition, stream loss and ESP ROM command failures; collect logs without relying exclusively on a USB debug console that shares the active OTG PHY.
- Add an asynchronous generation-safe programmer job **only if** clients need to cancel from a different task; such a design must explicitly join the worker before stream release and ELF unload. The current narrow reference spec permits an equivalent synchronous owned operation.

Do not remove legacy transport fallbacks or describe physical USB/flashing parity as verified on the basis of host CI alone.
