# Cabinet room and linked releases

The existing final choice now sits on a bolted table in the book's modest stone room. Its single aperture retains the established outside view. A pinned rubbing, two worn mat hollows and the absence of chairs ground the setting without fabricated dates or inscriptions.

The black and white grips share one slot and opposing linkage. Selecting black exposes the sealed original and construction lines while an opaque face covers the live page. Selecting white seals the master and raises the live glass; only that released live pen moves. The lowered grips remain reachable from the existing player stance. The neutral pen stays still.

This is a drawing change: existing choice, confirmation, testimony acknowledgement and final-door guards remain exact. Pantry-lamp ignition, arranging the floor papers and memory/hand-on-glass gestures remain open. Whole-book parity is unfinished.

[Four actual comparisons](images/hollow-trail-cabinet-room/README.md) show source `4e047dcc` → `95a32547`. The tested source and these comparisons are published in PR414.

## Verification

Full native aggregate passes with ASan/UBSan. Focused tests cover exclusive master/live visibility, live-only pen movement, reachable grips, grayscale and packed monochrome, unchanged choice/read state and ending exclusions. Existing ending tests pass both releases, cancellation, display/read acknowledgement, history and exit. C++ math and all 30 renderer references pass unchanged.

S3 build, ELF structure and package/version checks pass. ELF: 363460 bytes; SHA-256 `280fc1ac6803a36919493526ed0aed9bffee3df9669cd6036c219110c27a3405`. Cumulative app version 1.1.48 → 1.1.49; minimum firmware 1.3.37.
