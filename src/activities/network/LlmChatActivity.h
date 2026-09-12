#pragma once

#include <string>
#include <vector>

#include "../Activity.h"
#include "util/ButtonNavigator.h"

class LlmChatActivity final : public Activity {
  struct Turn {
    bool fromUser;
    std::string text;
  };

  ButtonNavigator buttonNavigator;
  std::vector<Turn> turns;
  std::string statusLine;
  bool waiting = false;
  bool wifiPrompted = false;
  int scrollLines = 0;

  void ensureWifiThenAsk();
  void promptForQuestion();
  void sendQuestion(const std::string& question);
  std::vector<std::string> buildVisibleLines(int maxWidth) const;

 public:
  explicit LlmChatActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("LlmChat", renderer, mappedInput) {}
  void onEnter() override;
  void loop() override;
  bool onTouchTap(int16_t x, int16_t y) override;
  bool preventAutoSleep() override { return waiting; }
  void render(RenderLock&&) override;
};
