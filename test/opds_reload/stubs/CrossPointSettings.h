#pragma once
struct SettingsFixture {
  char opdsServerUrl[128]{};
  char opdsUsername[128]{};
  char opdsPassword[128]{};
  int saves = 0;
  bool saveToFile() { ++saves; return true; }
};
extern SettingsFixture SETTINGS;
