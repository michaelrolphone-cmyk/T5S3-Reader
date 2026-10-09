# USB HID mouse semantic provider

`usb-hid-mouse@0.1.0` supplies `usb.hid.mouse@1` through the additive
[`RiscUsbMouseV1.h`](../sdk/driver/RiscUsbMouseV1.h). It depends on `usb.hid@1`
and `platform.clock@1`, uses only copied reports/events, and owns no USB
controller, native firmware bridge, application callback or cursor. Existing
HID, keyboard, gamepad and navigation ABI/source bytes are unchanged.

This is a distinct semantic product alongside the keyboard/gamepad providers,
not a renamed update of either. On 2026-10-09 the live default branch had no
mouse manifest, an exact-ID all-PR search had no result, and the live
`release-index` branch contained no `usb-hid-mouse` entry. Its initial version
is therefore `0.1.0`; its status remains `experimental-unpublished`.

## Consumer contract

1. Resolve the capability through an ordinary context-owned provider lease;
   validate API version, structure size and callbacks. Keep the dependency
   graph pinned while any pointer into this provider is usable.
2. Subscribe with a generation-qualified host device filter or zero for all
   mice. A new subscription begins in GAP. Call its `snapshot` with capacity
   for four states before interpreting events.
3. From the serialized owner, call `poll(1..16)` at the chosen input cadence.
   Read copied events with `next`; no callbacks run in the consumer ELF.
4. On REPORT, apply relative `x/y/wheel/pan` counts exactly once and replace
   the held-button mask with `state.buttons`. `pressed` and `released` are
   transitions for that report. Positive/negative directions and units are
   HID report counts; acceleration, scaling, cursor position and scroll policy
   belong to the consumer. No movement is normalized or accumulated here.
5. Identity is `(state.device, state.session)` with interface/alternate copied
   for diagnostics. Session is a never-reused semantic generation while this
   loaded ELF exists, not the raw HID handle. Separate mouse interfaces of one
   composite device remain separate. Event sequence is global to this loaded
   provider, so filtered subscriptions can legitimately skip sequence values.
6. `next == -2` is sticky GAP; it fills an initialized GAP event. Stop using
   assumed button history and replace it from subscription-scoped `snapshot`.
   A successful snapshot atomically discards that subscriber's older queued
   history and acknowledges GAP. Too-small capacity leaves GAP and history
   unchanged. Missing sessions release all previously held buttons. Lost
   relative movement/scroll/click history is unrecoverable. `next == -1` is an
   invalid handle/output; `0` is empty; `1` is one copied event.
7. DISCONNECTED carries zero current buttons and a `released` mask for all
   prior held buttons, exactly once. Do not retain input after disconnect.
   Poll failure also requires checking the local snapshot/status; a cached
   lower HID inventory is not evidence that held input is still valid.
8. Unsubscribe before releasing the provider lease. Failed quiesce retains
   the graph. In RETAINED, `next`, `snapshot`, `unsubscribe` and `status` remain
   local; no further dependency calls, restart or unload is permitted. Do not
   retry by freeing module memory or bypassing the lower provider.

No consumer integration, Runtime input-core change, UI cursor, package
installation or hardware activation is included in this unit. Product
integration must bind/poll this capability, implement the GAP/disconnect
contract, and preserve the existing keyboard/game-controller consumers.

## Supported reports and explicit limits

- One top-level Generic Desktop Mouse Application collection, optionally with
  bounded physical/logical subcollections. X and Y are required, distinct,
  relative signed fields of 2–16 bits. Wheel (Generic Desktop 0x38) and
  horizontal pan (Consumer AC Pan 0x0238) use the same relative signed range.
- Up to 32 distinct one-bit absolute buttons, usages Button 1–32 and logical
  range 0–1. All mouse data uses one report ID, or the unnumbered input report.
  Input offsets include constant and nonmouse fields before/after the mouse
  collection. At most eight input report IDs are described. Other declared
  input IDs are length-checked and ignored; unknown IDs are rejected.
- Descriptor length ≤512 bytes; report length ≤64 bytes and exactly the
  descriptor's rounded-up input bit length including a report ID when present.
  Values outside their declared logical range are malformed. No trailing
  data or report IDs are guessed. Signed bit extraction supports byte-crossing
  fields and sign extension without unaligned accesses.
- Up to 36 fields, local usage list/range ≤32 entries, global push stack depth4,
  collection depth8. Explicit 32-bit page-qualified usages are supported.
  Physical-unit metadata is ignored because output remains raw counts.
- Reject ambiguous/multiple mouse applications, duplicate axes/buttons,
  multiple mouse report IDs, mixed numbered/unnumbered input, absolute axes,
  arrays, null-state/wrapping/buffered controls, vendor mouse data, local
  delimiters/designators/strings, long/reserved items, overflow, truncated or
  unbalanced descriptors. Output/feature reports are not interpreted.
- Boot-capable mouse interfaces first try a supported report descriptor and
  explicit report protocol, retaining wheel/pan when supported. If the
  descriptor cannot be used, successful explicit SET_PROTOCOL(BOOT) selects
  the separately specified three-byte, three-button signed X/Y protocol.
  Reserved button bits must be zero. Extra boot bytes are rejected; wheel/pan
  are never inferred from them. This fallback does not interpret the rejected
  descriptor. Nonboot interfaces have no fallback.

These bounds deliberately exclude some mice, high-resolution feature-based
wheel multipliers, absolute pointing devices, digitizers and vendor protocols.
A rejected nonboot layout is cached until its interface attachment disappears.

## Work, state and recovery bounds

| Condition | Semantic result | Ownership and next action |
| --- | --- | --- |
| Read returns zero | No event; current buttons preserved | Normal poll; quiet endpoint tried once per invocation |
| Valid report | Ordered copied REPORT if motion/scroll/buttons changed | Current state updated before publication |
| Subscriber queue overflow | Only that subscriber enters sticky GAP | Snapshot replaces baseline; no fabricated motion |
| Detach / present false | One disconnect, released mask, zero current buttons | Retain session until close succeeds |
| Read error or malformed report | Affected subscribers enter GAP; affected mouse loses held state | Close it; no read/reclaim of same attachment |
| Raw scan/inventory failure | All subscribers enter GAP; all held states cleared | Close sessions; no use of stale inventory |
| Setup/read descriptor failure | No connected state is published | Retain attempted claim until close; bounded discovery retries |
| Unsupported nonboot descriptor | No connected state is published | Close; cache rejection until detach |
| Close returns false | All semantic states become disconnected/GAP | Preserve exact failed token with priority; close-only retry |
| Third false close of one session | RETAINED | Zero further dependency calls; refuse unload/restart |
| Final subscriber exits | No new consumer admission during quiesce | Close at most one session per quiesce call; retry while pending |

Storage is fixed: four mice, four subscribers, 32 copied events/subscriber,
16 attachment inspection records. No dynamic allocation or recursion. Discovery
attempts one claim/setup per poll, at most three attempts per attachment within
1,000ms, with 100ms backoff. Unsupported descriptors are not repeatedly parsed.
Raw HID control/descriptor calls are individually bounded to 100ms; a
boot-capable setup can require two such operations. Other lower calls inherit
the existing HID/host provider's own bounds.

Reports are bounded by both the requested count (maximum16) and a 20ms elapsed
budget measured from poll entry. Each raw read requests a 1ms timeout; a slow
scan/setup can consume that poll's report budget. Round-robin progress persists
across calls, so a budget of one still services multiple devices fairly. A
normal poll explicitly calls `platform.clock.sleep_ms(1)` to yield. Close-only
retries call only the exact retained `hid.close`, once per invocation; their
caller must yield between invocations. A close without a deadline parameter
inherits its lower provider's bounded cleanup contract. No watchdog feed is
substituted for scheduler cooperation.

### Lower-HID ownership boundary

The unchanged raw HID provider invokes a **void** `usb.host.release` and then
returns true from `hid.close`, clearing its own session. A semantic provider
cannot recover a failure result that the ABI does not expose. A true raw-HID
close is therefore not proof that the host/controller is physically quiescent.
The lower host must retain unsafe claims/quarantine and the generic graph must
keep that lower provider and its dependencies pinned. The composition fixture
explicitly checks this case instead of making a friendly release stub erase it.
False-close retention tests use the declared bool HID close contract directly.
No existing raw-HID ABI or implementation is changed to conceal this limit.

## Build and verification

Build with the existing shared builder and exact checked-out SDK:

```sh
NATIVE_DRIVER_CC=/path/to/xtensa-esp32s3-elf-gcc python scripts/build_usb_hid_mouse.py
python scripts/build_installed_usb_stack.py --ids usb-hid-mouse
./test/run_usb_hid_mouse_test.sh
ASAN_OPTIONS=detect_leaks=0 SANITIZERS=address,undefined ./test/run_usb_hid_mouse_test.sh
./test/run_usb_hid_test.sh
```

The GCC8 target uses `-fPIC -mtext-section-literals -mlongcalls -nostdlib
-nostartfiles -shared` like existing HID providers. Packaging discovers the new
manifest and conventional build script; no exporter/runtime catalog list was
added. The common builder's source-name validation includes the new source,
without changing the flags or output of existing drivers. The exact linked
provider is audited for export/import, Xtensa shared-ELF and loader relocations.

Tests execute the production C parser/driver, then load the separately compiled
production mouse and raw-HID shared libraries together. They cover supported
bit widths and report IDs, malformed descriptor corpus, boot fallback,
relative/wheel/pan/button events, multiple interfaces/devices/subscribers,
subscriber overflow recovery, disconnect releases, failed setup/read/scan,
discovery/elapsed/work bounds, exact-token close priority and terminal retention.
Existing HID/keyboard/gamepad/text/navigation/controller host regressions run
unchanged. LeakSanitizer is disabled in this ptrace-based executor; address and
undefined-behavior instrumentation remain enabled. This provider allocates no
heap memory.

The X4 `board.power.vbus` root is still electrically unqualified. Successful
host tests, ELF linking and package generation do not establish powered USB
attachment, real mouse interoperability, board safety or hardware input.
