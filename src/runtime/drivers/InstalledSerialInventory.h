#pragma once

#include "InstalledProviderGraph.h"
#include "runtime/capabilities/SerialProviderDevices.h"
#include <RiscSerialPortV1.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>

// Firmware's generic capability owner consumes semantic inventory from every
// installed serial.port@1 ELF. It never enumerates USB, reads descriptors,
// opens a physical interface or substitutes a firmware driver. The same code
// accepts a fourth or later compatible class by manifest alone. All methods
// run on the serialized provider owner task; publication holds exact module
// grants until every announced device has been withdrawn and release checked.
namespace RuntimeInstalledProviders {
class InstalledSerialInventory final {
 public:
  enum class Result : uint8_t { Updated, Uncertain, Fault };
  static constexpr size_t kMaxProviders = RuntimeProviders::GraphV2::kMaxModules;
  explicit InstalledSerialInventory(RuntimeDevices::Registry& registry)
      : registry_(registry) {}
  InstalledSerialInventory(const InstalledSerialInventory&) = delete;
  InstalledSerialInventory& operator=(const InstalledSerialInventory&) = delete;

  Result refresh() {
    if (fault_) return Result::Fault;
    if (!prepare()) return Result::Fault;
    // Existing providers first: an identity fault must not be converted into
    // a detached device by discovery of a different installed package.
    for (Slot& slot : slots_) {
      if (!slot.occupied || !slot.lease.grant.slot) continue;
      if (!slot.devices || !valid(slot.lease.interface)) {
        fault_ = true;
        return Result::Fault;
      }
      const auto status = slot.devices->refresh(slot.id,
          static_cast<const risc_serial_port_api_v1*>(slot.lease.interface));
      if (status == RuntimeDevices::SerialProviderDevices::Result::Uncertain)
        return Result::Uncertain;
      if (status != RuntimeDevices::SerialProviderDevices::Result::Updated) {
        fault_ = true;
        return Result::Fault;
      }
    }
    size_t cursor = 0;
    for (;;) {
      char id[96]{};
      const auto next = nextProviderChecked("serial.port", RISC_SERIAL_PORT_API_V1,
                                           &cursor, id, sizeof(id));
      if (next == EnumerationResult::Exhausted) return Result::Updated;
      if (next == EnumerationResult::Fault) {
        fault_ = true;
        return Result::Fault;
      }
      bool already = false;
      for (const Slot& existing : slots_)
        if (existing.occupied && !std::strcmp(existing.id, id)) {
          already = true;
          break;
        }
      if (already) continue;
      Slot* slot = nullptr;
      for (Slot& candidate : slots_) if (!candidate.occupied) {
        slot = &candidate;
        break;
      }
      if (!slot) { fault_ = true; return Result::Fault; }
      std::memcpy(slot->id, id, std::strlen(id) + 1);
      slot->occupied = true;
      if (!acquire(id, "serial.port", RISC_SERIAL_PORT_API_V1, &slot->lease) ||
          !slot->lease.grant.slot || !slot->lease.interface) {
        // No grant does NOT prove the failed activation is quiescent. Retain
        // its exact installed ID for targeted recovery in stopChecked().
        slot->grantlessFault = !slot->lease.grant.slot;
        fault_ = true;
        return Result::Fault;
      }
      if (!valid(slot->lease.interface)) {
        // A legacy candidate lacking the required inventory extension cannot
        // publish devices. Checked release before considering another class.
        if (!release(&slot->lease)) {
          fault_ = true;
          return Result::Fault;
        }
        slot = nullptr;
        // The candidate has been verified quiescent, so it may be forgotten.
        // Reuse its reserved slot at a later enumeration without touching it.
        for (Slot& released : slots_)
          if (released.occupied && !std::strcmp(released.id, id)) {
            released = Slot{};
            break;
          }
        continue;
      }
      slot->devices.reset(new (std::nothrow) RuntimeDevices::SerialProviderDevices(registry_));
      if (!slot->devices) { fault_ = true; return Result::Fault; }
      const auto status = slot->devices->refresh(id,
          static_cast<const risc_serial_port_api_v1*>(slot->lease.interface));
      if (status == RuntimeDevices::SerialProviderDevices::Result::Uncertain)
        return Result::Uncertain;
      if (status != RuntimeDevices::SerialProviderDevices::Result::Updated) {
        fault_ = true;
        return Result::Fault;
      }
    }
  }

  // A registry handle by itself is NOT a hardware access grant. Its caller
  // must also hold and validate the proper execution-context capability lease.
  // The table pointer is only used by trusted firmware while this monitor's
  // exact provider grant is pinned; it must never be exported to an app ELF.
  bool resolve(RuntimeDevices::DeviceHandle handle,
               const risc_serial_port_api_v1** api,
               uint64_t* providerDevice, uint64_t* generation) const {
    if (api) *api = nullptr;
    if (providerDevice) *providerDevice = 0;
    if (generation) *generation = 0;
    if (!api || !providerDevice || !generation || !handle || fault_) return false;
    for (const Slot& slot : slots_) {
      if (!slot.occupied || !slot.devices || !slot.lease.grant.slot ||
          !valid(slot.lease.interface)) continue;
      if (!slot.devices->resolve(handle, providerDevice, generation)) continue;
      *api = static_cast<const risc_serial_port_api_v1*>(slot.lease.interface);
      return true;
    }
    return false;
  }

  bool attachEndpoint(RuntimeDevices::DeviceHandle device, uint32_t endpoint, uint32_t rights) const {
    if (fault_ || !device || !endpoint) return false;
    for (const Slot& slot : slots_) {
      uint64_t token = 0, generation = 0;
      if (slot.occupied && slot.devices && slot.lease.grant.slot &&
          slot.devices->resolve(device, &token, &generation))
        return attachStream(slot.lease, endpoint, rights);
    }
    return false;
  }

  // Revoke every published device before graph release. An uncertain physical
  // quiesce keeps the exact grant/ID for retry, not a silent alternative ELF.
  bool stopChecked() {
    bool ok = true;
    for (Slot& slot : slots_) {
      if (!slot.occupied) continue;
      if (slot.devices && !slot.devices->withdrawAll()) {
        ok = false;
        continue;
      }
      if (slot.lease.grant.slot) {
        if (!release(&slot.lease)) {
          ok = false;
          continue;
        }
      } else if (slot.grantlessFault &&
                 !recoverFailedProvider(slot.id, "serial.port",
                                        RISC_SERIAL_PORT_API_V1)) {
        ok = false;
        continue;
      }
      slot = Slot{};
    }
    // A partially stopped graph must not be rediscovered or rebound. Only
    // another stopChecked() may retry the exact retained provider generation.
    fault_ = !ok;
    return ok;
  }
  bool faulted() const { return fault_; }
  size_t retainedProviders() const {
    size_t n = 0;
    for (const Slot& slot : slots_) if (slot.occupied) ++n;
    return n;
  }
  size_t publishedDevices() const {
    size_t n = 0;
    for (const Slot& slot : slots_)
      if (slot.occupied && slot.devices) n += slot.devices->count();
    return n;
  }

 private:
  struct Slot {
    char id[96]{};
    Lease lease{};
    std::unique_ptr<RuntimeDevices::SerialProviderDevices> devices{};
    bool occupied = false;
    bool grantlessFault = false;
  };
  static bool valid(const void* interface) {
    if (!interface) return false;
    const auto* api = static_cast<const risc_serial_port_api_v1*>(interface);
    if (api->api_version != RISC_SERIAL_PORT_API_V1 ||
        api->struct_size < sizeof(risc_serial_port_inventory_v1) ||
        !api->open || !api->configure || !api->control_lines ||
        !api->read || !api->write || !api->close) return false;
    const auto* inventory = static_cast<const risc_serial_port_inventory_v1*>(interface);
    return inventory->discovery.probe && inventory->snapshot;
  }
  RuntimeDevices::Registry& registry_;
  Slot slots_[kMaxProviders]{};
  bool fault_ = false;
};
} // namespace RuntimeInstalledProviders
