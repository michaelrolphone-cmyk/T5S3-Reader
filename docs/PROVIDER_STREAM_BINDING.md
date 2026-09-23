# Installed-provider stream binding

An ABI-v2 provider can opt into copied streams by returning the `driver` prefix
of `risc_driver_streams_v2` from `t5_driver_get`. Set `driver.struct_size` to the
size of the extended descriptor and implement `bind_streams` and `quiesce`.
Existing `risc_driver_v2` descriptors and app byte-v1/record-v2 layouts are
unchanged. See `sdk/driver/RiscStreamProviderV1.h` for the C host-table contract.

The loader creates a distinct context and calls `bind_streams` before `start`.
The provider may retain the table until `stop` returns. It publishes byte or
schema-checked record queues, then copies bytes/records into readable endpoints
or out of writable endpoints. Rights describe the consumer side. A dual-right
endpoint supports both directions, subject to pipe exclusivity. Queue operations
retain neither ELF callbacks nor caller buffers. Providers schedule their own
bounded hardware work and obey AGAIN backpressure; the stream task moves queued
data through the existing pipes. The table grants no hardware access.

The runtime bounds this path to 16 provider contexts, four endpoints per context,
the shared registry's 12 stream slots, 32 lease-to-consumer bindings, existing
queue capacities, and 512-byte transfers. Publication copies schema metadata.
The loader creates and closes contexts on the serialized owner task; provider
workers may call copied queue operations under the existing registry mutex.
These are cooperative in-process capabilities, not a memory-isolation sandbox.

Only the trusted capability broker calls
`RuntimeInstalledProviders::attachStream(lease, endpoint, rights)`. It validates
the exact live graph lease and authenticates the foreground app before granting
access. No provider API accepts a caller-selected consumer ID or grants itself
app access. Handles alone confer no consumer rights. Applications then use their
unchanged byte/record and pipe APIs. Other runtime brokers must authenticate
their consumers before using the lower-level `GraphV2::grantStream` primitive.

Each attachment is bound to the exact capability lease. Releasing one lease
recomputes remaining rights for that consumer; another lease may preserve them.
An unsuccessful rights downgrade fails closed by revoking access. App teardown
removes its bindings. Provider teardown revokes all endpoints before quiescence;
failed quiescence retains mapped code, dependencies, and the revoked host table
for retry. `stop` must tolerate revoked stream operations. Reload uses new
context and endpoint generations. No registry operation can call an unloaded ELF.

The production installed graph supplies this host factory. The dynamic host
fixture demonstrates byte and record pipes, exact-lease rights, app isolation,
quarantine, and reload. An optional `risc_driver_poll_v2` suffix supplies a
cooperative owner-task work callback. The installed graph dispatches at most
four callbacks per turn with a two-millisecond budget per callback and a
10-millisecond elapsed checkpoint, rotates the first module, and yields after
work. It does not scan storage or activate providers. Inactive, unpinned and
quarantined modules receive no callback. Providers must bound their underlying
I/O; the dispatcher cannot preempt a callback that violates its budget.

The CDC, CP210x, CH34x and simulated serial test packages use this dispatcher
and advertise
`risc_serial_port_streams_v1::endpoints`. Its source and sink are attached through
the exact inventory capability lease after semantic session acquisition. The
serial bridge does not create or pump a resident pair for this extension and
never falls back when advertised endpoint acquisition fails. The shared ELF-side pump in `Drivers/common/SerialStreamPump.inc` stages
at most 512 RX and 512 TX bytes per session, retains partial/zero writes, and
performs at most one read and one write (one-millisecond timeouts) per turn.
Successful detach inventory closes its endpoints and revokes consumer grants.
Checked physical close failure keeps the original claim for retry while data
work remains stopped. Queue finish denotes queue EOF, not electrical wire drain.

The owner-loop discovery hook currently invokes the generic dispatcher after
inventory updates. This requires owner-loop progress, but no app RX/TX polling.
The witness is a simulated-class package, not supported commercial hardware.
CDC, CP210x and CH34x now publish endpoints directly. The resident shuttle
remains reachable for older installed capability prefixes and still needs retirement. Protected streams and consent
propagation remain governed by their existing fail-closed policy; this public
queue table does not grant protected-source publication or delegation.
