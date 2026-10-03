#pragma once
#include "I18nKeys.h"
class I18n {
 public:
  static I18n& getInstance() { static I18n instance; return instance; }
  Language getLanguage() const { return language_; }
  void setLanguage(Language language) { language_ = language; }
  const char* getLanguageName(Language language) const {
    switch (language) {
      case Language::EN: return "English";
      case Language::ES: return "Spanish";
      case Language::FR: return "French";
      case Language::DE: return "German";
    }
    return "";
  }
 private:
  Language language_ = Language::EN;
};
#define I18N I18n::getInstance()
