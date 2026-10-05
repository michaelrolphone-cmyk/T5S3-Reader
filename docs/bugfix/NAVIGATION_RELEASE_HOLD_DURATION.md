# Navigation release-frame hold duration

Firmware 1.3.137, based on X4 integration commit 36891e71. No driver/package ABI or payload changes.

## Reproduced defect

The shared reader waits for release when chapter skip or orientation change is enabled. NativeNavigationInput reset its hold origin when the button mask became zero, and its duration query returned zero whenever no button remained down. MappedInputManager also selected the untouched GPIO duration on X4 release. A one-second physical page press therefore behaved as a short press in EPUB/XTC.

## Repair

Retain the elapsed unsigned 32-bit duration before resetting the hold origin, only when one previously held button produces precisely its matching release with no concurrent press or held button. This value belongs to that frame and stays fixed when queried repeatedly. Clear it before every subsequent tick, including a tick inside the polling interval, and through the existing focus/reset/error/suspend/provider-release paths.

The legacy scalar duration cannot identify a chord or simultaneous input sources. Multi-button holds, substitutions, unmatched releases and mixed GPIO/provider gestures report zero. A remaining single button after a chord change starts a fresh interval at that transition. Ordinary GPIO-only timing and synthetic-tap zero duration are preserved. GPIO indexes are the shared, board-independent contiguous HalGPIO constants BTN_BACK=0 through BTN_PCA=7.

The shared EPUB chapter/orientation code, XTC ten-page skip, Back/Confirm bindings, provider frame masks and polling cadence are unchanged. This is independent of PR422's page-direction trait and requires no new SD driver.

## Evidence

`ASAN_OPTIONS=detect_leaks=0 python3 test/navigation_hold/run_test.py` compiles the actual complete navigation consumer and mapped-input methods, the complete XTC loop and the original EPUB page-turn block. It checks both directions; short, exact-threshold and long releases; chapter/orientation/page outcomes; press mode; repeated queries and gestures; semantic Back/Confirm; multi-button transitions and resumed single-button holds; injected/GPIO/mixed input; tilt and render barriers; focus, reset, poll failure, disable, failed release and provider replacement; and clock rollover. It runs ordinary and ASan/UBSan builds. The existing host aggregate invokes it.

The same test against the pre-repair production source fails at the retained release-duration assertion. LeakSanitizer is unavailable under the local ptrace executor and is disabled separately from ASan/UBSan. Device timing and physical qualification are unavailable; no flash or device action is part of this repair.
