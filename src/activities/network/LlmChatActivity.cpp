#include "LlmChatActivity.h"

#include <I18n.h>
#include <WiFi.h>
#include <esp_task_wdt.h>

#include "MappedInputManager.h"
#include "WifiSelectionActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/Llm7Client.h"

namespace {
constexpr int kHistoryLimit = 6;
constexpr int kQuestionMaxLen = 280;
}  // namespace

void LlmChatActivity::onEnter() {
  Activity::onEnter();
  statusLine = tr(STR_LLM_HINT);
  requestUpdate();
  if (WiFi.status() != WL_CONNECTED && !wifiPrompted) {
    wifiPrompted = true;
    ensureWifiThenAsk();
  }
}

void LlmChatActivity::ensureWifiThenAsk() {
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) {
                           if (result.isCancelled || WiFi.status() != WL_CONNECTED) {
                             statusLine = tr(STR_WIFI_CONN_FAILED);
                             requestUpdate();
                             return;
                           }
                           statusLine = tr(STR_LLM_HINT);
                           requestUpdate();
                         });
}

void LlmChatActivity::promptForQuestion() {
  if (waiting) {
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    ensureWifiThenAsk();
    return;
  }
  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_LLM_ASK_PROMPT), "",
                                              kQuestionMaxLen, InputType::Text),
      [this](const ActivityResult& result) {
        if (result.isCancelled) {
          return;
        }
        const auto& keyboard = std::get<KeyboardResult>(result.data);
        if (keyboard.text.empty()) {
          return;
        }
        sendQuestion(keyboard.text);
      });
}

void LlmChatActivity::sendQuestion(const std::string& question) {
  waiting = true;
  statusLine = tr(STR_LLM_THINKING);
  turns.push_back(Turn{true, question});
  if (static_cast<int>(turns.size()) > kHistoryLimit) {
    turns.erase(turns.begin());
  }
  scrollLines = 0;
  requestUpdateAndWait();

  std::vector<LlmChatMessage> messages;
  messages.push_back(LlmChatMessage{
      "system", "You are Manifold on a small e-ink reader. Reply in short plain text. No markdown."});
  const int start = std::max(0, static_cast<int>(turns.size()) - 4);
  for (int i = start; i < static_cast<int>(turns.size()); ++i) {
    messages.push_back(LlmChatMessage{turns[i].fromUser ? "user" : "assistant", turns[i].text});
  }

  esp_task_wdt_reset();
  const LlmChatResult reply = Llm7Client::complete(messages);
  esp_task_wdt_reset();

  if (reply.ok) {
    turns.push_back(Turn{false, reply.text});
    if (static_cast<int>(turns.size()) > kHistoryLimit) {
      turns.erase(turns.begin());
    }
    statusLine = tr(STR_LLM_HINT);
  } else {
    statusLine = reply.error.empty() ? tr(STR_LLM_FAILED) : reply.error;
  }
  waiting = false;
  scrollLines = 0;
  requestUpdate();
}

std::vector<std::string> LlmChatActivity::buildVisibleLines(int maxWidth) const {
  std::vector<std::string> lines;
  for (const auto& turn : turns) {
    const std::string prefix = turn.fromUser ? "You: " : "AI: ";
    const std::string wrappedSource = prefix + turn.text;
    auto wrapped = BaseTheme::wrappedTextForRole(renderer, UI_10_FONT_ID, TextRole::UserContent,
                                                 wrappedSource.c_str(), maxWidth, 24);
    if (wrapped.empty()) {
      lines.push_back(wrappedSource);
    } else {
      lines.insert(lines.end(), wrapped.begin(), wrapped.end());
    }
    lines.emplace_back();
  }
  if (!lines.empty() && lines.back().empty()) {
    lines.pop_back();
  }
  return lines;
}

void LlmChatActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    activityManager.goHome();
    return;
  }
  if (!waiting && mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    promptForQuestion();
    return;
  }

  buttonNavigator.onNext([this] {
    scrollLines++;
    requestUpdate();
  });
  buttonNavigator.onPrevious([this] {
    if (scrollLines > 0) {
      scrollLines--;
    }
    requestUpdate();
  });
}

bool LlmChatActivity::onTouchTap(int16_t, int16_t y) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageHeight = renderer.getScreenHeight();
  if (y > pageHeight - metrics.buttonHintsHeight - 8) {
    promptForQuestion();
    return true;
  }
  if (y < metrics.headerHeight + metrics.topPadding + 40) {
    return false;
  }
  if (y < pageHeight / 2) {
    if (scrollLines > 0) {
      scrollLines--;
    }
  } else {
    scrollLines++;
  }
  requestUpdate();
  return true;
}

void LlmChatActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  renderer.clearScreen();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_LLM_CHAT));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentBottom = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int maxWidth = pageWidth - metrics.contentSidePadding * 2;
  const int lineHeight = BaseTheme::getLineHeightForRole(renderer, UI_10_FONT_ID, TextRole::UserContent);
  const int maxLines = std::max(1, (contentBottom - contentTop - lineHeight) / lineHeight);

  const auto lines = buildVisibleLines(maxWidth);
  int start = 0;
  if (static_cast<int>(lines.size()) > maxLines) {
    start = static_cast<int>(lines.size()) - maxLines;
  }
  start += scrollLines;
  if (start < 0) {
    start = 0;
  }
  if (!lines.empty() && start > static_cast<int>(lines.size()) - 1) {
    start = static_cast<int>(lines.size()) - 1;
  }

  int y = contentTop;
  if (lines.empty()) {
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, y, tr(STR_LLM_EMPTY));
  } else {
    for (int i = start; i < static_cast<int>(lines.size()) && y + lineHeight <= contentBottom - lineHeight; ++i) {
      if (!lines[i].empty()) {
        BaseTheme::drawTextForRole(renderer, UI_10_FONT_ID, TextRole::UserContent, metrics.contentSidePadding, y,
                                   lines[i].c_str());
      }
      y += lineHeight;
    }
  }

  const std::string status = renderer.truncatedText(SMALL_FONT_ID, statusLine.c_str(), maxWidth);
  renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding, contentBottom - lineHeight, status.c_str());

  const auto labels = mappedInput.mapLabels(tr(STR_HOME), waiting ? "" : tr(STR_LLM_ASK), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
