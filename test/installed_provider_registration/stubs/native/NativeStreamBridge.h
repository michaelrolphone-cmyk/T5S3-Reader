#pragma once
#include "runtime/drivers/ProviderGraphV2.h"
// No provider is activated by this registration test, so no stream host or
// owner poll hook is needed. Graph/module/executor implementations remain real.
inline const RuntimeProviders::StreamHostV1* nativeProviderStreamHost() { return nullptr; }
inline void nativeProviderSetOwnerPoll(void (*)()) {}
inline uint32_t nativeProviderStreamConsumer() { return 1; }
