#!/usr/bin/env python3
"""Production-source regression for touch confirmation hit testing.

Extracts the two real confirmation functions and compiles them against a small
host harness. Ordinary body/header taps must not finish either destructive dialog;
mapped hint bounds and physical buttons retain their explicit behavior.
"""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def extract_function(source: str, qualified_name: str) -> str:
    match = re.search(r"\b(?:bool|void)\s+" + re.escape(qualified_name) + r"\s*\(", source)
    if not match:
        raise AssertionError(f"missing production function {qualified_name}")
    opening = source.find("{", match.end())
    if opening < 0:
        raise AssertionError(f"missing body for {qualified_name}")
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[match.start():index + 1]
    raise AssertionError(f"unterminated body for {qualified_name}")


confirmation_source = (ROOT / "src/activities/util/ConfirmationActivity.cpp").read_text()
global_menu_source = (ROOT / "src/activities/GlobalMenuActivity.cpp").read_text()
activity_manager_source = (ROOT / "src/activities/ActivityManager.cpp").read_text()

tap_method = extract_function(confirmation_source, "ConfirmationActivity::onTouchTap")
loop_method = extract_function(confirmation_source, "ConfirmationActivity::loop")
modal_method = extract_function(global_menu_source, "modalShutdownConfirmed")

# The firmware ActivityManager must continue routing actual button-hint hits to
# mapped-button input, leaving only ordinary screen taps for onTouchTap().
for required in (
    "currentActivity->resolveTouchButtonHint(touchPoint.x, touchPoint.y, touchButton)",
    "mappedInput.injectButtonTap(touchButton)",
    "currentActivity->onTouchTap(touchPoint.x, touchPoint.y)",
):
    if required not in activity_manager_source:
        raise AssertionError(f"touch routing contract changed: missing {required}")

uses_menu_resolver = "GlobalMenuActivity& menu" in modal_method.split("{", 1)[0]
modal_call = (
    "modalShutdownConfirmed(menu, renderer, input)"
    if uses_menu_resolver
    else "modalShutdownConfirmed(renderer, input)"
)

harness = r"""
#include <cstdint>
#include <iostream>
#include <string>
#include <utility>

enum class EpdFontFamily { BOLD, REGULAR };
enum class DisplayPresentMode { Quality };
enum class StrId { STR_SHUTDOWN, STR_SHUTDOWN_PROMPT, STR_CANCEL, STR_CONFIRM };
constexpr int UI_10_FONT_ID = 10;

struct ActivityResult { bool isCancelled = true; };

class GfxRenderer {
public:
    int width = 960;
    int getLineHeight(int) const { return 20; }
    int getScreenWidth() const { return width; }
    int getScreenHeight() const { return 540; }
    std::string truncatedText(int, const char* text, int, EpdFontFamily) const { return text; }
    void clearScreen() {}
    void drawCenteredText(int, int, const char*, bool, EpdFontFamily) {}
    void displayBuffer(DisplayPresentMode) {}
};

class MappedInputManager {
public:
    enum class Button { Back, Left, Right, Confirm };
    struct TouchPoint { int16_t x = 0; int16_t y = 0; };
    struct Labels { const char* btn1; const char* btn2; const char* btn3; const char* btn4; };
    MappedInputManager::Button released = MappedInputManager::Button::Back;
    bool releaseAvailable = false;
    MappedInputManager::TouchPoint touch{};
    bool touchAvailable = false;
    bool touched = false;
    bool endAfterBodyTouch = false;
    int updates = 0;

    void update() { ++updates; }
    bool wasTouchHomeButtonPressed() { return false; }
    bool wasReleased(MappedInputManager::Button button) {
        if (releaseAvailable && released == button) { releaseAvailable = false; return true; }
        if (button == MappedInputManager::Button::Back && endAfterBodyTouch && touched) return true;
        return false;
    }
    bool wasTouchTapped(MappedInputManager::TouchPoint& point, const GfxRenderer&) {
        if (!touchAvailable || touched) return false;
        touched = true;
        point = touch;
        return true;
    }
    MappedInputManager::Labels mapLabels(const char*, const char*, const char*, const char*) {
        return {"", "", "Cancel", "Confirm"};
    }
};

class GlobalMenuActivity {
public:
    int resolutionChecks = 0;
    bool resolveTouchButtonHint(int16_t x, int16_t y, MappedInputManager::Button& button) {
        ++resolutionChecks;
        if (y < 480 || y >= 540) return false;
        if (x >= 900 && x < 960) { button = MappedInputManager::Button::Right; return true; }
        if (x >= 0 && x < 60) { button = MappedInputManager::Button::Left; return true; }
        return false;
    }
};

class ConfirmationActivity {
public:
    GfxRenderer& renderer;
    MappedInputManager& mappedInput;
    bool hasResult = false;
    bool finished = false;
    ActivityResult result{};
    ConfirmationActivity(GfxRenderer& r, MappedInputManager& i) : renderer(r), mappedInput(i) {}
    void setResult(ActivityResult&& value) { hasResult = true; result = value; }
    void finish() { finished = true; }
    void loop();
    bool onTouchTap(int16_t x, int16_t y);
};

struct I18n {
    const char* get(StrId id) const {
        return id == StrId::STR_SHUTDOWN ? "Shutdown" :
               id == StrId::STR_SHUTDOWN_PROMPT ? "Shut down now?" :
               id == StrId::STR_CANCEL ? "Cancel" : "Confirm";
    }
};
I18n I18N;
struct Gui {
    void drawButtonHints(GfxRenderer&, const char*, const char*, const char*, const char*) {}
};
Gui GUI;
void delay(int) {}
void esp_task_wdt_reset() {}

__CONFIRMATION_LOOP__
__CONFIRMATION_TAP__
__GLOBAL_MODAL__

static int failures = 0;
static void expect(bool condition, const char* message) {
    if (!condition) { std::cerr << message << "\n"; ++failures; }
}

static void testOrdinaryTapsNeverCompleteConfirmation() {
    const int widths[] = {960, 540};
    for (int width : widths) {
        const int xs[] = {0, 1, width / 2 - 1, width / 2, width - 1};
        const int ys[] = {0, 1, 270, 538, 539};
        for (int x : xs) for (int y : ys) {
            GfxRenderer renderer;
            renderer.width = width;
            MappedInputManager input;
            ConfirmationActivity activity(renderer, input);
            expect(activity.onTouchTap(static_cast<int16_t>(x), static_cast<int16_t>(y)),
                   "ConfirmationActivity did not consume an ordinary tap");
            expect(!activity.hasResult && !activity.finished,
                   "ordinary tap completed a confirmation dialog");
        }
    }
}

static void testMappedPhysicalButtonsStillWork() {
    for (auto button : {MappedInputManager::Button::Right, MappedInputManager::Button::Left}) {
        GfxRenderer renderer;
        MappedInputManager input;
        input.releaseAvailable = true;
        input.released = button;
        ConfirmationActivity activity(renderer, input);
        activity.loop();
        expect(activity.hasResult && activity.finished,
               "mapped physical confirmation button no longer completes the dialog");
        expect(activity.result.isCancelled == (button == MappedInputManager::Button::Left),
               "mapped cancel/confirm result changed");
    }
}

static void testEmbeddedModalUsesMappedHintBounds() {
    const MappedInputManager::TouchPoint bodyPoints[] = {{100, 270}, {900, 270}};
    for (const auto& point : bodyPoints) {
        GfxRenderer renderer;
        MappedInputManager input;
        input.touch = point;
        input.touchAvailable = true;
        input.endAfterBodyTouch = true;
        GlobalMenuActivity menu;
        const bool approved = __MODAL_CALL__;
        (void)menu;
        expect(!approved, "ordinary embedded-modal body tap approved shutdown");
        if (__USES_MENU_RESOLVER__) {
            expect(menu.resolutionChecks == 1, "embedded modal did not hit-test the tapped point");
        }
    }

    for (const auto& item : {
             std::pair<MappedInputManager::TouchPoint, MappedInputManager::Button>{{930, 520}, MappedInputManager::Button::Right},
             std::pair<MappedInputManager::TouchPoint, MappedInputManager::Button>{{30, 520}, MappedInputManager::Button::Left}}) {
        GfxRenderer renderer;
        MappedInputManager input;
        input.touch = item.first;
        input.touchAvailable = true;
        GlobalMenuActivity menu;
        const bool approved = __MODAL_CALL__;
        (void)menu;
        expect(approved == (item.second == MappedInputManager::Button::Right),
               "embedded modal hint did not preserve mapped Cancel/Confirm behavior");
    }
}

int main() {
    testOrdinaryTapsNeverCompleteConfirmation();
    testMappedPhysicalButtonsStillWork();
    testEmbeddedModalUsesMappedHintBounds();
    return failures == 0 ? 0 : 1;
}
"""

harness = harness.replace("__CONFIRMATION_LOOP__", loop_method)
harness = harness.replace("__CONFIRMATION_TAP__", tap_method)
harness = harness.replace("__GLOBAL_MODAL__", modal_method)
harness = harness.replace("__MODAL_CALL__", modal_call)
harness = harness.replace("__USES_MENU_RESOLVER__", "true" if uses_menu_resolver else "false")

with tempfile.TemporaryDirectory(prefix="confirmation-touch-") as directory:
    binary = Path(directory) / "confirmation_touch_test"
    subprocess.run(
        ["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-x", "c++", "-", "-o", str(binary)],
        input=harness,
        text=True,
        check=True,
    )
    result = subprocess.run([str(binary)], text=True, capture_output=True)
    if result.stdout:
        print(result.stdout, end="")
    if result.stderr:
        print(result.stderr, end="", file=sys.stderr)
    sys.exit(result.returncode)
