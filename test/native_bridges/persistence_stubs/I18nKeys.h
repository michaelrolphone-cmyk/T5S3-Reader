#pragma once
#include <cstdint>
enum class Language : uint8_t { EN = 0, ES = 1, FR = 2, DE = 3 };
enum class StrId : uint16_t { Dummy = 0 };
static constexpr uint8_t SORTED_LANGUAGE_INDICES[] = {0, 1, 2, 3};
