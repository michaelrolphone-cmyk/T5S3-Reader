#include "LyraTheme.h"

#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <I18n.h>

#include <cstdint>
#include <string>
#include <vector>

#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "LyraIcons.h"
#include "fontIds.h"

namespace {
void drawLyraBatteryIcon(const GfxRenderer& renderer, int x, int y, int battWidth, int rectHeight,
                         uint16_t percentage) {
  BaseTheme::drawBatteryOutline(renderer, x, y, battWidth, rectHeight);

  const bool charging = gpio.isUsbConnected();

  if (charging) {
    renderer.fillRect(x + 2, y + 2, battWidth - 5, rectHeight - 4);
    BaseTheme::drawBatteryLightningBolt(renderer, x + 4, y + 2);
  } else {
    if (percentage > 10) {
      renderer.fillRect(x + 2, y + 2, 3, rectHeight - 4);
    }
    if (percentage > 40) {
      renderer.fillRect(x + 6, y + 2, 3, rectHeight - 4);
    }
    if (percentage > 70) {
      renderer.fillRect(x + 10, y + 2, 3, rectHeight - 4);
    }
  }
}
}  // namespace

void LyraTheme::drawBatteryLeft(const GfxRenderer& renderer, Rect rect, const bool showPercentage) const {
  const uint16_t percentage = powerManager.getBatteryPercentage();

  if (showPercentage) {
    const auto percentageText = std::to_string(percentage) + "%";
    renderer.drawText(SMALL_FONT_ID, rect.x + BaseTheme::batteryPercentSpacing + LyraMetrics::values.batteryWidth,
                      rect.y, percentageText.c_str());
  }

  drawLyraBatteryIcon(renderer, rect.x, rect.y + 6, LyraMetrics::values.batteryWidth, rect.height, percentage);
}

void LyraTheme::drawBatteryRight(const GfxRenderer& renderer, Rect rect, const bool showPercentage) const {
  const uint16_t percentage = powerManager.getBatteryPercentage();

  if (showPercentage) {
    const auto percentageText = std::to_string(percentage) + "%";
    const int textWidth = renderer.getTextWidth(SMALL_FONT_ID, percentageText.c_str());
    const auto textHeight = renderer.getTextHeight(SMALL_FONT_ID);
    renderer.fillRect(rect.x - textWidth - BaseTheme::batteryPercentSpacing, rect.y, textWidth, textHeight, false);
    renderer.drawText(SMALL_FONT_ID, rect.x - textWidth - BaseTheme::batteryPercentSpacing, rect.y,
                      percentageText.c_str());
  }

  drawLyraBatteryIcon(renderer, rect.x, rect.y + 6, LyraMetrics::values.batteryWidth, rect.height, percentage);
}
