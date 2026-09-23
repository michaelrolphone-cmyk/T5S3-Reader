#pragma once
#include "runtime/drivers/ProviderModuleV2.h"
// Compatibility-prefix test only. Queue behavior uses the real registry in
// usb_witness_provider_graph_test; accidental queue use here fails closed.
static bool openStreams(risc_stream_provider_v1* out) {
  *out = {};
  out->api_version = 1; out->struct_size = sizeof(*out); out->context = 1;
  out->publish = [](uint64_t, const risc_stream_endpoint_v1*, uint32_t*) -> int32_t { return -1; };
  out->produce = [](uint64_t, uint32_t, const void*, uint32_t, uint32_t*) -> int32_t { return -1; };
  out->consume = [](uint64_t, uint32_t, void*, uint32_t, uint32_t*) -> int32_t { return -1; };
  out->finish = [](uint64_t, uint32_t, int32_t) -> int32_t { return -1; };
  out->close = [](uint64_t, uint32_t) -> int32_t { return -1; };
  return true;
}
static const RuntimeProviders::StreamHostV1 streamHost = {
  openStreams, [](uint64_t) {}, [](uint64_t) {}, nullptr, nullptr
};
