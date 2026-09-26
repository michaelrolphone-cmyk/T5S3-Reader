#!/usr/bin/env python3
"""Source-level lifecycle guards; physical wake and current draw require a board."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
CLOCK = (ROOT / "src/DeskClockSleep.cpp").read_text()
DISPLAY = (ROOT / "lib/hal/HalDisplay.cpp").read_text()
SYSTEM = (ROOT / "lib/hal/HalSystem.cpp").read_text()
MAIN = (ROOT / "src/main.cpp").read_text()


class ClockDeepSleepWiring(unittest.TestCase):
    def test_timer_and_power_button_are_both_armed(self):
        self.assertIn("esp_sleep_enable_timer_wakeup(waitUs)", CLOCK)
        self.assertIn("esp_deep_sleep_start()", CLOCK)
        self.assertIn("esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL)", CLOCK)
        self.assertIn("1ULL << BoardPins::PowerButton", CLOCK)

    def test_no_touch_wake_or_light_sleep(self):
        self.assertNotIn("TouchInterrupt", CLOCK)
        self.assertNotIn("esp_light_sleep_start", CLOCK)
        self.assertNotIn("esp_sleep_enable_gpio_wakeup()", CLOCK)

    def test_timer_wakes_reenter_before_normal_boot(self):
        self.assertIn("wakeCause == ESP_SLEEP_WAKEUP_TIMER", CLOCK)
        self.assertIn("RTC_DATA_ATTR ClockRetention", CLOCK)
        self.assertLess(SYSTEM.index("DeskClockSleep::resumeAfterTimerWake()"),
                        SYSTEM.index("if (!isRebootFromPanic())"))
        resume = CLOCK.index("bool DeskClockSleep::resumeAfterTimerWake")
        self.assertLess(CLOCK.index("clockState.magic = 0;", resume),
                        CLOCK.index("Board::begin();", resume))

    def test_user_wake_skips_splash_but_preserves_resume_policy(self):
        self.assertIn("bool DeskClockSleep::consumeUserWake()", CLOCK)
        self.assertIn("const bool deskClockUserWake = DeskClockSleep::consumeUserWake();", MAIN)
        self.assertIn("&& !deskClockUserWake", MAIN)
        self.assertIn("if (!deskClockUserWake)", MAIN)
        self.assertIn("const bool resumeReaderOnBoot = shouldResumeReaderOnBoot();", MAIN)
        self.assertLess(MAIN.index("const bool resumeReaderOnBoot = shouldResumeReaderOnBoot();"),
                        MAIN.index("} else if (!resumeReaderOnBoot)"))
        self.assertNotIn("deskClockUserWake && !resumeReaderOnBoot", MAIN)

    def test_button_during_timer_repaint_survives_explicit_restart(self):
        self.assertIn("clockUiWakeMagic = kClockUiWakeMagic;", CLOCK)
        self.assertIn("if (clockUiWakeMagic == kClockUiWakeMagic)", CLOCK)
        self.assertIn("userWakePending = true;", CLOCK)

    def test_timer_boot_avoids_touch_sd_and_app_startup(self):
        resume = CLOCK.split("bool DeskClockSleep::resumeAfterTimerWake()", 1)[1]
        for forbidden in ("Storage.begin", "touch.begin", "activityManager", "WiFi.begin"):
            self.assertNotIn(forbidden, resume)
        self.assertIn("display.deepSleep()", CLOCK)
        self.assertIn("Board::deinitForSleep()", CLOCK)
        self.assertIn("display.begin(false)", resume)
        self.assertNotIn("display.begin();", resume)
        self.assertIn("init_impl(true, false)", DISPLAY)

    def test_timer_refresh_reconstructs_previous_frame_and_clips_diff(self):
        self.assertIn("displayedMinuteEpoch", CLOCK)
        self.assertIn("renderClockFrame(gfx, previousDisplayedMinute)", CLOCK)
        self.assertIn("memcpy(previousFrame, display.getFrameBuffer(), display.getBufferSize())", CLOCK)
        self.assertIn("display.displayBufferDiff(previousFrame, HalDisplay::HALF_REFRESH)", CLOCK)
        self.assertIn("frameBuffer[index] == previousBuffer[index]", DISPLAY)
        self.assertIn("renderBwToPanelCanvas(previousBuffer)", DISPLAY)
        self.assertIn("gfx->setPanelOutputSuppressed(true)", DISPLAY)
        self.assertIn("gfx->setPanelOutputSuppressed(false)", DISPLAY)
        self.assertIn("gfx->setClipRect(clipX, clipY, clipW, clipH)", DISPLAY)


if __name__ == "__main__":
    unittest.main()
