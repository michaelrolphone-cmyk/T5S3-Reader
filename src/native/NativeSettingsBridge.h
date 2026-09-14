#pragma once

#include <stddef.h>
#include <stdint.h>

#include <T5AppApi.h>

class GfxRenderer;
class MappedInputManager;

// Main-task native-app settings bridge. It deliberately exposes values and
// theme-rendered widgets rather than CrossPointSettings internals to ELF code.
void nativeSettingsBegin(GfxRenderer& renderer, MappedInputManager& input);
void nativeSettingsEnd();

uint32_t nativeSettingsCategoryCount();
bool nativeSettingsCategoryGet(uint32_t category, char* label, size_t capacity);
uint32_t nativeSettingsCount(uint32_t category);
bool nativeSettingsGet(uint32_t category, uint32_t index, t5_app_setting_t* setting);
uint8_t nativeSettingsActivate(uint32_t category, uint32_t index);
void nativeSettingsRender(uint32_t category, int32_t selectedIndex);
uint8_t nativeSettingsTouch(int16_t x, int16_t y, uint32_t* category, int32_t* selectedIndex);

// Complex settings screens remain firmware activities during migration. An
// ELF action requests one of them, then returns; the host dispatches it after
// the ELF loader has released framebuffer ownership.
void nativeSettingsDispatchPendingAction(GfxRenderer& renderer, MappedInputManager& input);
