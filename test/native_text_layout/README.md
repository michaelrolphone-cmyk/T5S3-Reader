# Native text layout cache regression

Run from the repository root:

```sh
python3 test/native_text_layout/run_tests.py
python3 test/native_text_layout/run_tests.py --sanitize
```

ASan and UBSan are fatal (`-fno-sanitize-recover=all`). On runners where ptrace
prevents LeakSanitizer from running, use `ASAN_OPTIONS=detect_leaks=0` with
`--sanitize`; this does not disable address or undefined-behavior checks.

- `cache_test.cpp` includes the production cache directly and covers exact bytes,
  mutable buffers, every key field, Unicode/empty lines, admission boundaries,
  optional allocation failure, all copy-checkpoint failures, reset and retry.
- `baseline_render_oracle.inc` is the verbatim pre-cache render function from
  commit `cbf44781cb36c12a716f32fcfbdb31ce95b40342`. It must remain independent of
  cache implementation changes.
- The runner extracts the current production render/key/budget/reset functions
  verbatim and compares their frame, presentation, result and hit-test metadata
  against that frozen oracle. Fixtures supply deterministic font metrics and
  capture draw calls; they do not claim hardware, font-rasterization or SD proof.
- Repeated randomized cases and explicit text/font/geometry/chrome/scroll changes
  cover cold and warm rendering. SD/lazy/missing fonts and size/deadline rejection
  retain original rendering. Lifecycle checks require entry/exit and alternate
  surface cleanup and owner-task gating.
- The production `BaseTheme::resolveTextFontId` body is exercised with a counting,
  changing selected-SD resolver. Baseline/current resolution count, phase, order,
  returned IDs, and rendering must match, including missing-SD built-in fallback.
