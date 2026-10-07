#pragma once
#include <cstddef>

namespace RuntimeMemory { class PsramGrowingTextStream; }
namespace HttpDownloader {
inline bool gFetchUrlReturns = false;
inline bool fetchUrl(const char*, RuntimeMemory::PsramGrowingTextStream&) { return gFetchUrlReturns; }
}
