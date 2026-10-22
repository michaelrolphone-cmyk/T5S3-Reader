#pragma once
#include <Arduino.h>
inline unsigned loads=0;
inline uint32_t loadTime=0;
inline void (*afterLoad)()=nullptr;
inline void ensureSdFontLoaded() {
    ++loads;
    readerClock+=loadTime;
    if(afterLoad) afterLoad();
}
