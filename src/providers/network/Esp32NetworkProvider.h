#pragma once
#include "runtime/network/NetworkService.h"

namespace Esp32NetworkProvider {
const RuntimeNetwork::InterfaceApi& interfaceApi();
const RuntimeNetwork::WifiControlApi& wifiControlApi();
}  // namespace Esp32NetworkProvider
