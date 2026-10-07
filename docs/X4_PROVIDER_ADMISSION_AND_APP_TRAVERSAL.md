# X4 provider admission and app traversal follow-through

The physical 1.3.102 log moves past the former capability-inventory timeout:
inventory completes in 5,676/5,686 ms, followed immediately by a zero-ms
`board.battery` registration refusal. No battery peripheral activation is
shown. The supplied fragment begins after the RTC startup stage, so it does
not establish why hardware time was unavailable.

The immutable matching nine-package SD archive resolves battery API1, RTC
API2, I2C API1 and platform clock API1 through the real SD/FatFs/HAL/inspector
fixture. A non-null inventory does not imply that every package passed
inspection. Rejected clock or I2C metadata disables both optional consumers;
rejected battery metadata alone does not disable RTC. Missing metadata,
malformed headers/profiles, undeclared files, noncanonical case and a stale
storage generation reproduce distinct refusals. These are test cases, not
claims about the contents of the owner's card.

## Admission diagnostics

- Preserve existing admission and the finite inventory budget. No diagnostic
  rereads, peripheral probes, driver replacements or new authority are added.
- A cold scan reports accepted/rejected counts, cache eligibility and storage
  quiescence. Up to twelve rejected-package records identify the path and
  inspection stage, including the offending tree entry when observed.
- A failed in-memory resolution identifies absent inspected candidates,
  an unmet dependency/API, cycles/depth, or a changed storage generation.
  Failure logging is capped at four records per operation-owned snapshot.
- Keep a concrete transitive registration error instead of overwriting it
  with the parent capability's generic unavailable message.

The actual forty-provider SD/FatFs fixture retains its 3,383-sector count and
12,941 modeled ms. Diagnostic and silent inspections perform identical SD
reads for header, size and unknown-entry failures. Final-tree mutation and
manifest-close failures retain their original refusal/ownership semantics.

## App directory traversal

The same physical log attributes 13,242 ms to two Home pins, 4,497/4,499 ms
to recovery scans with 84 entries and zero transactions, and 22,659 ms to a
37-app inventory. Those paths still opened each child merely to inspect its
name and type. They now use the existing metadata cursor, check clean EOF
and directory close, and refuse partial/overbound results. Sidecars use the
existing direct regular-file reader, retaining the explicit directory guard
needed by the legacy storage backend.

An actual SD/FatFs/HAL fixture with 84 entries and 37 loose apps measures:

| Operation | Prior sectors / opens | Current sectors / opens |
| --- | ---: | ---: |
| Recovery | 1,458 / 85 | 30 / 1 |
| Two Home pins | 3,582 / 180 | 636 / 12 |
| Four Home pins | 7,164 / 360 | 1,272 / 24 |
| Springboard inventory | 6,961 / 208 | 3,507 / 40 |

Managed-package priority, ambiguous basenames, invalid payloads, interrupted
pair restoration, mapped-package protection, read/close faults and item/time
bounds remain covered. No new persistent cache or Home batching is added.
Per-app ELF/backup existence probes remain in the inventory cost; this change
does not claim to eliminate all startup work.

## Shared logo

Before: `X4DiagnosticBoot.cpp` drew a separate `Starting Reader / X4 Pro`
text splash. After: the same site calls `StartupScreen::staticLogo`, which
uses the existing complete `drawBootFrame` also used by T5's renderer
fallback. It presents one frame through the existing display owner, restores
the render mode and starts no animation task or boot-state transition.
Desk-clock user wake still skips the splash. The already-shared native app
loading helper is unchanged.

The production startup test covers all nine original logo rectangles and
labels at X4 480x800, a single presentation, unchanged loading state and no
video takeover. Existing T5 animation cancellation/ownership checks pass.

## Verification limits

Host checks use ASan/UBSan with leak detection disabled only because the
executor's ptrace environment prevents LeakSanitizer. Operation-count timing
is sensitivity modeling, not measured hardware speed. Firmware 1.3.105 is a
new candidate; delivered 1.3.102 and its artifacts remain immutable. Physical
startup, battery/RTC admission and interaction results require a fresh log.
