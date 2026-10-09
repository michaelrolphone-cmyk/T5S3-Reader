#pragma once
#include <esp_err.h>
class GfxRenderer;
class MappedInputManager;
esp_err_t runNativeReaderEntry(const char*, GfxRenderer&, MappedInputManager&);
