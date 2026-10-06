# Keyboard line fitting

Report: [PERF-20261005-UI-KEYBOARD-SUFFIX-REMEASUREMENT](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/332#issuecomment-6003772465).
Claim: [2026-10-06 repair](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/332#issuecomment-6007994166).
Repository: `michaelrolphone-cmyk/T5S3-Reader` (1367546328).
Canonical bug number remains pending the existing ledger coordinator.

Source and repair base: `xteink-x4-pro-boot`,
`b91bc323fb736eb6a13ea1a9403470cf699c6128`, [PR #350](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/350).
Repair branch: `perf/keyboard-prefix-fit`. PR #350 is not yet integrated into
master `21ce3b5b720e106815c8f3a7778bc7003294e0e2`; PR #348 is closed-unmerged
and retained through PR #350. The separate [performance consolidation PR #433](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/433)
at `9e8693be42ede01a6b9c9bfd66010905751df9a2` retains the same keyboard code.
Firmware advances **1.3.143 → 1.3.148**, above the live 1.3.147 reservation;
no independently distributed app, driver, service or provider changes.

## Change and compatibility

`KeyboardEntryActivity::render` and `measureInputHeightForTouch` previously
measured a whole remaining suffix, removed one byte, and measured again until
the prefix fit. Each subsequent line restarted from the remaining full suffix.

An overflowing new line now tries `GfxRenderer::getTextFittingPrefix`. The
helper calculates every candidate advance once in one forward pass, preserving
the renderer's differential rounding, missing-glyph behavior, font-style
selection, kerning and greedy chained ligatures. Widths can decrease: it selects
the **last** fitting prefix rather than assuming monotonic widths. The existing
renderer still measures and draws the selected line and calculates the cursor.
Short input that already fits retains its original single measurement.

The helper is optional and operation-local. It stores only scalar state and
does not cache text, font pointers or metrics between calls. It accepts at most
4096 printable ASCII bytes from a resident font without a lazy glyph callback.
SD fonts, missing/lazy fonts, Unicode, malformed bytes, control bytes, embedded
NUL, longer suffixes, no nonempty fit and timeout all use the original shrinking
algorithm. **4096 is not an input limit.** No text is rejected or truncated.
Password masking/reveal, byte-based cursor behavior, line boundaries, field
geometry, keys, hints, themes and render/input ownership remain unchanged.

CPU cooperation occurs after 256 bytes or 8 elapsed milliseconds with a real
`vTaskDelay(1)`. A 1000-ms per-attempt deadline abandons only the optional path;
the original behavior remains available. The existing renderer/activity locks
are retained. No package, storage, ELF validation or admission policy changes.

## Verification

The focused regression compiles the complete production keyboard render and
touch-height methods, real Ubuntu12/EpdFont/EpdFontFamily/UTF-8 code, and the
production renderer measurement/fitting methods. Display, controller, theme
and scheduling boundaries are fixtures. It compares complete draw records
against the unmodified source and checks deterministic metric work rather than
wall-clock or hardware speed. Synthetic font cases cover nonmonotonic widths,
negative kerning and chained ligatures; optional-path rejection and retry do
not retain partial state.

All **173** complete draw/touch-height snapshots match the original source
(SHA256 `1fb62b8d9171efe9ce4d78984cb0b15ffa2e992e5dba65e30be7a12610f91bb2`).
The direct helper oracle performs **9,231** exact-prefix comparisons. Normal and
ASan/UBSan runs pass. The original source fails the enforced cost budget.

Deterministic **per-render** counts for the checked-in fixture:

| Input bytes / screen width | Measurement calls, before → after | Measurement argument bytes, before → after | Glyph lookups, before → after |
| --- | --- | --- | --- |
| 280 prose / 400 | 1,171 → 20 | 138,099 → 1,720 | 132,689 → 3,086 |
| 280 prose / 540 | 831 → 14 | 105,579 → 1,383 | 101,094 → 2,396 |
| 383 URL / 400 | 2,526 → 30 | 377,918 → 3,276 | 369,355 → 6,102 |
| 383 URL / 540 | 1,814 → 22 | 285,004 → 2,568 | 278,324 → 4,685 |
| 1,024 stress / 400 | 17,607 → 72 | 6,430,603 → 19,618 | 5,147,545 → 34,262 |
| 1,024 stress / 540 | 12,627 → 52 | 4,726,548 → 14,648 | 3,776,211 → 25,286 |
| 2,048 stress / 400 | 71,367 → 142 | 50,429,086 → 75,391 | 40,356,673 → 133,665 |
| 2,048 stress / 540 | 51,461 → 104 | 36,833,712 → 55,504 | 29,446,375 → 97,830 |

These fixture strings are specified in the regression, rather than claimed to
be the owner's text or the earlier report's exact prose. They exercise the
shipped font and real portrait screen widths. Long strings are stress inputs.
Each suffix still requires a linear metrics pass; the complete multi-line
operation is bounded quadratic work on this optional path, not a claim of
linear whole-input layout. No accepted-text restriction was introduced.

Repeat/insert/erase, cursor and password modes, selected password toggles,
centered/left-aligned text, keyboard positioning, side hints, missing glyphs,
all style values, unsupported fonts/text, 4096/4097-byte boundaries, failure
followed by retry, elapsed/item checkpoints and 32-bit clock rollover are
covered. Lazy/SD-font fallback keeps the exact baseline measurement calls and
arguments. Other suffixes can become eligible only after unsupported bytes
have left the remaining line input.

Reproduce from the repository root:

```sh
python3 test/activities/keyboard_prefix_fit_test.py --baseline-ref b91bc323fb736eb6a13ea1a9403470cf699c6128 --output /tmp/keyboard-baseline.records
python3 test/activities/keyboard_prefix_fit_test.py --enforce-cost --compare /tmp/keyboard-baseline.records
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 PREFIX_SANITIZE=1 python3 test/activities/keyboard_prefix_fit_test.py --enforce-cost --compare /tmp/keyboard-baseline.records
```

Adding `--enforce-cost` to the baseline command must fail. The sanitizer test
is wired into the existing Springboard host aggregate and normal firmware CI.

No physical display, touch, SD-font device or scheduling measurement is claimed.
Host sanitizers cannot establish hardware performance. LeakSanitizer is
unavailable under the traced local executor; ASan and UBSan are run separately
from that limitation. No merge, release, deployment, flashing or device action.
