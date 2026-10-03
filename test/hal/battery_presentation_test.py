#!/usr/bin/env python3
"""Compile the real battery painters: unavailable is never a fabricated 0%."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def function(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


base = (ROOT / 'src/components/themes/BaseTheme.cpp').read_text()
lyra = (ROOT / 'src/components/themes/lyra/LyraTheme.cpp').read_text()
rounded = (ROOT / 'src/components/themes/roundedraff/RoundedRaffTheme.cpp').read_text()
prefix = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <string>
#include <vector>
constexpr int SMALL_FONT_ID = 1;
constexpr int batteryPercentSpacing = 4;
struct Rect { int x, y, width, height; };
struct GfxRenderer {
  mutable std::vector<std::string> text;
  mutable unsigned fills = 0, lines = 0;
  void drawText(int, int, int, const char* s) const { text.emplace_back(s); }
  void fillRect(int, int, int, int, bool black = true) const { fills += black; }
  void drawLine(int, int, int, int, bool = true) const { ++lines; }
  void drawPixel(int, int) const {}
  int getTextWidth(int, const char* s) const { return std::string(s).size() * 8; }
  int getTextHeight(int) const { return 12; }
};
struct Power {
  bool available = false, charging = true;
  uint16_t percent = 0;
  unsigned chargeReads = 0;
  bool readBatteryPercentage(uint16_t* out) { if (!available) return false; *out = percent; return true; }
  bool isBatteryCharging() { ++chargeReads; return charging; }
} powerManager;
namespace BaseMetrics { struct Metrics { int batteryWidth = 15; }; constexpr Metrics values{}; }
namespace LyraMetrics { struct Metrics { int batteryWidth = 16; }; constexpr Metrics values{}; }
namespace RoundedRaffMetrics { struct Metrics { int batteryWidth = 15; }; constexpr Metrics values{}; }
struct BaseTheme {
  static constexpr int batteryPercentSpacing = 4;
  static void drawBatteryOutline(const GfxRenderer& r, int, int, int, int) { ++r.lines; }
  static void drawBatteryLightningBolt(const GfxRenderer& r, int, int) { ++r.lines; }
  static void drawBatteryUnknown(const GfxRenderer&, int, int, int, int);
  void drawBatteryLeft(const GfxRenderer&, Rect, bool) const;
  void drawBatteryRight(const GfxRenderer&, Rect, bool) const;
};
struct LyraTheme : BaseTheme {
  void drawBatteryLeft(const GfxRenderer&, Rect, bool) const;
  void drawBatteryRight(const GfxRenderer&, Rect, bool) const;
};
'''
unknown = function(base, 'void BaseTheme::drawBatteryUnknown(')
test = r'''
int main() {
  for (bool left : {false, true}) {
    for (bool show : {false, true}) {
      GfxRenderer renderer;
      powerManager.available = false; powerManager.chargeReads = 0;
      PAINT
      assert(renderer.fills == 0); // No empty-cell fill and no false charging bolt.
      assert(renderer.lines >= 3); // Outline plus explicit unavailable cross.
      assert(powerManager.chargeReads == 0);
      assert(renderer.text == (show ? std::vector<std::string>{"--%"} : std::vector<std::string>{}));
      for (uint16_t percentage : {0u, 55u, 100u}) {
        renderer = {}; powerManager.available = true;
        powerManager.percent = percentage; powerManager.charging = false;
        PAINT
        assert(renderer.text == (show ? std::vector<std::string>{std::to_string(percentage) + "%"} : std::vector<std::string>{}));
        assert(renderer.lines > 0);
      }
      // A failed next sample must not leave the preceding numeric value visible.
      renderer = {}; powerManager.available = false;
      PAINT
      assert(renderer.fills == 0);
      assert(renderer.text == (show ? std::vector<std::string>{"--%"} : std::vector<std::string>{}));
    }
  }
}
'''
cases = [
    ('base', base, ('void drawBatteryIcon(', 'void BaseTheme::drawBatteryLeft(', 'void BaseTheme::drawBatteryRight('),
     'if (left) BaseTheme{}.drawBatteryLeft(renderer, {30,20,15,12}, show); else BaseTheme{}.drawBatteryRight(renderer, {30,20,15,12}, show);'),
    ('lyra', lyra, ('void drawLyraBatteryIcon(', 'void LyraTheme::drawBatteryLeft(', 'void LyraTheme::drawBatteryRight('),
     'if (left) LyraTheme{}.drawBatteryLeft(renderer, {30,20,16,12}, show); else LyraTheme{}.drawBatteryRight(renderer, {30,20,16,12}, show);'),
    ('rounded', rounded, ('void drawBatteryIcon(', 'void drawBatteryRightStable('),
     '(void)left; drawBatteryRightStable(renderer, {30,20,15,12}, powerManager.percent, powerManager.available, show);'),
]
with tempfile.TemporaryDirectory(prefix='battery-presentation-') as temp:
    for name, source, signatures, paint in cases:
        cpp = Path(temp) / (name + '.cpp')
        binary = Path(temp) / name
        cpp.write_text(prefix + unknown + '\n' + '\n'.join(function(source, s) for s in signatures) + test.replace('PAINT', paint))
        subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(binary)], check=True, timeout=30)
        subprocess.run([str(binary)], check=True, timeout=10)
# X4 participates in the common header painter; hardware/reader availability is
# represented in that painter rather than a board-specific hidden indicator.
header = function(base, 'void BaseTheme::drawHeader(')
assert 'BOARD_XTEINK_X4_PRO' not in header
assert 'drawBatteryRight(' in header
hal = (ROOT / 'lib/hal/HalPowerManager.cpp').read_text()
# Compile both real HAL branches. The fixture distinguishes cached Board reads
# from the legacy percentage/USB methods so a new path cannot silently regress
# existing boards or fall back to a manufactured X4 percentage.
hal_prefix = r"""
#include <cassert>
#include <cstdint>
namespace Board {
bool valid = false, charging = false, usb = true;
uint16_t value = 0;
struct BatteryState { bool charging = false; };
bool readBatteryStateOfCharge(uint16_t* out) { *out = 0; if (!valid) return false; *out = value; return true; }
bool readBatteryState(BatteryState* out) { out->charging = charging; return valid; }
bool isUsbConnected() { return usb; }
}
struct HalPowerManager {
 mutable unsigned legacyReads = 0;
 uint16_t getBatteryPercentage() const { ++legacyReads; return 42; }
 bool readBatteryPercentage(uint16_t*) const;
 bool isBatteryCharging() const;
};
"""
hal_test = r"""
int main() {
 HalPowerManager power; uint16_t percentage = 99;
 assert(!power.readBatteryPercentage(nullptr));
#if defined(BOARD_XTEINK_X4_PRO)
 assert(!power.readBatteryPercentage(&percentage) && percentage == 0);
 assert(!power.isBatteryCharging());
 for (uint16_t v : {0u, 55u, 100u}) {
   Board::valid = true; Board::value = v; Board::charging = true;
   assert(power.readBatteryPercentage(&percentage) && percentage == v);
   assert(power.isBatteryCharging());
 }
 Board::valid = false;
 assert(!power.readBatteryPercentage(&percentage) && percentage == 0);
 assert(!power.isBatteryCharging());
 assert(power.legacyReads == 0);
#else
 assert(power.readBatteryPercentage(&percentage) && percentage == 42);
 assert(power.isBatteryCharging());
 Board::usb = false; assert(!power.isBatteryCharging());
 assert(power.legacyReads == 1);
#endif
}
"""
with tempfile.TemporaryDirectory(prefix='battery-hal-') as temp:
    cpp = Path(temp) / 'hal.cpp'
    binary = Path(temp) / 'hal'
    cpp.write_text('#include <initializer_list>\n' + hal_prefix + '\n'.join(function(hal, sig) for sig in
                   ('bool HalPowerManager::readBatteryPercentage(', 'bool HalPowerManager::isBatteryCharging(')) + hal_test)
    for board in ('BOARD_XTEINK_X4_PRO', 'BOARD_T5S3_PRO', 'BOARD_LILYGO_EPD47_S3'):
        subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-D' + board, str(cpp), '-o', str(binary)], check=True, timeout=30)
        subprocess.run([str(binary)], check=True, timeout=10)
print('Battery production painters/HAL: Base/Lyra/Rounded, X4 validity/recovery, T5/EPD47 preservation PASS')
