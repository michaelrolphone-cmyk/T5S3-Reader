// Generic copied endpoint ingress for installed providers. Only firmware
// callbacks enter this table; no provider function pointer enters the registry.
#include "runtime/drivers/ProviderModuleV2.h"
#include "runtime/packages/PackageOrdinaryTransaction.h"
namespace {
constexpr unsigned kProviderContexts = 16, kProviderEndpoints = 4;
struct ProviderStreams {
  uint32_t owner = 0;
  bool stopped = false;
  t5_stream_t endpoints[kProviderEndpoints]{};
  t5_stream_t resources[kProviderEndpoints]{};
  RuntimePackages::Identity resourceIdentity{};
  char resourceRoot[96]{};
};
ProviderStreams providerStreams[kProviderContexts];
struct ProviderConsumerGrant {
  uint64_t context = 0, lease = 0;
  uint32_t consumer = 0, endpoint = 0, rights = 0;
};
ProviderConsumerGrant providerConsumerGrants[32];
void forgetProviderConsumer(uint32_t consumer) {
  for (auto& grant : providerConsumerGrants)
    if (grant.consumer == consumer) grant = {};
}
ProviderStreams* providerContext(uint64_t context) {
  if (!context || context > UINT32_MAX) return nullptr;
  for (auto& slot : providerStreams)
    if (slot.owner == context && !slot.stopped) return &slot;
  return nullptr;
}
int32_t providerEndpoint(uint64_t context, t5_stream_t handle, uint32_t right,
                         ProviderStreams** slot) {
  *slot = providerContext(context);
  if (!*slot) return T5_STREAM_DENIED;
  bool owned = false;
  for (const auto endpoint : (*slot)->endpoints) owned |= endpoint && endpoint == handle;
  if (!owned) return T5_STREAM_INVALID;
  t5_stream_info_t info{}; info.struct_size = sizeof(info);
  const auto result = registry.info((*slot)->owner, handle, &info);
  if (result != T5_STREAM_OK) return result;
  return (info.flags & right) == right ? T5_STREAM_OK : T5_STREAM_DENIED;
}
int32_t providerPublish(uint64_t context, const risc_stream_endpoint_v1* spec, uint32_t* out) {
  if (out) *out = 0;
  if (!mutex) return T5_STREAM_DENIED;
  Lock lock;
  auto* slot = providerContext(context);
  if (!slot) return T5_STREAM_DENIED;
  if (!spec || spec->struct_size < sizeof(*spec) || !out || !spec->rights ||
      (spec->rights & ~(T5_STREAM_READ | T5_STREAM_WRITE))) return T5_STREAM_INVALID;
  for (auto& handle : slot->endpoints) {
    if (handle) continue;
    const auto result = registry.publishEndpoint(slot->owner, spec->kind, spec->rights,
        spec->byte_capacity, spec->schema, spec->max_record, spec->record_capacity,
        RuntimeStreams::kStreamPublic, out);
    if (result == T5_STREAM_OK) handle = *out;
    wake(); return result;
  }
  return T5_STREAM_LIMIT;
}
int32_t providerProduce(uint64_t context, uint32_t h, const void* data, uint32_t n, uint32_t* out) {
  if (out) *out = 0;
  if (!mutex) return T5_STREAM_DENIED;
  Lock lock; ProviderStreams* slot = nullptr;
  const auto valid = providerEndpoint(context, h, T5_STREAM_READ, &slot);
  if (valid != T5_STREAM_OK) return valid;
  const auto result = registry.produce(slot->owner, h, data, n, out);
  wake(); return result;
}
int32_t providerConsume(uint64_t context, uint32_t h, void* data, uint32_t n, uint32_t* out) {
  if (out) *out = 0;
  if (!mutex) return T5_STREAM_DENIED;
  Lock lock; ProviderStreams* slot = nullptr;
  const auto valid = providerEndpoint(context, h, T5_STREAM_WRITE, &slot);
  if (valid != T5_STREAM_OK) return valid;
  const auto result = registry.consume(slot->owner, h, data, n, out);
  wake(); return result;
}
int32_t providerProduceRecord(uint64_t context, uint32_t h, const void* data, uint32_t n) {
  if (!mutex) return T5_STREAM_DENIED;
  Lock lock; ProviderStreams* slot = nullptr;
  const auto valid = providerEndpoint(context, h, T5_STREAM_READ, &slot);
  if (valid != T5_STREAM_OK) return valid;
  const auto result = registry.produceRecord(slot->owner, h, data, n);
  wake(); return result;
}
int32_t providerConsumeRecord(uint64_t context, uint32_t h, void* data, uint32_t n, uint32_t* out) {
  if (out) *out = 0;
  if (!mutex) return T5_STREAM_DENIED;
  Lock lock; ProviderStreams* slot = nullptr;
  const auto valid = providerEndpoint(context, h, T5_STREAM_WRITE, &slot);
  if (valid != T5_STREAM_OK) return valid;
  const auto result = registry.consumeRecord(slot->owner, h, data, n, out);
  wake(); return result;
}
int32_t providerFinish(uint64_t context, uint32_t h, int32_t terminal) {
  if (!mutex) return T5_STREAM_DENIED;
  Lock lock; ProviderStreams* slot = nullptr;
  const auto valid = providerEndpoint(context, h, 0, &slot);
  if (valid != T5_STREAM_OK) return valid;
  const auto result = terminal < 0 ? registry.failEndpoint(slot->owner, h, terminal)
                                  : registry.finish(slot->owner, h, terminal);
  wake(); return result;
}
bool providerResource(ProviderStreams* slot, uint32_t h) {
  if (!slot || !h) return false;
  for (const auto resource : slot->resources) if (resource == h) return true;
  return false;
}
int32_t providerClose(uint64_t context, uint32_t h) {
  if (!mutex) return T5_STREAM_DENIED;
  RetiredProvider retired;
  Lock lock; ProviderStreams* slot = providerContext(context);
  if (!slot) return T5_STREAM_DENIED;
  if (!providerResource(slot, h)) {
    const auto valid = providerEndpoint(context, h, 0, &slot);
    if (valid != T5_STREAM_OK) return valid;
  }
  const auto result = registry.close(slot->owner, h, &retired.value);
  if (result == T5_STREAM_OK) {
    for (auto& endpoint : slot->endpoints) if (endpoint == h) endpoint = 0;
    for (auto& resource : slot->resources) if (resource == h) resource = 0;
    for (auto& grant : providerConsumerGrants)
      if (grant.context == context && grant.endpoint == h) grant = {};
  }
  wake(); return result;
}
int32_t providerOpenResource(uint64_t context, const char* name, uint32_t* out) {
  if (out) *out = 0;
  if (!mutex) return T5_STREAM_DENIED;
  if (!out || !RuntimePackages::safePackageResourcePath(name)) return T5_STREAM_INVALID;
  char root[96]{}, relative[128]{};
  RuntimePackages::Identity expected{};
  {
    Lock lock; auto* slot = providerContext(context);
    if (!slot || !slot->resourceRoot[0]) return T5_STREAM_DENIED;
    bool room = false;
    for (const auto h : slot->resources) if (!h) room = true;
    if (!room) return T5_STREAM_LIMIT;
    std::strcpy(root, slot->resourceRoot); std::strcpy(relative, name);
    expected = slot->resourceIdentity;
  }
  std::unique_ptr<File> candidate;
  const int32_t prepared = prepareResource(root, expected, relative, candidate);
  if (prepared != T5_STREAM_OK) return prepared;
  RuntimeStreams::Provider provider{candidate.get(), fileRead, nullptr, fileSeek, fileFinish, fileClose};
  Lock lock; auto* slot = providerContext(context);
  if (!slot) return T5_STREAM_DENIED;
  for (auto& h : slot->resources) {
    if (h) continue;
    const auto result = registry.attach(slot->owner, T5_STREAM_BYTES,
        T5_STREAM_READ | T5_STREAM_SEEK, provider, out);
    if (result == T5_STREAM_OK) { h = *out; (void)candidate.release(); }
    return result;
  }
  return T5_STREAM_LIMIT;
}
int32_t providerOpenImport(uint64_t context, uint32_t index, const char* name, uint32_t* out) {
  if (out) *out = 0;
  if (!mutex) return T5_STREAM_DENIED;
  if (!out || !RuntimePackages::safePackageResourcePath(name)) return T5_STREAM_INVALID;
  char root[96]{}, relative[128]{};
  RuntimePackages::Identity expected{};
  {
    Lock lock; auto* slot = providerContext(context);
    if (!slot || !slot->resourceRoot[0]) return T5_STREAM_DENIED;
    std::strcpy(root, slot->resourceRoot); std::strcpy(relative, name); expected = slot->resourceIdentity;
  }
  std::unique_ptr<File> candidate;
  const auto prepared = prepareResourceImport(root, expected, index, relative, candidate);
  if (prepared != T5_STREAM_OK) return prepared;
  RuntimeStreams::Provider provider{candidate.get(), fileRead, nullptr, fileSeek, fileFinish, fileClose};
  Lock lock; auto* slot = providerContext(context);
  if (!slot || std::strcmp(root, slot->resourceRoot)) return T5_STREAM_DENIED;
  for (auto& h : slot->resources) {
    if (h) continue;
    const auto result = registry.attach(slot->owner, T5_STREAM_BYTES, T5_STREAM_READ | T5_STREAM_SEEK, provider, out);
    if (result == T5_STREAM_OK) { h = *out; (void)candidate.release(); }
    return result;
  }
  return T5_STREAM_LIMIT;
}
int32_t providerReadResource(uint64_t context, uint32_t h, void* data,
                             uint32_t capacity, uint32_t* count) {
  if (count) *count = 0;
  if (!mutex) return T5_STREAM_DENIED;
  RuntimeStreams::DirectIo operation{};
  int32_t result;
  {
    Lock lock; auto* slot = providerContext(context);
    if (!slot) return T5_STREAM_DENIED;
    if (!providerResource(slot, h)) return T5_STREAM_INVALID;
    result = registry.read(slot->owner, h, data, capacity, count, &operation);
  }
  if (operation.transfer.pending) result = executeDirect(operation, data, count);
  return result;
}
int32_t providerSeekResource(uint64_t context, uint32_t h, uint64_t offset) {
  if (!mutex) return T5_STREAM_DENIED;
  RuntimeStreams::DirectIo operation{};
  int32_t result;
  {
    Lock lock; auto* slot = providerContext(context);
    if (!slot) return T5_STREAM_DENIED;
    if (!providerResource(slot, h)) return T5_STREAM_INVALID;
    result = registry.seek(slot->owner, h, offset, &operation);
  }
  if (operation.transfer.pending) result = executeDirect(operation);
  return result;
}
bool providerOpenContext(risc_stream_provider_v1* out) {
  if (!out) return false;
  *out = {};
  // Loader-owned initialization and ID allocation run on the owner task.
  if (!initialize()) return false;
  Lock lock;
  for (auto& slot : providerStreams) {
    if (slot.owner) continue;
    const uint32_t id = RuntimeResources::ExecutionContext::reserveIdentity();
    if (!id) return false;
    slot = {}; slot.owner = id;
    *out = {RISC_STREAM_PROVIDER_API_V1, sizeof(*out), id, providerPublish,
            providerProduce, providerConsume, providerProduceRecord,
            providerConsumeRecord, providerFinish, providerClose};
    return true;
  }
  return false;
}
void providerRevokeContext(uint64_t context) {
  if (!mutex) return;
  RetiredProviders retired;
  Lock lock;
  auto* slot = providerContext(context);
  if (!slot) return;
  slot->stopped = true;
  for (auto& grant : providerConsumerGrants)
    if (grant.context == context) grant = {};
  // In-flight resource I/O retains its adapter ticket; other file adapters
  // retire only after unlocking. Endpoint and resource handles both revoke.
  registry.release(slot->owner, &retired.values);
  wake();
}
void providerCloseContext(uint64_t context) {
  providerRevokeContext(context);
  if (!mutex) return;
  Lock lock;
  for (auto& slot : providerStreams)
    if (slot.owner && slot.owner == context) { slot = {}; return; }
}
bool providerOpenResources(risc_stream_provider_resources_v1* out,
                           const RuntimePackages::Identity& identity) {
  using namespace RuntimePackages;
  if (!out) return false;
  *out = {};
  Identity canonical{};
  OrdinaryTransactionPaths paths{};
  if (identity.kind == Kind::Application || identity.payload != Payload::Executable || identity.legacyVersion ||
      !makeIdentity(identity.kind, identity.id, identity.version, identity.artifact, false, &canonical) ||
      !ordinaryTransactionPaths(identity.kind, identity.id, paths) ||
      !systemPackageUseGate().pinned(paths.target) || !providerOpenContext(&out->streams)) return false;
  Lock lock; auto* slot = providerContext(out->streams.context);
  if (!slot) return false;
  slot->resourceIdentity = identity;
  std::strcpy(slot->resourceRoot, paths.target);
  out->streams.struct_size = sizeof(*out);
  out->open_resource = providerOpenResource;
  out->read_resource = providerReadResource;
  out->seek_resource = providerSeekResource;
  out->open_import = providerOpenImport;
  return true;
}
bool providerGrant(uint64_t context, uint64_t lease, uint32_t consumer,
                   uint32_t endpoint, uint32_t rights) {
  if (!mutex || !lease || !consumer || consumer == context || !rights ||
      (rights & ~(T5_STREAM_READ | T5_STREAM_WRITE))) return false;
  Lock lock; ProviderStreams* slot = nullptr;
  if (providerEndpoint(context, endpoint, rights, &slot) != T5_STREAM_OK) return false;
  ProviderConsumerGrant* target = nullptr;
  for (auto& grant : providerConsumerGrants) {
    if (grant.context == context && grant.lease == lease &&
        grant.consumer == consumer && grant.endpoint == endpoint) { target = &grant; break; }
    if (!grant.context && !target) target = &grant;
  }
  if (!target) return false;
  uint32_t combined = rights;
  for (const auto& grant : providerConsumerGrants)
    if (&grant != target && grant.context == context && grant.consumer == consumer &&
        grant.endpoint == endpoint) combined |= grant.rights;
  if (registry.grant(slot->owner, endpoint, consumer, combined) != T5_STREAM_OK) return false;
  *target = {context, lease, consumer, endpoint, rights};
  wake(); return true;
}
void providerRevokeGrant(uint64_t context, uint64_t lease) {
  if (!mutex) return;
  Lock lock;
  for (auto& grant : providerConsumerGrants) {
    if (grant.context != context || grant.lease != lease) continue;
    const auto revoked = grant; grant = {};
    uint32_t remaining = 0;
    for (const auto& other : providerConsumerGrants)
      if (other.context == context && other.consumer == revoked.consumer &&
          other.endpoint == revoked.endpoint) remaining |= other.rights;
    if (!remaining || registry.grant(static_cast<uint32_t>(context), revoked.endpoint,
                                     revoked.consumer, remaining) != T5_STREAM_OK)
      (void)registry.revoke(static_cast<uint32_t>(context), revoked.endpoint, revoked.consumer);
  }
  wake();
}
const RuntimeProviders::StreamHostV1 providerHost{
    providerOpenContext, providerRevokeContext, providerCloseContext,
    providerGrant, providerRevokeGrant, providerOpenResources};
} // namespace
const RuntimeProviders::StreamHostV1* nativeProviderStreamHost() { return &providerHost; }

uint32_t nativeProviderStreamConsumer() { return authorized() ? owner : 0; }
