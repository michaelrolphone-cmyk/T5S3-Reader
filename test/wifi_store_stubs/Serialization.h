#pragma once
#include <string>
#include "HalStorage.h"
namespace serialization {
template<class T> void readPod(FsFile&, T& value) { value=0; }
inline void readString(FsFile&, std::string& value) { value.clear(); }
}
