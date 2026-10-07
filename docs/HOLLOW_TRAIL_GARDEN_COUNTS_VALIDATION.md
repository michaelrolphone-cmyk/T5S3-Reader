# Garden counts: implementation and host validation

Date: 2026-10-06. Final app source: `55b3b4c0f5e9ff7a30e3ca0253144a6d62abfc64`.
Baseline: `641be8cc46ee8a95bceac5faab20405c5cd7c544`.
The cumulative app version remains **1.1.50**, using the 1.1.49 → 1.1.50 bump
already present in the baseline. Nothing was released or tested on a device.

## Connected behavior

The existing level-6/page-19 garden label now opens the clipped consecutive-morning
counts. The player takes both sheets to the clear pane, moves one on both axes,
compares their identical six-to-seven correction, and returns the overlaid pair
beneath a lifted empty pot. The shared stroke includes the fork, dot and unjoined
gap; a weak face remains behind the figures. The exact Chapter VII passage is
retained in the journal alongside the pre-existing mirror instructions. No dates
were invented. Route geometry, original mirror solution, evidence identity and
ending/read guards are unchanged.

## Checks and repairs

- Strict C focused test with AddressSanitizer/UBSan passed: actual route arrival,
  both HID/XInput mappings, bounded reversible alignment, exact-match gating,
  quiet holds, pause/notes/cancel/replay at every stage, fault and neutral-gated
  recovery, host exit, unchanged gameplay, both raster modes and reachable contacts.
- Exact novella prose and retained mirror-guidance check passed.
- All sections of `test/run_native_app_test.sh` completed successfully after two
  corrections. This was a resumed aggregate run, not an uninterrupted green run:
  - The new furniture changed only level 6/view 2 of the renderer references.
    Pixel inspection and a baseline comparison confirmed the other 29 pairs were
    unchanged. Updated that one pair to `2317935994 / 3755071145`; the complete
    renderer test then passed without weakening its assertions.
  - The strict C++ math consumer required every state field to be explicitly
    initialized. The complete initializer fixed that build; the remaining checks
    passed through native app launcher, panic capture and SDK logging.
- The app-specific builder and `build_all_apps.py --id hollow_trail --export-bundles`
  passed ELF structural/import validation, offline ZIP verification and manifest/
  archive/catalog version agreement. All remain 1.1.50.
- `git diff --check`, capture source-identity checks and Python compilation passed.

LeakSanitizer cannot run under this container's ptrace. Sanitized runs used
`ASAN_OPTIONS=detect_leaks=0`; address and undefined-behavior checks remained on.
No leak-check or hardware/panel performance result is claimed.

## Actual before/after pixels

`scripts/preview_hollow_trail_counts.py` walks the original route and uses normal
app input. The before build opens its old journal; the after build opens the
physical study. Nine views are captured in grayscale and production packed mono.
Comparison panels preserve the original pixels and label their verified source.

- Before source: `641be8cc46ee8a95bceac5faab20405c5cd7c544`
- Earlier reviewed capture source: `e0d217bf86a92cd9d842c46374a2a64c802666a9`
- Final capture source: `55b3b4c0f5e9ff7a30e3ca0253144a6d62abfc64`
- All **18 raw grayscale/mono frames are byte-identical** between the two after
  sources. Their ELF bytes differ; this is pixel equivalence, not binary equivalence.

The evidence directory has `before/`, the retained `after/` source-labeled set,
`after-final/`, `capture-equivalence.json`, and `package-final/`. Logs are
`native-suite.log` (initial aggregate and reviewed golden failure),
`renderer-final.log`, `native-remaining.log` (ending/pipeline passes and C++ warning),
`native-final.log` (successful remaining checks), `counts-final.log`, and
`package-build-final.log`. Images were inspected for pickup, carry, misalignment,
exact alignment, weak reflection, pot return and packed-monochrome readability.

## Final unpublished package SHA-256

- ELF: `37c7453d3735a45b0648d3ec34add47a90615b26a63644576338ba040fbf9410`
- Stamped app JSON: `96f5dea4890b9d3017ff8e093403c3c9af494b319c619cb8862cf70a80efb0e3`
- `.rte.zip`: `b603374f9f78ed349a6b41660dfda048465961ecc390072f5877572e4d2715fa`
- Package catalog: `28e7ef17de311fed08a669ee0972d5393c62d20df4dcc91a64f4e4c11daa2792`
