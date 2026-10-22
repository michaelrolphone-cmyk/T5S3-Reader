#pragma once

namespace SleepOrchestration {

// Internal orchestration, not a driver ABI. The caller owns these callbacks
// and their context for the synchronous call; no allocation or retained state.
// Application/UI preparation must finish before invoking this phase.
struct Operations {
  void* context;
  bool (*powerDownDisplay)(void*);
  void (*resumeAfterRefusal)(void*);
};

// Supply only for explicit shutdown. These operations delegate to existing
// board/provider owners; the helper does not own storage, rails or PMU policy.
struct ShutdownOperations {
  bool (*reserve)(void*);
  bool (*cancel)(void*);
  bool (*drain)(void*);
  bool (*available)(void*);
  bool (*powerOff)(void*);
  void (*returned)(void*, bool accepted);
  void (*unavailable)(void*);
};

// A true result has crossed the existing display/storage one-way barrier.
// Continue directly to the caller's terminal sleep fallback: do not remount,
// save files or re-enter ordinary preparation. False means preparation refused.
// Missing callbacks fail closed before any callback or recovery is invoked.
bool prepare(const Operations& operations, const ShutdownOperations* shutdown = nullptr);

}  // namespace SleepOrchestration
