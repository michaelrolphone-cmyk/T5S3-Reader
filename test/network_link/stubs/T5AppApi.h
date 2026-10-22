#pragma once
#include "NetworkTestSupport.h"
#define T5_APP_ABI_VERSION 1u
inline const void* t5_app_get_api(uint32_t) { return Fixture::appActive ? &Fixture::appActive : nullptr; }
