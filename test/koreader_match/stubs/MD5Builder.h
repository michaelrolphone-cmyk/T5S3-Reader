#pragma once
#include "HalStorage.h"
struct MD5Builder { void begin(){} void add(const char*){} void calculate(){} String toString()const{return String{"fixture-md5"};}};
