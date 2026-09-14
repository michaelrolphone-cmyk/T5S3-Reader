#include "Activity.h"

#include <cassert>

ActivityManager::ActivityManager(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : renderer(renderer), mappedInput(mappedInput), renderingMutex(xSemaphoreCreateMutex()) {
  assert(renderingMutex != nullptr && "Failed to create rendering mutex");
  stackActivities.reserve(10);
}

ActivityManager::~ActivityManager() { assert(false); }
