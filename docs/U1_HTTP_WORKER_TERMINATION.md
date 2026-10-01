# U1 HTTP worker termination

## Confirmed source boundary

`NativeStreamBridge.p1.inc::httpRequest` invokes the existing
`HttpDownloader::fetchUrl` on its single worker. The selected PlatformIO6.13.0
framework is Arduino2.0.17 (`framework-arduinoespressif32 ~3.20017.0`). In that
framework, `HTTPClient::writeToStreamDataBlock` loops while connected even when
no body bytes are available; its empty-progress branch only delays. The
application stream deadline cannot invoke HttpSink while that loop receives no
bytes. Therefore caller cancellation alone does not guarantee worker retirement.

Source: [PlatformIO manifest](https://github.com/platformio/platform-espressif32/blob/v6.13.0/platform.json)
and [Arduino HTTPClient.cpp](https://github.com/espressif/arduino-esp32/blob/2.0.17/libraries/HTTPClient/src/HTTPClient.cpp),
Git blob b1328c07316631573c21a2d17fb4b9c322671a7a. This was established from
source and the dependency-backed host fixture, not a reported physical hang.

## Connected correction

The existing fetch path now constructs a private budgeted subclass of its
normal plain/TLS client. Virtual connected/available/read/write dispatch checks
a30-second no-I/O budget and a5-minute total budget. Only that worker stops its
own client. Redirects preserve the total deadline. Existing 5-second connect
and socket-read limits are explicit; TLS handshake time is set to15seconds.
Denied calls explicitly yield so Arduino Stream timed-read retries cannot
turn the cleanup tail into a busy spin. No other task closes a socket, deletes
a worker, discards a live lease, or
substitutes a new HTTP parser/registry.

Header and chunk-size lines read by the existing parser have an8KiB line bound;
binary body blocks use the existing readBytes path and do not acquire a newline
requirement. The two paths are distinguished by the actual Arduino Stream API.
The caller checks budget failure after transfer/close: a nonnegative partial
EOF caused by timeout is still failure. Diagnostics identify the budget reason.
The idle budget matches the existing stream sink's30-second backpressure bound.

The existing redirect and transport certificate policy is unchanged. A request
deadline is not certificate validation. Legacy file-download helpers outside
this shared fetch/HTTP-worker path are not claimed changed by this slice.

## Executed evidence

- The normal runtime-network aggregate includes real adapter tests for virtual
  dispatch, idle and total limits, monotonic wrap, progress, redirect persistence,
  refusal after expiry and same-worker stop
- Header overflow is refused; long binary blocks without newlines remain valid
- `http_client_body_budget_test.py` compiles the unchanged body-loop function
  from the actual Arduino dependency with deterministic client/time adapters.
  The baseline remains active beyond the request budget until the simulated peer
  disconnects; the production adapter terminates the idle case at30seconds and
  ongoing trickle at5minutes. Complete bodies pass. These are simulated host
  times, not network or device benchmarks
- The actual fetch-path catalog fixture passes with the production budget and
  now checks late partial output returns failure. Its platform mocks were
  updated for the client API, preserving all existing freshness assertions. The
  full native-app aggregate passed after that fixture update
- Both normal board jobs run that dependency-backed fixture against the
  dependency already used by their source build. Both workflows36851475126 /
  36851475033 passed exact64d19d64, tree59044fc1 matching tested localf4ef3a0b

## Limits retained

This closes the demonstrated outer worker-loop failure. It does not forcibly
interrupt an arbitrary synchronous SDK/DNS/TLS call already in progress; those
calls retain their own driver-level behavior. Stream deadline/cancellation and
storage uncertainty remain independent. HalStorage mutex/destructor ownership
and synchronous media termination remain open, as does full target/PHY evidence.
The blocked independent receipt review remains incomplete and was not retried.

### Pinned lower-call evidence, October 1 integration

A targeted read of the same Arduino2.0.17 dependency distinguishes configured
bounds from arbitrary interruption. WiFiGeneric.cpp blob
`a6bc47e80daf247dc698759c0a7f1d5ab3c98d2f` uses a16-second DNS-idle wait and
15-second result wait in hostByName. ssl_client.cpp blob
`a8b570a88514f5784a6213f78df225e597106822` uses finite socket select/receive/send
limits and checks handshake/write elapsed time with vTaskDelay(2). The caller's
5-second socket/connect and15-second handshake settings therefore reach actual
lower mechanisms; DNS has its own longer bound rather than inheriting5seconds.

This is source evidence, not a deterministic maximum for arbitrary lwIP/SDK
internals or a callback-lifetime proof. The same worker keeps its buffers/client
until those calls return, then rejects an expired operation. No timeout-driven
cross-task destruction or force-reset was added. The confirmed unresolved
unbounded SPI/media path has a separate owner-policy decision; these finite
network mechanisms should not be described as a proven indefinite DNS loop.

**Implementation In Progress**
