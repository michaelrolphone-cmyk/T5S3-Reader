#pragma once
class CrossPointState;
namespace JsonSettingsIO {
bool saveState(const CrossPointState&, const char*);
bool loadState(CrossPointState&, const char*);
}
