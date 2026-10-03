#pragma once
namespace BoardPins {
constexpr int SdCs = 4;
}
namespace Board {
inline unsigned prepares = 0;
inline void prepareSdBus() { ++prepares; }
}  // namespace Board
