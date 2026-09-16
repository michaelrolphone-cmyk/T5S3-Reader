# Stream/Pipe byte-stream MVP

This implements the byte-stream foundation of `STREAM_PIPE_ARCHITECTURE.md` for
future App Store and serial-monitor migration. Neither app source, manifest nor
existing service table changes. The new `t5_stream_get_api(1)` export is additive.
Apps using it must ship only with firmware containing this export and set their
manifest minimum firmware version accordingly at migration/release time.

## Implemented contract

`lib/NativeApps/include/T5StreamApi.h` defines a versioned C table with
`api_version` and `struct_size`. Consumers check both before using required fields.
Handles contain a slot and 24-bit generation. Slots retire on generation exhaustion;
owner identities also fail closed on exhaustion. Handles are never durable IDs.

The firmware owns at most 12 streams and 4 pipes. Each memory stream has a requested
capacity of 1–4096 bytes. Each pipe retains at most 512 bytes, including partially
written data. There is no payload-sized file/HTTP allocation. Reads/writes accept
binary buffers and move at most 512 bytes per call; always inspect the returned
count. Zero-length operations succeed. `AGAIN`, `EOF`, disconnect, timeout,
permission/direction failure, resource exhaustion and I/O failure are distinct.

Only byte streams and `BLOCK_PRODUCER` pipes are supported. Other kinds/policies
are rejected. A full memory stream returns `AGAIN` without consuming input. A pipe
holds an unwritten suffix until the destination accepts it. It never silently
drops bytes. Source and destination must belong to the active native session.
One pipe exclusively leases a source's read side and a destination's write side;
competing direct operations return `BUSY`. Self-pipes and cycles are rejected.

A single lazy-created firmware scheduler moves one bounded read/write per active
pipe per turn, rotating the first pipe for fairness. Data/API changes notify it;
while live pipes exist it also checks legacy providers every 10 ms because those
providers do not yet offer readiness notifications. It sleeps indefinitely when
no pipes are runnable. Transport I/O retains its bounded provider latency (for
example the existing USB TX lock can wait up to 100 ms); this is not a hard
real-time scheduler. There is no task per pipe.

`pipe_pause`, `pipe_cancel`, and `pipe_close` affect the connection, not endpoint
ownership. Cancellation discards the pipe's pending bytes explicitly. Terminal
pipes retain counters until `pipe_close` or session cleanup. EOF completes a pipe
only after its pending bytes drain. Destination EOF/close is **not automatic**:
the owner calls `finish(destination)` after successful pipe completion, checks its
result, then closes handles. This permits borrowed endpoints and explicit file
commit decisions. Chained memory pipes likewise need explicit EOF at each sink.
Errors fail the connection and remain visible through `pipe_info`; a failed file
must not be installed just because some bytes reached disk.

`info` reports owner, kind, direction, buffer usage/high-water mark, terminal result
and byte counts. `pipe_info` reports endpoints, owner, state, pending bytes,
transferred bytes, last error and number of scheduler turns without progress.
Initialize output structures' `struct_size` to `sizeof(structure)`.

## Providers

| Open operation | Behavior |
| --- | --- |
| `open_buffer` | Bounded read/write FIFO; `finish` closes input and readers drain to EOF. |
| `open_file(..., FILE_READ)` | `/sd/` file, sequential binary reads and absolute seek within its initial length. EOF is distinct from a failed/short read. |
| `open_file(..., FILE_CREATE_NEW)` | Exclusively creates a new output file; never truncates an existing file. Parent directory must already exist. `finish` closes the file and checks the close result. |
| `open_usb` | Borrows the existing USB service as a duplex byte endpoint. Start/configure it with `T5UsbApi`; DTR/RTS remain control-plane operations. Only one stream borrow is allowed. |
| `open_http` | One asynchronous HTTP response body at a time, with a 4096-byte read-only FIFO. HTTPClient handles redirects and chunked decoding through existing `HttpDownloader`. |

File paths reject traversal, backslashes, empty path segments, trailing slashes,
and paths outside `/sd/`. File streams do not implement atomic installation:
download to a new staging path, require successful pipe completion and file
`finish`, validate the payload, then let the existing installer commit it. Failed
or cancelled output files remain for the caller to remove/recover.

USB stream close does not stop the host or VBUS. Reconfiguration temporarily
returns `AGAIN`; device loss after connection is terminal. Seek is unsupported.
RX/TX progress is independent. The existing USB RX ring can overflow if the
physical peer cannot be throttled; `T5UsbApi.serial_read_state` remains the source
of transport drop/error counters. Lossless *pipe* buffering cannot undo losses
upstream in that ring. Do not mix legacy `serial_read/write` calls with the stream
borrow. Application conversion will choose one data path. Device rediscovery and
reopening belong to the caller; there is no silent automatic reconnect.

HTTP uses one transient firmware network task because the existing HTTP client
performs blocking protocol I/O. This keeps TLS/network waits out of the pipe
scheduler. It copies the URL (maximum 1023 bytes), never stores an ELF pointer,
and rejects a second request with `BUSY`. Network readiness, redirects and TLS
policy are inherited from `HttpDownloader` (including its existing `setInsecure`
HTTPS policy); this MVP does not introduce certificate verification or automatic
Wi-Fi connection. A consumer stalled for 30 seconds terminates the producer with
`TIMEOUT`. Buffered data drains before the terminal result appears. Closing the
HTTP stream immediately reclaims its FIFO; the firmware network task unwinds on
its next body write or existing network timeout. Until it unwinds, another HTTP
request returns `BUSY`. No forced task deletion or cross-thread client destruction
is used. A closed/reused handle rejects late writes by generation and owner.

## Ownership and unload

`NativeAppHost` starts a fresh stream owner for every ELF invocation and releases
all pipes/streams after `launch_elf_app` returns, including loader failure. The
public table is available only on the active app task and every entry rechecks
that session. Worker access is serialized by a firmware mutex. Provider vtables
are firmware-only and are not exported for ELF registration. The HTTP job owns
only copied firmware data. There are no app callbacks, pointers or executable
references retained after unload.

The existing keyboard handoff unloads/relaunches apps. A future serial-monitor
conversion must reopen its borrowed USB stream after handoff; the USB service
continues running under its current lifecycle. Stream handles must not be saved
in resume state. This MVP uses the platform's current trusted-native-session
access boundary; granular manifest capability authorization is deferred.

## Future migration paths

App Store can open an HTTP source, a new staged file destination and a pipe,
continue servicing UI input while inspecting pipe status, and cancel/close on
Back. Catalog parsers can read response bodies in bounded chunks. Existing
catalog/install entry points continue their current implementation in this MVP.

Serial monitor can retain the current USB start/configuration/control API and
replace data reads/writes with the borrowed stream. A bounded memory sink can be
attached through a pipe for display consumption. Explicit overflow/drop policies
and tee-to-logger support will be added before enabling multiple consumers.

## Deferred architecture

Typed records/schema identity, transforms, tee/merge, dropping policies,
zero-copy pools, range/substream views, replay, stream enumeration UI, persistent
logical topology, provider ELF registration, revocation and manifest capability
permissions remain future work. The full architecture's hardware acceptance
matrix is not claimed by this byte-stream MVP.

## Validation

Run `bash test/run_stream_test.sh` for the actual portable registry under ASan and
UBSan plus C-header compatibility. It covers ordering, short writes, bounded
backpressure, partial-buffer drain, EOF/error distinction, source size independence,
seek limits, ownership rejection, stale handles, pause/cancel, provider failure,
cleanup, slot exhaustion and two-pipe fairness. The actual firmware bridge is also
compiled against host doubles to test path rejection, exclusive file creation,
SD failure, file close failure, USB disconnect/reconfiguration, HTTP backpressure
timeout, and late HTTP writes across session reuse. Restricted containers may need
`ASAN_OPTIONS=detect_leaks=0` because LeakSanitizer cannot inspect `/proc` there;
CI runs the default sanitizer configuration.

`test/run_native_app_test.sh` checks loader integration; the existing app tests
cover unchanged App Store and serial-monitor behavior. CI builds both supported
boards and all released native apps. Physical USB disconnect, SD removal and
large/chunked HTTP transfers still need on-device validation before app migration.
