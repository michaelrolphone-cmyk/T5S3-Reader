# Optional CDC stream-session adapter

`usb-cdc-acm` **0.1.9** adds a CDC-only adapter to Runtime's tagged stream-session
contract. The existing Reader serial capability and full diagnostic/stream/poll
descriptor prefixes keep their exact layouts. Reader's legacy open, configuration,
endpoint and raw-I/O behavior remains available for legacy sessions. Tagged
sessions cannot be configured, closed or adopted through that raw prefix.

## Source and version custody

- Isolated implementation base: Reader `aac8c06d3221139084acd0cfc64f7b0ba194a97a`.
- Runtime contract and production queue test source:
  `b587df55298e0bb8e676b3d59ca13679c0267bf7` (0.1.73).
- The three new generic/session headers are copied byte-for-byte from that
  Runtime; its existing `RiscProviderV2.h` and `RiscStreamProviderV1.h` match this
  Reader base. The Runtime queue test checks all five files before compiling.
- On 2026-10-08, live Reader master
  `b5a9887e16ce04baecdfa8366ff7d67a2a22c3cc` and release index
  `19bb3dcb5cb763c4eacd884ad5fc0bcab57ff2ed` both identified canonical
  `usb-cdc-acm` 0.1.8. This change advances that same identity to 0.1.9.

The source stays in the existing shared Reader driver location. This change
makes no X4 product, paused PR350, hardware, publication or release-index edit.

## Admission and deadline contract

The driver appends `risc_driver_stream_sessions_v2` after its COMPLETE existing
poll descriptor. Its open callback requires a bound queue host and the optional,
size/tag/version-checked `risc_usb_host_deadlines_v1` suffix. A legacy USB host
can still start the class for Reader, but tagged open returns UNSUPPORTED before
any host or queue operation. An oversized host table alone grants no new API.

The USB suffix follows the COMPLETE existing host snapshot prefix. It provides
monotonic milliseconds plus deadline-aware complete inventory, configuration,
claim and checked release callbacks. An advertising host also promises total
budgets for its existing claim-scoped control and bulk calls. No firmware import,
new hardware grant, implicit controller/PHY access or power selection is added.

Each tagged open/call/close uses one caller budget, between 1 and 1000 ms. Every
lower operation receives only the remainder. Both work and elapsed time are
bounded: inventory has at most eight unique nonzero generation tokens, descriptor
parsing is bounded to 4096 bytes, at most two interfaces are claimed/released,
and there are no loops of retries. A callback that overruns causes sticky
retention. The adapter cannot make an unbounded legacy host callback safe by
measuring it afterward; the new host contract must actually be implemented.

Open validates the copied request and exact device/generation pair against a
complete current host inventory. It parses the real CDC configuration, acquires
new control/data claims, sets baud/framing, and only then publishes distinct
READ-only RX and WRITE-only TX queues. It never adopts an existing raw token.
Failed inventory pauses work; valid absence/change disconnects the session.
Invalid or partial inventory cannot authorize a claim or physical transfer.

The owner-task pump services one session per turn, at most one 256-byte bulk
read and write (each at most 1 ms), while checking the total polling budget.
Short transfers keep pending offsets and byte order. It checks inventory before
moving either already-buffered or physical bytes. Runtime's existing dispatcher
owns scheduler cooperation between bounded turns; the adapter creates no task.

## Cleanup and ownership

Close stops pumping, releases the data and control claims with the remaining
budget, and only then closes queues. This ordering also applies to a failed
open after just one queue was published. Only confirmed successful queue closes
clear endpoint IDs. A failed physical release, queue contention/refusal, malformed
claim result, or exhausted cleanup budget preserves exact surviving custody.

RETAINED is sticky: quiescence, direct stop and later tagged calls cannot discard
resources or retry uncertain cleanup. Runtime retains the invocation/provider
and lower dependencies. Successful provider close only marks reserved queues
closed; Runtime's matching owner/session reservation retires them afterward.
The provider authenticates its own fresh tokens; app invocation/grant ownership
remains Runtime's responsibility, rather than an app-supplied owner number.

Legacy Reader cleanup behavior is intentionally unchanged. The shared pump has
CDC-only conditional hooks; preprocessing with those hooks absent is identical
to the frozen source, so other serial packages do not change behavior or version.

## Verification

Run from this checkout, using a clean canonical Runtime source directory:

```sh
bash test/run_usb_cdc_tagged_sessions_test.sh
SANITIZE=1 ASAN_OPTIONS=detect_leaks=0 bash test/run_usb_cdc_tagged_sessions_test.sh
bash test/run_usb_cdc_runtime_queues_test.sh "$RUNTIME"
SANITIZE=1 ASAN_OPTIONS=detect_leaks=0 bash test/run_usb_cdc_runtime_queues_test.sh "$RUNTIME"
PLATFORMIO_SETTING_ENABLE_TELEMETRY=No NATIVE_DRIVER_CC="$CC_XTENSA" \
  python3 scripts/build_usb_cdc_v2.py
```

Passed on 2026-10-08:

- Twenty-one deterministic actual-CDC ELF scenarios, normal and ASan/UBSan: valid
  open/configuration, missing/truncated/wrong host extension, malformed requests,
  absent/unknown/duplicate/zero/oversized inventory, short configuration, claim
  failures with clean/retained/malformed custody, partial publication and rollback,
  total open/call/close deadline exhaustion and overrun, physical close refusal,
  first/second queue close refusal, raw/tagged and stale-token isolation, partial
  RX/TX ordering, inventory pause, I/O error without false absence, and
  changed-generation disconnect.
- Three cases with Runtime 0.1.73's production queue host, normal and ASan/UBSan:
  copied-byte delivery and wrong-owner rejection; second publication failing at
  real queue capacity without deleting existing endpoints; actual registry-lock
  contention during close preserving both queue allocations after physical close.
- Existing legacy CDC I/O/lifecycle, composite/union/IAD/alternate descriptor and
  ST-LINK VCP fixtures, normal and ASan/UBSan.
- Existing CH34x, CP210x and FTDI class host fixtures, normal.
- Existing Reader ABI-v2 module admission, dependency, pin and unload fixture.
- Shared-pump preprocessing unchanged without the CDC hooks.
- Xtensa ET_DYN build and Runtime's real ELF structural rejection/validation
  fixture. The target imports only `memcpy` and `memset`; its only function export
  is `t5_driver_get`.

Address/undefined sanitizers are enabled; leak detection is disabled because
retained fixtures deliberately exit with mapped code and custody intact.
No full firmware rebuild or physical test was performed.

## Remaining integration

The real `usb.host` and physical controller do not yet implement the new deadline
suffix. The adapter therefore fails closed on the existing shipping host. A real
implementation must propagate the single remaining deadline through event polling,
configuration/claim/controller operations and checked release; preserve uncertain
DMA/callback/claim custody; and supply a stable monotonic clock. Wrapping fixed
legacy timeouts does not meet this contract.

CH34x, CP210x and FTDI have no tagged adapter in this slice. End-to-end loading of
the entire paper UI/client, broker, migrated class and a real deadline-capable
host/controller is still open. The deterministic tests exercise the real CDC
class and real Runtime queues but simulate USB callbacks. No native PHY lease,
X4 VBUS route, connector/pin assignment, electrical suitability, physical attach,
unplug/reconnect, power-loss recovery or diagnostic restoration is qualified.
