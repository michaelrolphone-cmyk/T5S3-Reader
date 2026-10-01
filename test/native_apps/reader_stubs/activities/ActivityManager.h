#pragma once
#include <GfxRenderer.h>
struct ActivityManager {
    GfxRenderer renderer;
    GfxRenderer& nativeAppRenderer(){ return renderer; }
};
inline ActivityManager activityManager;
