#pragma once
class OpdsServerStore;
namespace JsonSettingsIO {
bool loadOpds(OpdsServerStore&, const char*, bool*);
bool saveOpds(const OpdsServerStore&, const char*);
}
