#!/usr/bin/env python3
"""Source contracts for responsive touch input and shared footer hit targets."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
HAL = (ROOT / "lib/hal/HalGPIO.cpp").read_text(encoding="utf-8")
THEME = (ROOT / "src/components/themes/BaseTheme.cpp").read_text(encoding="utf-8")


def constant(name: str) -> int:
    match = re.search(rf"{name}\s*=\s*(\d+)", HAL)
    if not match:
        raise AssertionError(f"missing {name}")
    return int(match.group(1))


release_grace = constant("TOUCH_RELEASE_GRACE_MS")
swipe_threshold = constant("TOUCH_SWIPE_THRESHOLD")

# A tap must be published quickly enough that the normal active-touch 10 ms loop
# does not impose hundreds of milliseconds of latency after finger release.
assert release_grace <= 50, release_grace

# Finger jitter should not turn an ordinary footer tap into a swipe, while the
# ActivityManager's deliberate global-menu drag (70 px) still clears this bound.
assert 30 <= swipe_threshold < 70, swipe_threshold

start = THEME.index("std::array<Rect, 4> BaseTheme::getButtonHintTouchBounds")
end = THEME.index("\nvoid BaseTheme::drawSideButtonHints", start)
bounds = THEME[start:end]
assert "touchPadTop = 20" in bounds
assert "touchPadOuterX = 12" in bounds
assert "(centers[i - 1] + centers[i]) / 2" in bounds
assert "buttonHeight + touchPadTop" in bounds

print("Touch latency and shared navigation hit-target contracts passed")
