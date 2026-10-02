#include "X4TxtActivity.h"

#if defined(BOARD_XTEINK_X4_PRO)

#include <GfxRenderer.h>
#include <Logging.h>
#include <RiscInputNavigationV1.h>
#include <cstdio>

#include "fontIds.h"
#include "MappedInputManager.h"
#include "activities/ActivityManager.h"

bool X4TxtActivity::StorageSource::available() const { return Storage.ready(); }

bool X4TxtActivity::StorageSource::list(const char* directory, std::vector<std::string>& names) {
  if (!available()) return false;
  const auto listed = Storage.listFiles(directory, 32);
  if (!available()) return false;
  names.reserve(listed.size());
  for (const auto& name : listed) names.emplace_back(name.c_str());
  return true;
}

bool X4TxtActivity::StorageSource::open(const std::string& path, size_t& bytes) {
  if (!available() || opened_) return false;
  file_ = Storage.open(path.c_str(), O_RDONLY);
  if (!file_.isOpen() || file_.isDirectory()) {
    (void)file_.close();
    return false;
  }
  opened_ = true;
  const uint64_t length = file_.fileSize64();
  if (length > X4TxtSession::kMaxBytes) return false;
  bytes = static_cast<size_t>(length);
  return true;
}

bool X4TxtActivity::StorageSource::readAt(size_t offset, uint8_t* output, size_t count) {
  if (!available() || !file_.isOpen() || !output || count > X4TxtSession::kPageBytes ||
      offset > X4TxtSession::kMaxBytes || count > X4TxtSession::kMaxBytes - offset)
    return false;
  if (!file_.seek(offset)) return false;
  return count == 0 || file_.read(output, count) == static_cast<int>(count);
}

bool X4TxtActivity::StorageSource::close() {
  if (!opened_) return true;
  if (!file_.close()) return false;
  opened_ = false;
  return true;
}

void X4TxtActivity::onEnter() {
  Activity::onEnter();
  session_.enter(source_);
  LOG_INF("X4", "txt files=%lu", static_cast<unsigned long>(session_.files().size()));
  requestUpdate();
}

void X4TxtActivity::onExit() {
  session_.leave(source_);
  Activity::onExit();
}

void X4TxtActivity::loop() {
  uint32_t pressed = 0, released = 0;
  if (mappedInput.wasPressed(MappedInputManager::Button::Left)) pressed |= RISC_NAV_LEFT;
  if (mappedInput.wasPressed(MappedInputManager::Button::Right)) pressed |= RISC_NAV_RIGHT;
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) pressed |= RISC_NAV_BACK;
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) released |= RISC_NAV_CONFIRM;
  const auto action = session_.input(pressed, released, source_);
  if (action == X4TxtSession::Action::Home) {
    activityManager.goHome();
    return;
  }
  if (action == X4TxtSession::Action::None) return;
  LOG_INF("X4", "txt view=%u selection=%lu offset=%lu",
          session_.view() == X4TxtSession::View::Page ? 1u : 0u,
          static_cast<unsigned long>(session_.selection()),
          static_cast<unsigned long>(session_.offset()));
  requestUpdate();
}

void X4TxtActivity::render(RenderLock&&) {
  renderer.clearScreen();
  renderer.drawCenteredText(UI_12_FONT_ID, 16,
                            session_.view() == X4TxtSession::View::List ? "TXT Files" : "TXT Preview");
  if (session_.view() == X4TxtSession::View::List) {
    renderer.drawText(UI_10_FONT_ID, 28, 58,
                      session_.selection() == 0 ? "> Home" : "  Home");
    const auto& files = session_.files();
    for (size_t i = 0; i < files.size(); ++i) {
      const std::string label = (session_.selection() == i + 1 ? "> " : "  ") + files[i];
      renderer.drawText(UI_10_FONT_ID, 28, 84 + static_cast<int>(i) * 26, label.c_str());
    }
  } else {
    const std::string& page = session_.page();
    for (size_t i = 0; i < 5 && i * 32 < page.size(); ++i) {
      const std::string line = page.substr(i * 32, 32);
      renderer.drawText(UI_12_FONT_ID, 28, 66 + static_cast<int>(i) * 40, line.c_str());
    }
    char position[64];
    std::snprintf(position, sizeof(position), "Byte %lu of %lu",
             static_cast<unsigned long>(session_.offset() + page.size()),
             static_cast<unsigned long>(session_.bytes()));
    renderer.drawText(UI_10_FONT_ID, 28, 344, position);
  }
  if (!session_.status().empty())
    renderer.drawText(UI_10_FONT_ID, 28, 410, session_.status().c_str());
  renderer.drawText(UI_10_FONT_ID, 28, 446,
                    session_.view() == X4TxtSession::View::List
                        ? "Left/Right: choose   Power: open"
                        : "Left/Right: page   Power: files");
  renderer.displayBuffer(DisplayPresentMode::Balanced);
}

#endif
