#include "HomeActivity.h"

#include <Bitmap.h>
#include <Epub.h>
#include <FontCacheManager.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Utf8.h>
#include <Xtc.h>

#include <cstring>
#include <vector>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr char UTF8_ELLIPSIS[] = "\xE2\x80\xA6";

void appendTextKey(std::string& key, const std::string& text) {
  if (text.empty()) {
    return;
  }
  key.push_back('\n');
  key += text;
}

void recordUserContentText(FontCacheManager* fcm, const int systemFontId, const char* text,
                           const EpdFontFamily::Style style = EpdFontFamily::REGULAR) {
  if (fcm == nullptr || text == nullptr || text[0] == '\0') {
    return;
  }
  fcm->recordText(text, BaseTheme::resolveTextFontId(systemFontId, TextRole::UserContent), style);
}
}  // namespace

int HomeActivity::getMenuItemCount() const {
  int count = 5;  // File Browser, Recents, File transfer, Ask, Settings
  if (!recentBooks.empty()) {
    count += recentBooks.size();
  }
  if (hasOpdsServers) {
    count++;
  }
  return count;
}
