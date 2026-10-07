#pragma once
#include "NativeReaderEntryState.h"
class GfxRenderer;
class MappedInputManager;
class Activity;
namespace NativeReaderEntry {
// Firmware-only entry, on the existing owner task. No second UI/task/session.
bool run(const char* path, GfxRenderer& renderer, MappedInputManager& input,
         void (*start)(), void (*loop)(), void (*sleep)(Action, bool));
bool mapped();
bool blocked();
bool pending();
bool deferActivity(Activity* activity);
bool deferSleep(Action action, bool wakeOnTouch = true);
bool consumeResume();
}  // namespace NativeReaderEntry
