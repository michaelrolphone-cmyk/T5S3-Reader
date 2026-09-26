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
#include "components/FontAwesomeIcons.h"
#include "components/UITheme.h"
#include "LyraIcons.h"
#include "fontIds.h"

BaseTheme::ButtonMenuLayout LyraTheme::buttonMenuLayout(const GfxRenderer& renderer, Rect rect, int selectedIndex) const {
  (void)renderer;
  const int top = rect.y;
  const int height = LyraMetrics::values.menuRowHeight;
  const int step = height + LyraMetrics::values.menuSpacing;
  const int pageSize = std::max(1, (rect.height - (top - rect.y)) / step);
  return {top, height, step, pageSize, (std::max(0, selectedIndex) / pageSize) * pageSize};
}

void LyraTheme::drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                               const std::function<std::string(int index)>& buttonLabel,
                               const std::function<UIIcon(int index)>& rowIcon,
                               const std::function<const char*(int index)>& rowAppIcon) const {
  const auto layout = buttonMenuLayout(renderer, rect, selectedIndex);
  for (int i = layout.start; i < buttonCount && i < layout.start + layout.pageSize; ++i) {
    int tileWidth = rect.width - LyraMetrics::values.contentSidePadding * 2;
    Rect tileRect = Rect{rect.x + LyraMetrics::values.contentSidePadding,
                         layout.top + (i - layout.start) * layout.rowStep, tileWidth,
                         LyraMetrics::values.menuRowHeight};

    const bool selected = selectedIndex == i;

    if (selected) {
      renderer.fillRoundedRect(tileRect.x, tileRect.y, tileRect.width, tileRect.height, cornerRadius, Color::LightGray);
    }

    std::string labelStr = buttonLabel(i);
    const char* label = labelStr.c_str();
    int textX = tileRect.x + 16;
    const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
    const int textY = tileRect.y + (LyraMetrics::values.menuRowHeight - lineHeight) / 2;

    const char* appIcon = rowAppIcon ? rowAppIcon(i) : nullptr;
    if (appIcon && appIcon[0] != '\0') {
      constexpr int kAppIconSize = 12;
      const int iconX = textX + (mainMenuIconSize - kAppIconSize) / 2;
      const int iconY = tileRect.y + (LyraMetrics::values.menuRowHeight - kAppIconSize) / 2;
      (void)FontAwesomeIcons::draw(renderer, iconX, iconY, appIcon, kAppIconSize, true);
      textX += mainMenuIconSize + hPaddingInSelection + 2;
    } else if (rowIcon != nullptr) {
      UIIcon icon = rowIcon(i);
      const uint8_t* iconBitmap = iconForName(icon, mainMenuIconSize);
      if (iconBitmap != nullptr) {
        renderer.drawIcon(iconBitmap, textX, textY + 3, mainMenuIconSize, mainMenuIconSize);
        textX += mainMenuIconSize + hPaddingInSelection + 2;
      }
    }

    renderer.drawText(UI_12_FONT_ID, textX, textY, label, true);
  }
}

Rect LyraTheme::drawPopup(const GfxRenderer& renderer, const char* message) const {
  const int y = static_cast<int>(renderer.getScreenHeight() * 0.165f);
  constexpr int outline = 2;
  const int textWidth = renderer.getTextWidth(UI_12_FONT_ID, message, EpdFontFamily::REGULAR);
  const int textHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int w = textWidth + popupMarginX * 2;
  const int h = textHeight + popupMarginY * 2;
  const int x = (renderer.getScreenWidth() - w) / 2;

  renderer.fillRoundedRect(x - outline, y - outline, w + outline * 2, h + outline * 2, cornerRadius + outline,
                           Color::White);
  renderer.fillRoundedRect(x, y, w, h, cornerRadius, Color::Black);

  const int textX = x + (w - textWidth) / 2;
  const int textY = y + popupMarginY - 2;
  renderer.drawText(UI_12_FONT_ID, textX, textY, message, false, EpdFontFamily::REGULAR);
  renderer.displayBuffer();

  return Rect{x, y, w, h};
}

void LyraTheme::fillPopupProgress(const GfxRenderer& renderer, const Rect& layout, const int progress) const {
  constexpr int barHeight = 4;

  const int barWidth = layout.width - popupMarginX * 2;
  const int barX = layout.x + (layout.width - barWidth) / 2;
  const int barY = layout.y + layout.height - popupMarginY / 2 - barHeight / 2 - 1;

  int fillWidth = barWidth * progress / 100;

  renderer.fillRect(barX, barY, fillWidth, barHeight, false);

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
