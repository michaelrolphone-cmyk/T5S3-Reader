#include "NativeSystemUiBridge.h"

#include <GfxRenderer.h>
#include <NativeAppLauncher.h>
#include <T5AppApi.h>

#include <cstring>
#include <memory>
#include <string>
#include <utility>

#include "MappedInputManager.h"
#include "NativeAppHost.h"
#include "activities/Activity.h"
#include "activities/ActivityManager.h"
#include "activities/RenderLock.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"

extern GfxRenderer renderer;
extern MappedInputManager mappedInputManager;

namespace {
struct KeyboardState {
  bool available = false;
  bool cancelled = false;
  uint64_t cookie = 0;
  std::string text;
};

KeyboardState keyboardState;
NativeSystemUiNavigation navigation = NativeSystemUiNavigation::None;

bool validResumePath(const char* path) {
  return path && std::strncmp(path, "/sd/", 4) == 0 && path[4] != '\0';
}

InputType mapInputType(uint8_t inputType) {
  switch (inputType) {
    case T5_SYSTEM_KEYBOARD_PASSWORD:
      return InputType::Password;
    case T5_SYSTEM_KEYBOARD_URL:
      return InputType::Url;
    case T5_SYSTEM_KEYBOARD_TEXT:
    default:
      return InputType::Text;
  }
}

class NativeKeyboardActivity final : public Activity {
  std::string resumePath;
  std::string title;
  std::string initialText;
  size_t maxLength;
  InputType inputType;
  uint64_t cookie;
  bool started = false;
  bool childCompleted = false;
  bool resumeReturned = false;

 public:
  NativeKeyboardActivity(GfxRenderer& gfx, MappedInputManager& input, std::string resume, std::string keyboardTitle,
                         std::string initial, size_t maximumLength, InputType type, uint64_t requestCookie)
      : Activity("NativeKeyboard", gfx, input),
        resumePath(std::move(resume)),
        title(std::move(keyboardTitle)),
        initialText(std::move(initial)),
        maxLength(maximumLength),
        inputType(type),
        cookie(requestCookie) {}

  void onEnter() override {
    Activity::onEnter();
    if (started) return;
    started = true;

    startActivityForResult(
        std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, title, initialText, maxLength, inputType),
        [this](const ActivityResult& result) {
          keyboardState.available = true;
          keyboardState.cancelled = result.isCancelled;
          keyboardState.cookie = cookie;
          keyboardState.text.clear();
          if (!result.isCancelled && std::holds_alternative<KeyboardResult>(result.data)) {
            keyboardState.text = std::get<KeyboardResult>(result.data).text;
          }
          childCompleted = true;
        });
  }

  void loop() override {
    if (resumeReturned) {
      finish();
      return;
    }
    if (!childCompleted) return;
    childCompleted = false;

    if (resumePath.empty()) {
      keyboardState = {};
      finish();
      return;
    }

    const esp_err_t result = runNativeApp(resumePath.c_str(), renderer, mappedInput);
    if (result != ESP_OK) {
      // The result cannot be consumed if the caller cannot be relaunched. Clear
      // it so one failed app cannot permanently block keyboard use by other ELFs.
      keyboardState = {};
      finish();
      return;
    }

    // If the relaunched ELF requests another system activity, let
    // ActivityManager process that pending push before this wrapper unwinds.
    resumeReturned = true;
  }

  void render(RenderLock&&) override {
    renderer.clearScreen();
    const auto& metrics = UITheme::getInstance().getMetrics();
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight},
                   title.c_str());
    renderer.displayBuffer(HalDisplay::BALANCED_REFRESH);
  }
};

bool keyboardRequest(const char* title, const char* initialText, size_t maxLength, uint8_t inputType, uint64_t cookie) {
  const char* currentPath = native_app_current_path();
  if (!validResumePath(currentPath)) return false;
  if (inputType > T5_SYSTEM_KEYBOARD_URL) return false;

  // Do not let a new request silently destroy an unread result from a prior
  // keyboard interaction.
  if (keyboardState.available || navigation != NativeSystemUiNavigation::None) return false;

  activityManager.pushActivity(std::make_unique<NativeKeyboardActivity>(
      renderer, mappedInputManager, std::string(currentPath), title ? title : "Enter Text",
      initialText ? initialText : "", maxLength, mapInputType(inputType), cookie));
  navigation = NativeSystemUiNavigation::Keyboard;
  return true;
}

bool keyboardTakeResult(char* text, size_t capacity, bool* cancelled, uint64_t* cookie) {
  if (!keyboardState.available) return false;
  if (text && capacity == 0) return false;

  if (text) {
    const size_t count = keyboardState.text.size() < capacity - 1 ? keyboardState.text.size() : capacity - 1;
    if (count > 0) std::memcpy(text, keyboardState.text.data(), count);
    text[count] = '\0';
  }
  if (cancelled) *cancelled = keyboardState.cancelled;
  if (cookie) *cookie = keyboardState.cookie;

  keyboardState = {};
  return true;
}

void navigateHome() {
  navigation = NativeSystemUiNavigation::Home;
  activityManager.goHome();
}

const t5_system_ui_api_v1 api = {
    T5_SYSTEM_UI_API_VERSION,
    sizeof(t5_system_ui_api_v1),
    keyboardRequest,
    keyboardTakeResult,
    navigateHome,
};
}  // namespace

extern "C" const t5_system_ui_api_v1* t5_system_ui_get_api(uint32_t version) {
  if (version != T5_SYSTEM_UI_API_VERSION || t5_app_get_api(T5_APP_ABI_VERSION) == nullptr) return nullptr;
  return &api;
}

void nativeSystemUiBegin() { navigation = NativeSystemUiNavigation::None; }
NativeSystemUiNavigation nativeSystemUiTakeNavigation() {
  const auto value = navigation;
  navigation = NativeSystemUiNavigation::None;
  return value;
}
