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
quarantine, and reload. Existing packaged serial providers have not yet opted
into the descriptor or advertised these endpoints through their capability API;
the serial shuttle remains until that conversion. Protected streams and consent
propagation remain governed by their existing fail-closed policy; this public
queue table does not grant protected-source publication or delegation.
