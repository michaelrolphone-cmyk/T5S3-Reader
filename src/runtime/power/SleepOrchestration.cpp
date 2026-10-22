#include "SleepOrchestration.h"

namespace SleepOrchestration {

bool prepare(const Operations& operations, const ShutdownOperations* shutdown) {
  if (!operations.powerDownDisplay || !operations.resumeAfterRefusal) return false;
  if (shutdown && (!shutdown->reserve || !shutdown->cancel || !shutdown->drain ||
                   !shutdown->available || !shutdown->powerOff || !shutdown->returned ||
                   !shutdown->unavailable)) return false;

  void* const context = operations.context;
  const bool reserved = !shutdown || shutdown->reserve(context);
  if (!reserved && (!shutdown->cancel(context) || !shutdown->drain(context))) {
    operations.resumeAfterRefusal(context);
    return false;
  }

  if (!operations.powerDownDisplay(context)) {
    if (shutdown) (void)shutdown->cancel(context);
    operations.resumeAfterRefusal(context);
    return false;
  }

  // Display/storage teardown must precede the PMU request. A returned shutdown
  // (accepted or refused) uses the same terminal fallback, never preparation.
  if (shutdown) {
    if (reserved && shutdown->available(context)) {
      shutdown->returned(context, shutdown->powerOff(context));
    } else {
      shutdown->unavailable(context);
    }
  }
  return true;
}

}  // namespace SleepOrchestration
