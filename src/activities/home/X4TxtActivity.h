#pragma once

#if defined(BOARD_XTEINK_X4_PRO)

#include <HalStorage.h>

#include "../Activity.h"
#include "X4TxtSession.h"

class X4TxtActivity final : public Activity {
  class StorageSource final : public X4TxtSession::Source {
    HalFile file_;
    bool opened_ = false;

   public:
    bool available() const override;
    bool list(const char* directory, std::vector<std::string>& names) override;
    bool open(const std::string& path, size_t& bytes) override;
    bool readAt(size_t offset, uint8_t* output, size_t count) override;
    bool close() override;
  } source_;
  X4TxtSession session_;

 public:
  explicit X4TxtActivity(GfxRenderer& renderer, MappedInputManager& input)
      : Activity("X4Txt", renderer, input) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool supportsGlobalMenu() const override { return false; }
  void render(RenderLock&& lock) override;
};

#endif
