#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32) || defined(RISCRTE_TEST_INSTALLED_SERIAL_PATH)
#define RISCRTE_INSTALLED_SERIAL_PATH 1
#else
#define RISCRTE_INSTALLED_SERIAL_PATH 0
#endif

#include "NativeSerialPortBridge.h"
#include "NativeStreamBridge.h"
#include "runtime/capabilities/SerialProviderRegistry.h"
#include "runtime/resources/ExecutionContext.h"
#if RISCRTE_INSTALLED_SERIAL_PATH
#include "runtime/drivers/InstalledSerialInventory.h"
#include "runtime/drivers/InstalledSerialSession.h"
#include <RiscSerialPortV1.h>
#else
#include "NativeUsbDeviceRegistry.h"
#include "NativeUsbClassBridge.h"
#include "runtime/capabilities/UsbSerialProjection.h"
#include <T5UsbApi.h>
#endif
#include <T5AppApi.h>
#include <T5SerialPortApi.h>
#include <cstdio>
#include <cstring>
#include <atomic>
#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <Logging.h>
#endif

namespace {
constexpr uint32_t kMaxHandleGeneration = 0x7fffffffu;
bool active = false;
t5_serial_port_lease_t leaseHandle = 0;
t5_stream_t rxHandle = 0;
t5_stream_t txHandle = 0;
uint32_t leaseGeneration = 0;
uint32_t leaseEpoch = 0;
#if !RISCRTE_INSTALLED_SERIAL_PATH
// Non-production fixture compatibility only.
NativeUsbDevices::Registry devices;
RuntimeDevices::UsbSerialProjection usbProjection(RuntimeDevices::systemRegistry());
#else
RuntimeInstalledProviders::InstalledSerialInventory installedSerial(RuntimeDevices::systemRegistry());
RuntimeInstalledProviders::InstalledSerialSession installedSession;
RuntimeDevices::DeviceHandle activeSemanticDevice = 0;
const risc_serial_port_api_v1* activeInstalledApi = nullptr;
uint64_t activeProviderDevice = 0;
uint64_t activeProviderGeneration = 0;
std::atomic<uint32_t> activeStreamEpoch{0};
std::atomic<uint32_t> serialIoInFlight{0};
uint32_t nextStreamEpoch = 0;
#endif
RuntimeDevices::LeaseHandle physicalLease = 0;
uint32_t physicalOwner = 0;
enum class SerialConsumer : uint8_t { None, SerialPort, LegacyDirectStream };
SerialConsumer physicalConsumer = SerialConsumer::None;
RuntimeSerial::Registry providers;
t5_serial_config_t currentConfig = {115200u, 8u, T5_SERIAL_PARITY_NONE, 1u, T5_SERIAL_FLOW_NONE};

t5_serial_port_lease_t lastLoggedLease = 0;
uint8_t lastLoggedStatus = 0xff;
uint8_t lastLoggedConnected = 0xff;
int32_t lastLoggedError = 0;
t5_serial_device_t lastLoggedDevice = 0;
void resetStatusTrace() {
  lastLoggedLease = 0;
  lastLoggedStatus = 0xff;
  lastLoggedConnected = 0xff;
  lastLoggedError = 0;
  lastLoggedDevice = 0;
}
void traceResult(const char* action, t5_serial_result_t result,
                 t5_serial_port_lease_t lease, t5_serial_device_t device) {
#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
  LOG_INF("SERIAL", "SERREF action=%s result=%d lease=%lu device=%lu",
          action, static_cast<int>(result), static_cast<unsigned long>(lease),
          static_cast<unsigned long>(device));
#else
  (void)action; (void)result; (void)lease; (void)device;
#endif
}
void traceStatus(t5_serial_port_lease_t lease, const t5_serial_port_state_t& status) {
  if (lastLoggedLease == lease && lastLoggedStatus == status.status &&
      lastLoggedConnected == status.connected && lastLoggedError == status.last_error &&
      lastLoggedDevice == status.device) return;
  lastLoggedLease = lease;
  lastLoggedStatus = status.status;
  lastLoggedConnected = status.connected;
  lastLoggedError = status.last_error;
  lastLoggedDevice = status.device;
#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
  LOG_INF("SERIAL", "SERREF action=status lease=%lu device=%lu status=%u connected=%u error=%ld rx=%lu tx=%lu dropped=%lu",
          static_cast<unsigned long>(lease), static_cast<unsigned long>(status.device),
          static_cast<unsigned>(status.status), static_cast<unsigned>(status.connected),
          static_cast<long>(status.last_error), static_cast<unsigned long>(status.rx_bytes),
          static_cast<unsigned long>(status.tx_bytes),
          static_cast<unsigned long>(status.dropped_rx_bytes));
#else
  (void)lease; (void)status;
#endif
}

bool authorized() {
  return active && t5_app_get_api(T5_APP_ABI_VERSION) != nullptr;
}
void synchronizeSerialDevices() {
#if RISCRTE_INSTALLED_SERIAL_PATH
  const auto discovery = installedSerial.refresh();
  if (activeSemanticDevice &&
      discovery != RuntimeInstalledProviders::InstalledSerialInventory::Result::Uncertain) {
    const risc_serial_port_api_v1* api = nullptr;
    uint64_t providerDevice = 0, generation = 0;
    if (discovery == RuntimeInstalledProviders::InstalledSerialInventory::Result::Fault ||
        !installedSerial.resolve(activeSemanticDevice, &api, &providerDevice, &generation) ||
        api != activeInstalledApi || providerDevice != activeProviderDevice ||
        generation != activeProviderGeneration)
      activeStreamEpoch.store(0, std::memory_order_release);
  }
#else
  const auto snapshot = devices.diagnostics();
  const auto& d = snapshot.device;
  (void)usbProjection.reconcile(snapshot.epoch, d.id,
      d.presence == NativeUsbDevices::Presence::Bound,
      d.vid, d.pid, d.interface_number, d.product);
#endif
  if (physicalLease && !RuntimeDevices::systemRegistry().valid(physicalLease, physicalOwner))
    physicalLease = 0;
}
void releasePhysicalDevice(SerialConsumer consumer) {
  if (physicalConsumer != consumer) return;
  if (physicalLease) {
    (void)RuntimeDevices::systemRegistry().release(physicalLease, physicalOwner);
    physicalLease = 0;
  }
  physicalOwner = 0;
  physicalConsumer = SerialConsumer::None;
}
#if !RISCRTE_INSTALLED_SERIAL_PATH
bool claimPhysicalDevice(SerialConsumer consumer) {
  synchronizeSerialDevices();
  auto* context = RuntimeResources::ExecutionContext::current();
  if (!context || !context->running(context->id())) return false;
  if (physicalConsumer != SerialConsumer::None &&
      (physicalConsumer != consumer || physicalOwner != context->id())) return false;
  if (physicalLease) return RuntimeDevices::systemRegistry().valid(physicalLease, context->id());
  if (!usbProjection.device()) {
    physicalConsumer = consumer;
    physicalOwner = context->id();
    return true;  // Enumeration remains asynchronous.
  }
  RuntimeDevices::LeaseHandle next = 0;
  if (RuntimeDevices::systemRegistry().acquire(
          "serial.port", context->id(), &next, usbProjection.device(),
          RuntimeDevices::Mode::Exclusive) != RuntimeDevices::Result::Ok) return false;
  physicalLease = next;
  physicalConsumer = consumer;
  physicalOwner = context->id();
  return true;
}
#endif
bool validConfig(const t5_serial_config_t* config) {
  return config && config->baud_rate >= 300u && config->baud_rate <= 3000000u &&
         config->data_bits >= 5u && config->data_bits <= 8u &&
         config->parity <= T5_SERIAL_PARITY_SPACE &&
         (config->stop_bits == 1u || config->stop_bits == 2u) &&
         config->flow_control == T5_SERIAL_FLOW_NONE;
}
#if !RISCRTE_INSTALLED_SERIAL_PATH
uint8_t semanticStatus(uint8_t status) {
  switch (status) {
    case T5_USB_STATUS_OFF: return T5_SERIAL_STATUS_OFF;
    case T5_USB_STATUS_WAITING: return T5_SERIAL_STATUS_WAITING;
    case T5_USB_STATUS_CONFIGURING: return T5_SERIAL_STATUS_CONFIGURING;
    case T5_USB_STATUS_READY: return T5_SERIAL_STATUS_READY;
    case T5_USB_STATUS_ERROR: return T5_SERIAL_STATUS_ERROR;
    default: return T5_SERIAL_STATUS_UNAVAILABLE;
  }
}
#endif
bool validLease(t5_serial_port_lease_t lease) {
  return lease != 0 && lease == leaseHandle;
}
#if RISCRTE_INSTALLED_SERIAL_PATH
bool installedLeaseRevoked() {
  if (!physicalLease || !physicalOwner || !activeSemanticDevice ||
      !activeInstalledApi || !activeProviderDevice || !activeProviderGeneration)
    return true;
  if (!RuntimeDevices::systemRegistry().valid(physicalLease, physicalOwner))
    return true;
  const risc_serial_port_api_v1* api = nullptr;
  uint64_t providerDevice = 0, generation = 0;
  if (!installedSerial.resolve(activeSemanticDevice, &api, &providerDevice, &generation))
    return true;
  return api != installedSession.api() ||
         providerDevice != installedSession.providerDevice() ||
         generation != installedSession.providerGeneration();
}

bool waitInstalledSerialIoIdle() {
  if (!serialIoInFlight.load(std::memory_order_acquire)) return true;
#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
  // Provider transfers use a 1 ms timeout. Twenty cooperative scheduler ticks
  // is a bounded teardown grace period, not an unbounded owner-task wait.
  for (unsigned attempt = 0; attempt < 20; ++attempt) {
    if (!serialIoInFlight.load(std::memory_order_acquire)) return true;
    vTaskDelay(1);
  }
#endif
  return serialIoInFlight.load(std::memory_order_acquire) == 0;
}

bool clearInstalledLease(bool stopPhysical) {
  activeStreamEpoch.store(0, std::memory_order_release);
  if (!waitInstalledSerialIoIdle()) return false;
  if (stopPhysical && !installedSession.closeChecked()) return false;
  if (rxHandle) {
    const auto result = nativeStreamCloseOwned(rxHandle);
    if (result != T5_STREAM_OK && result != T5_STREAM_INVALID) return false;
    rxHandle = 0;
  }
  if (txHandle) {
    const auto result = nativeStreamCloseOwned(txHandle);
    if (result != T5_STREAM_OK && result != T5_STREAM_INVALID) return false;
    txHandle = 0;
  }
  if (!installedSession.unbindChecked()) return false;
  if (physicalLease) {
    const auto released = RuntimeDevices::systemRegistry().release(physicalLease, physicalOwner);
    if (released != RuntimeDevices::Result::Ok &&
        released != RuntimeDevices::Result::Stale) return false;
  }
  physicalLease = 0;
  physicalOwner = 0;
  physicalConsumer = SerialConsumer::None;
  activeSemanticDevice = 0;
  activeInstalledApi = nullptr;
  activeProviderDevice = 0;
  activeProviderGeneration = 0;
  activeStreamEpoch = 0;
  leaseHandle = 0;
  leaseEpoch = 0;
  return true;
}

t5_serial_result_t failedInstalledOpen(t5_serial_port_lease_t pending,
                                       t5_serial_port_lease_t* privateLease,
                                       t5_serial_result_t result,
                                       t5_stream_t pendingRx = 0,
                                       t5_stream_t pendingTx = 0) {
  leaseHandle = pending;
  rxHandle = pendingRx;
  txHandle = pendingTx;
  if (!clearInstalledLease(true) && privateLease) *privateLease = pending;
  return result;
}

t5_serial_result_t installedAcquirePort(const t5_serial_port_request_t* request,
                                        t5_serial_port_lease_t* lease,
                                        t5_stream_t* rx, t5_stream_t* tx) {
  if (lease) *lease = 0;
  if (rx) *rx = 0;
  if (tx) *tx = 0;
  if (!authorized()) return T5_SERIAL_DENIED;
  if (!request || !lease || !rx || !tx) return T5_SERIAL_INVALID;
  if (request->config.flow_control != T5_SERIAL_FLOW_NONE) return T5_SERIAL_UNSUPPORTED;
  if (!validConfig(&request->config)) return T5_SERIAL_INVALID;
  if (leaseHandle || physicalLease || nativeStreamSerialIsBusy()) return T5_SERIAL_BUSY;
  if (leaseGeneration >= kMaxHandleGeneration || nextStreamEpoch == UINT32_MAX)
    return T5_SERIAL_LIMIT;

  const auto inventory = installedSerial.refresh();
  if (inventory == RuntimeInstalledProviders::InstalledSerialInventory::Result::Uncertain)
    return T5_SERIAL_IO;
  if (inventory != RuntimeInstalledProviders::InstalledSerialInventory::Result::Updated)
    return T5_SERIAL_IO;

  auto* context = RuntimeResources::ExecutionContext::current();
  if (!context || !context->running(context->id())) return T5_SERIAL_DENIED;
  RuntimeDevices::LeaseHandle deviceLease = 0;
  const auto selected = RuntimeDevices::systemRegistry().acquire(
      "serial.port", context->id(), &deviceLease, request->device,
      RuntimeDevices::Mode::Exclusive);
  if (selected != RuntimeDevices::Result::Ok) {
    if (selected == RuntimeDevices::Result::Busy) return T5_SERIAL_BUSY;
    if (selected == RuntimeDevices::Result::Limit) return T5_SERIAL_LIMIT;
    if (selected == RuntimeDevices::Result::Invalid) return T5_SERIAL_INVALID;
    return request->device ? T5_SERIAL_INVALID : T5_SERIAL_UNSUPPORTED;
  }
  RuntimeDevices::LeaseInfo selectedLease{};
  if (!RuntimeDevices::systemRegistry().getLease(deviceLease, context->id(), &selectedLease)) {
    (void)RuntimeDevices::systemRegistry().release(deviceLease, context->id());
    return T5_SERIAL_IO;
  }

  const risc_serial_port_api_v1* providerApi = nullptr;
  uint64_t providerDevice = 0, providerGeneration = 0;
  if (!installedSerial.resolve(selectedLease.device, &providerApi,
                               &providerDevice, &providerGeneration) ||
      !providerApi || !providerDevice || !providerGeneration) {
    (void)RuntimeDevices::systemRegistry().release(deviceLease, context->id());
    return T5_SERIAL_DISCONNECTED;
  }

  physicalLease = deviceLease;
  physicalOwner = context->id();
  physicalConsumer = SerialConsumer::SerialPort;
  activeSemanticDevice = selectedLease.device;
  activeInstalledApi = providerApi;
  activeProviderDevice = providerDevice;
  activeProviderGeneration = providerGeneration;
  const t5_serial_port_lease_t pending = ((++leaseGeneration) << 1u) | 1u;

  // The inventory grant already pins the exact provider generation. The
  // hardware-blind session binds that exact semantic API; no firmware class
  // scan, USB identity interpretation or competing provider selection occurs.
  if (!installedSession.bind(providerApi, providerDevice, providerGeneration) ||
      !installedSession.open(request->config))
    return failedInstalledOpen(pending, lease, T5_SERIAL_IO);
  activeStreamEpoch.store(++nextStreamEpoch, std::memory_order_release);

  t5_stream_t newRx = 0, newTx = 0;
  const auto streamResult = nativeStreamOpenSerialPair(&newRx, &newTx);
  if (streamResult != T5_STREAM_OK) {
    const auto mapped = streamResult == T5_STREAM_BUSY ? T5_SERIAL_BUSY :
        streamResult == T5_STREAM_UNSUPPORTED ? T5_SERIAL_UNSUPPORTED :
        streamResult == T5_STREAM_DENIED ? T5_SERIAL_DENIED :
        streamResult == T5_STREAM_LIMIT ? T5_SERIAL_LIMIT : T5_SERIAL_IO;
    return failedInstalledOpen(pending, lease, mapped, newRx, newTx);
  }
  leaseHandle = pending;
  rxHandle = newRx;
  txHandle = newTx;
  currentConfig = request->config;
  *lease = leaseHandle;
  *rx = rxHandle;
  *tx = txHandle;
  return T5_SERIAL_OK;
}

t5_serial_result_t installedConfigurePort(t5_serial_port_lease_t lease,
                                          const t5_serial_config_t* config) {
  if (!authorized()) return T5_SERIAL_DENIED;
  if (!validLease(lease)) return T5_SERIAL_CLOSED;
  synchronizeSerialDevices();
  if (installedLeaseRevoked()) return T5_SERIAL_DISCONNECTED;
  if (config && config->flow_control != T5_SERIAL_FLOW_NONE) return T5_SERIAL_UNSUPPORTED;
  if (!validConfig(config)) return T5_SERIAL_INVALID;
  if (!installedSession.configure(*config)) return T5_SERIAL_IO;
  currentConfig = *config;
  return T5_SERIAL_OK;
}

t5_serial_result_t installedReadStatus(t5_serial_port_lease_t lease,
                                       t5_serial_port_state_t* out) {
  if (!authorized()) return T5_SERIAL_DENIED;
  if (!validLease(lease)) return T5_SERIAL_CLOSED;
  if (!out) return T5_SERIAL_INVALID;
  std::memset(out, 0, sizeof(*out));
  synchronizeSerialDevices();
  if (installedLeaseRevoked() || !installedSession.started()) {
    out->status = T5_SERIAL_STATUS_WAITING;
    out->last_error = T5_SERIAL_DISCONNECTED;
    out->config = currentConfig;
    return T5_SERIAL_OK;
  }
  RuntimeDevices::DeviceInfo info{};
  if (!RuntimeDevices::systemRegistry().get(activeSemanticDevice, &info))
    return T5_SERIAL_DISCONNECTED;
  out->status = T5_SERIAL_STATUS_READY;
  out->connected = 1;
  out->dtr = installedSession.dtr();
  out->rts = installedSession.rts();
  out->device = activeSemanticDevice;
  out->config = installedSession.config();
  std::snprintf(out->device_label, sizeof(out->device_label), "%s",
                info.label[0] ? info.label : "Serial device");
  return T5_SERIAL_OK;
}

t5_serial_result_t installedSetControlLines(t5_serial_port_lease_t lease,
                                            bool dtr, bool rts) {
  if (!authorized()) return T5_SERIAL_DENIED;
  if (!validLease(lease)) return T5_SERIAL_CLOSED;
  synchronizeSerialDevices();
  if (installedLeaseRevoked()) return T5_SERIAL_DISCONNECTED;
  return installedSession.control(dtr, rts) ? T5_SERIAL_OK : T5_SERIAL_IO;
}

t5_serial_result_t installedReleasePort(t5_serial_port_lease_t lease) {
  if (!authorized()) return T5_SERIAL_DENIED;
  if (!validLease(lease)) return T5_SERIAL_CLOSED;
  return clearInstalledLease(true) ? T5_SERIAL_OK : T5_SERIAL_IO;
}

bool installedAvailable(void*) {
  const auto result = installedSerial.refresh();
  // A verified empty inventory may yield to another semantic serial transport.
  // Uncertain/corrupt inventory must remain selected so acquire() returns a
  // fail-closed error rather than silently activating unrelated hardware.
  if (result == RuntimeInstalledProviders::InstalledSerialInventory::Result::Updated)
    return installedSerial.publishedDevices() != 0;
  return true;
}
bool installedMatches(void*, t5_serial_device_t id) {
  const risc_serial_port_api_v1* providerApi = nullptr;
  uint64_t providerDevice = 0, generation = 0;
  return id && installedSerial.resolve(id, &providerApi, &providerDevice, &generation);
}
t5_serial_result_t acquireInstalled(void*, const t5_serial_port_request_t* request,
                                    t5_serial_port_lease_t* lease,
                                    t5_stream_t* rx, t5_stream_t* tx) {
  return installedAcquirePort(request, lease, rx, tx);
}
t5_serial_result_t configureInstalled(void*, t5_serial_port_lease_t lease,
                                      const t5_serial_config_t* config) {
  return installedConfigurePort(lease, config);
}
t5_serial_result_t statusInstalled(void*, t5_serial_port_lease_t lease,
                                   t5_serial_port_state_t* state) {
  return installedReadStatus(lease, state);
}
t5_serial_result_t controlInstalled(void*, t5_serial_port_lease_t lease,
                                    bool dtr, bool rts) {
  return installedSetControlLines(lease, dtr, rts);
}
t5_serial_result_t releaseInstalled(void*, t5_serial_port_lease_t lease) {
  return installedReleasePort(lease);
}
const RuntimeSerial::Provider installedProvider = {
    "installed.serial", 0, nullptr, installedAvailable, installedMatches,
    acquireInstalled, configureInstalled, statusInstalled, controlInstalled,
    releaseInstalled};
bool usbRegistered = false;
void ensureUsbRegistered() {
  if (!usbRegistered) usbRegistered = providers.add(installedProvider);
}
#else
bool leaseRevoked() {
  return devices.epoch() != leaseEpoch || leaseEpoch == UINT32_MAX;
}

// Close the physical session first. A failed endpoint close must retain its
// exact stream handle, serial token and physical lease for a subsequent retry.
// INVALID is an already-closed endpoint generation, not an active endpoint.
bool clearLease(bool stopUsb) {
  if (stopUsb && !nativeUsbClassStopChecked()) return false;
  if (rxHandle) {
    const auto result = nativeStreamCloseOwned(rxHandle);
    if (result != T5_STREAM_OK && result != T5_STREAM_INVALID) return false;
    rxHandle = 0;
  }
  if (txHandle) {
    const auto result = nativeStreamCloseOwned(txHandle);
    if (result != T5_STREAM_OK && result != T5_STREAM_INVALID) return false;
    txHandle = 0;
  }
  releasePhysicalDevice(SerialConsumer::SerialPort);
  leaseHandle = 0;
  leaseEpoch = 0;
  synchronizeSerialDevices();
  return true;
}

// A failed open must not publish any stream or private token to the app.
// Capture even partially opened stream handles before cleanup. The generic
// registry receives the private token only if checked teardown is uncertain.
t5_serial_result_t failedOpen(t5_serial_port_lease_t pending,
                              t5_serial_port_lease_t* privateLease,
                              t5_serial_result_t result,
                              t5_stream_t pendingRx = 0,
                              t5_stream_t pendingTx = 0) {
  leaseHandle = pending;
  leaseEpoch = devices.epoch();
  rxHandle = pendingRx;
  txHandle = pendingTx;
  if (!clearLease(true) && privateLease) *privateLease = pending;
  return result;
}

t5_serial_result_t usbAcquirePort(const t5_serial_port_request_t* request,
                                  t5_serial_port_lease_t* lease,
                                  t5_stream_t* rx,
                                  t5_stream_t* tx) {
  if (lease) *lease = 0;
  if (rx) *rx = 0;
  if (tx) *tx = 0;
  if (!authorized()) return T5_SERIAL_DENIED;
  if (!request || !lease || !rx || !tx) return T5_SERIAL_INVALID;
  if (request->config.flow_control != T5_SERIAL_FLOW_NONE) return T5_SERIAL_UNSUPPORTED;
  if (!validConfig(&request->config)) return T5_SERIAL_INVALID;
  if (leaseHandle || nativeStreamUsbIsBusy()) return T5_SERIAL_BUSY;
  if (leaseGeneration >= kMaxHandleGeneration) return T5_SERIAL_LIMIT;
  if (request->device && !devices.resolve(request->device, NativeUsbDevices::Provider::UsbSerial))
    return T5_SERIAL_INVALID;

  if (!nativeUsbClassEnsureInstalled()) return T5_SERIAL_UNSUPPORTED;
  if (!nativeUsbClassAvailable()) return T5_SERIAL_UNSUPPORTED;
  const t5_serial_port_lease_t pending = ((++leaseGeneration) << 1u) | 1u;
  if (!nativeUsbClassStart(request->config))
    return failedOpen(pending, lease, T5_SERIAL_IO);
  const uint32_t startEpoch = devices.epoch();
  if (startEpoch == UINT32_MAX)
    return failedOpen(pending, lease, T5_SERIAL_LIMIT);

  t5_stream_t newRx = 0, newTx = 0;
  const auto streamResult = nativeStreamOpenUsbPair(&newRx, &newTx);
  if (streamResult != T5_STREAM_OK) {
    if (streamResult == T5_STREAM_BUSY)
      return failedOpen(pending, lease, T5_SERIAL_BUSY, newRx, newTx);
    if (streamResult == T5_STREAM_UNSUPPORTED)
      return failedOpen(pending, lease, T5_SERIAL_UNSUPPORTED, newRx, newTx);
    if (streamResult == T5_STREAM_DENIED)
      return failedOpen(pending, lease, T5_SERIAL_DENIED, newRx, newTx);
    if (streamResult == T5_STREAM_LIMIT)
      return failedOpen(pending, lease, T5_SERIAL_LIMIT, newRx, newTx);
    return failedOpen(pending, lease, T5_SERIAL_IO, newRx, newTx);
  }
  const bool detached = devices.epoch() != startEpoch;
  if (detached || !claimPhysicalDevice(SerialConsumer::SerialPort))
    return failedOpen(pending, lease,
                      detached ? T5_SERIAL_DISCONNECTED : T5_SERIAL_BUSY,
                      newRx, newTx);

  auto* owner = RuntimeResources::ExecutionContext::current();
  if (!owner || !nativeUsbClassAttachPair(owner->id(), newRx, newTx))
    return failedOpen(pending, lease, T5_SERIAL_IO, newRx, newTx);
  leaseHandle = pending;
  leaseEpoch = startEpoch;
  rxHandle = newRx;
  txHandle = newTx;
  currentConfig = request->config;
  *lease = leaseHandle;
  *rx = rxHandle;
  *tx = txHandle;
  return T5_SERIAL_OK;
}

t5_serial_result_t usbConfigurePort(t5_serial_port_lease_t lease, const t5_serial_config_t* config) {
  if (!authorized()) return T5_SERIAL_DENIED;
  if (!validLease(lease)) return T5_SERIAL_CLOSED;
  synchronizeSerialDevices();
  if (leaseRevoked()) return T5_SERIAL_DISCONNECTED;
  if (!claimPhysicalDevice(SerialConsumer::SerialPort)) return T5_SERIAL_BUSY;
  if (config && config->flow_control != T5_SERIAL_FLOW_NONE) return T5_SERIAL_UNSUPPORTED;
  if (!validConfig(config)) return T5_SERIAL_INVALID;
  if (!nativeUsbClassConfigure(*config)) return T5_SERIAL_IO;
  currentConfig = *config;
  return T5_SERIAL_OK;
}

t5_serial_result_t usbReadStatus(t5_serial_port_lease_t lease, t5_serial_port_state_t* out) {
  if (!authorized()) return T5_SERIAL_DENIED;
  if (!validLease(lease)) return T5_SERIAL_CLOSED;
  if (!out) return T5_SERIAL_INVALID;
  std::memset(out, 0, sizeof(*out));
  synchronizeSerialDevices();
  if (leaseRevoked()) {
    out->status = T5_SERIAL_STATUS_WAITING;
    out->last_error = T5_SERIAL_DISCONNECTED;
    out->config = currentConfig;
    return T5_SERIAL_OK;
  }
  t5_usb_serial_state_t state{};
  if (!nativeUsbClassReadState(&state)) return T5_SERIAL_IO;
  const auto device = devices.snapshot();
  synchronizeSerialDevices();
  if (leaseRevoked()) {
    out->status = T5_SERIAL_STATUS_WAITING;
    out->last_error = T5_SERIAL_DISCONNECTED;
    out->config = currentConfig;
    return T5_SERIAL_OK;
  }
  const bool bound = device.presence == NativeUsbDevices::Presence::Bound;
  if (state.connected && bound && !claimPhysicalDevice(SerialConsumer::SerialPort)) return T5_SERIAL_BUSY;
  out->status = semanticStatus(state.status);
  out->connected = state.connected && bound;
  if (!bound && out->status != T5_SERIAL_STATUS_ERROR && out->status != T5_SERIAL_STATUS_OFF)
    out->status = T5_SERIAL_STATUS_WAITING;
  out->dtr = state.dtr;
  out->rts = state.rts;
  out->last_error = state.last_error;
  out->rx_bytes = state.rx_bytes;
  out->tx_bytes = state.tx_bytes;
  out->dropped_rx_bytes = state.dropped_rx_bytes;
  out->device = bound && state.connected ? device.id : 0;
  out->config = currentConfig;
  if (out->device && device.product[0]) {
    std::snprintf(out->device_label, sizeof(out->device_label), "%s  %04X:%04X",
                  device.product, (unsigned)device.vid, (unsigned)device.pid);
  } else if (out->device) {
    std::snprintf(out->device_label, sizeof(out->device_label), "Serial device  %04X:%04X",
                  (unsigned)device.vid, (unsigned)device.pid);
  }
  return T5_SERIAL_OK;
}

t5_serial_result_t usbSetControlLines(t5_serial_port_lease_t lease, bool dtr, bool rts) {
  if (!authorized()) return T5_SERIAL_DENIED;
  if (!validLease(lease)) return T5_SERIAL_CLOSED;
  synchronizeSerialDevices();
  if (leaseRevoked()) return T5_SERIAL_DISCONNECTED;
  if (!claimPhysicalDevice(SerialConsumer::SerialPort)) return T5_SERIAL_BUSY;
  return nativeUsbClassControl(dtr, rts) ? T5_SERIAL_OK : T5_SERIAL_IO;
}
t5_serial_result_t usbReleasePort(t5_serial_port_lease_t lease) {
  if (!authorized()) return T5_SERIAL_DENIED;
  if (!validLease(lease)) return T5_SERIAL_CLOSED;
  return clearLease(true) ? T5_SERIAL_OK : T5_SERIAL_IO;
}
bool usbAvailable(void*) { return nativeUsbClassAvailable(); }
bool usbMatches(void*, t5_serial_device_t id) {
  return devices.resolve(id, NativeUsbDevices::Provider::UsbSerial);
}
t5_serial_result_t acquireUsb(void*, const t5_serial_port_request_t* request,
                              t5_serial_port_lease_t* lease, t5_stream_t* rx, t5_stream_t* tx) {
  return usbAcquirePort(request, lease, rx, tx);
}
t5_serial_result_t configureUsb(void*, t5_serial_port_lease_t lease, const t5_serial_config_t* config) {
  return usbConfigurePort(lease, config);
}
t5_serial_result_t statusUsb(void*, t5_serial_port_lease_t lease, t5_serial_port_state_t* state) {
  return usbReadStatus(lease, state);
}
t5_serial_result_t controlUsb(void*, t5_serial_port_lease_t lease, bool dtr, bool rts) {
  return usbSetControlLines(lease, dtr, rts);
}
t5_serial_result_t releaseUsb(void*, t5_serial_port_lease_t lease) {
  return usbReleasePort(lease);
}
const RuntimeSerial::Provider usbProvider = {
    "usb.serial", 0, nullptr, usbAvailable, usbMatches, acquireUsb, configureUsb,
    statusUsb, controlUsb, releaseUsb};
bool usbRegistered = false;
void ensureUsbRegistered() {
  if (!usbRegistered) usbRegistered = providers.add(usbProvider);
}

#endif
t5_serial_result_t acquirePort(const t5_serial_port_request_t* request,
                               t5_serial_port_lease_t* lease,
                               t5_stream_t* rx,
                               t5_stream_t* tx) {
  if (lease) *lease = 0;
  if (rx) *rx = 0;
  if (tx) *tx = 0;
  if (!authorized()) {
    traceResult("acquire", T5_SERIAL_DENIED, 0, request ? request->device : 0);
    return T5_SERIAL_DENIED;
  }
  synchronizeSerialDevices();
  ensureUsbRegistered();
  const auto rc = providers.acquire(request, lease, rx, tx);
  traceResult("acquire", rc, rc == T5_SERIAL_OK && lease ? *lease : 0,
              request ? request->device : 0);
  if (rc == T5_SERIAL_OK) resetStatusTrace();
  return rc;
}
t5_serial_result_t configurePort(t5_serial_port_lease_t lease, const t5_serial_config_t* config) {
  const auto rc = authorized() ? providers.configure(lease, config) : T5_SERIAL_DENIED;
  traceResult("configure", rc, lease, 0);
  return rc;
}
t5_serial_result_t readStatus(t5_serial_port_lease_t lease, t5_serial_port_state_t* state) {
  const auto rc = authorized() ? providers.status(lease, state) : T5_SERIAL_DENIED;
  if (rc != T5_SERIAL_OK) traceResult("read-status", rc, lease, 0);
  else if (state) traceStatus(lease, *state);
  return rc;
}
t5_serial_result_t setControlLines(t5_serial_port_lease_t lease, bool dtr, bool rts) {
  const auto rc = authorized() ? providers.control(lease, dtr, rts) : T5_SERIAL_DENIED;
  traceResult("control-lines", rc, lease, 0);
  return rc;
}
t5_serial_result_t releasePort(t5_serial_port_lease_t lease) {
  const auto rc = authorized() ? providers.release(lease) : T5_SERIAL_DENIED;
  traceResult("release", rc, lease, 0);
  if (rc == T5_SERIAL_OK) resetStatusTrace();
  return rc;
}

const t5_serial_port_api_v1 api = {
    T5_SERIAL_PORT_API_VERSION,
    sizeof(t5_serial_port_api_v1),
    T5_SERIAL_PORT_CAPABILITY,
    acquirePort,
    configurePort,
    readStatus,
    setControlLines,
    releasePort,
};
} // namespace

bool nativeRegisterSerialProvider(const RuntimeSerial::Provider& provider) {
  ensureUsbRegistered();
  return providers.add(provider);
}
bool nativeUnregisterSerialProvider(const char* id) {
  if (!id) return false;
#if RISCRTE_INSTALLED_SERIAL_PATH
  if (std::strcmp(id, "installed.serial") == 0) return false;
#else
  if (std::strcmp(id, "usb.serial") == 0) return false;
#endif
  return providers.remove(id);
}
#if RISCRTE_INSTALLED_SERIAL_PATH
bool nativeSerialProviderActive() {
  return activeStreamEpoch.load(std::memory_order_acquire) != 0 &&
         installedSession.started();
}
uint32_t nativeSerialProviderEpoch() {
  return activeStreamEpoch.load(std::memory_order_acquire);
}
int32_t nativeSerialProviderRead(uint8_t* dst, uint32_t capacity, uint32_t* out) {
  if (out) *out = 0;
  const uint32_t epoch = activeStreamEpoch.load(std::memory_order_acquire);
  if (!epoch) return T5_STREAM_DISCONNECTED;
  if (serialIoInFlight.fetch_add(1, std::memory_order_acq_rel) == UINT32_MAX) {
    serialIoInFlight.fetch_sub(1, std::memory_order_acq_rel);
    return T5_STREAM_IO;
  }
  if (activeStreamEpoch.load(std::memory_order_acquire) != epoch) {
    serialIoInFlight.fetch_sub(1, std::memory_order_release);
    return T5_STREAM_DISCONNECTED;
  }
  const int32_t result = installedSession.read(dst, capacity, out);
  serialIoInFlight.fetch_sub(1, std::memory_order_release);
  if (activeStreamEpoch.load(std::memory_order_acquire) != epoch) {
    if (out) *out = 0;
    return T5_STREAM_DISCONNECTED;
  }
  return result;
}
int32_t nativeSerialProviderWrite(const uint8_t* src, uint32_t length, uint32_t* out) {
  if (out) *out = 0;
  const uint32_t epoch = activeStreamEpoch.load(std::memory_order_acquire);
  if (!epoch) return T5_STREAM_DISCONNECTED;
  if (serialIoInFlight.fetch_add(1, std::memory_order_acq_rel) == UINT32_MAX) {
    serialIoInFlight.fetch_sub(1, std::memory_order_acq_rel);
    return T5_STREAM_IO;
  }
  if (activeStreamEpoch.load(std::memory_order_acquire) != epoch) {
    serialIoInFlight.fetch_sub(1, std::memory_order_release);
    return T5_STREAM_DISCONNECTED;
  }
  const int32_t result = installedSession.write(src, length, out);
  serialIoInFlight.fetch_sub(1, std::memory_order_release);
  if (activeStreamEpoch.load(std::memory_order_acquire) != epoch) {
    if (out) *out = 0;
    return T5_STREAM_DISCONNECTED;
  }
  return result;
}
#else
bool nativeSerialProviderActive() { return nativeUsbClassAvailable(); }
uint32_t nativeSerialProviderEpoch() {
  // The legacy host fixture starts at epoch zero, while semantic streams
  // reserve zero for an inactive provider. Preserve every valid generation.
  const uint32_t epoch = devices.epoch();
  return epoch == UINT32_MAX ? 0 : epoch + 1u;
}
int32_t nativeSerialProviderRead(uint8_t* dst, uint32_t capacity, uint32_t* out) {
  return nativeUsbClassRead(dst, capacity, out);
}
int32_t nativeSerialProviderWrite(const uint8_t* src, uint32_t length, uint32_t* out) {
  return nativeUsbClassWrite(src, length, out);
}
#endif

#if !RISCRTE_INSTALLED_SERIAL_PATH
void nativeUsbProviderAttach(const t5_usb_serial_state_t* state, uint8_t dataInterface) {
  if (state) devices.observe(*state, dataInterface);
}
void nativeUsbProviderDetach() { devices.detach(); }
uint32_t nativeUsbProviderEpoch() { return devices.epoch(); }

bool nativeUsbDirectStreamClaim(uint32_t expectedEpoch) {
  if (!authorized()) return false;
  synchronizeSerialDevices();
  if (devices.epoch() != expectedEpoch) return false;
  if (!claimPhysicalDevice(SerialConsumer::LegacyDirectStream)) return false;
  if (devices.epoch() != expectedEpoch) {
    synchronizeSerialDevices();
    releasePhysicalDevice(SerialConsumer::LegacyDirectStream);
    return false;
  }
  return true;
}
void nativeUsbDirectStreamRelease() {
  releasePhysicalDevice(SerialConsumer::LegacyDirectStream);
}
#endif
void nativeDeviceDiscoveryTick() { synchronizeSerialDevices(); }

void nativeSerialPortsBegin() {
  active = true;
  providers.end();
  if (providers.leased() || leaseHandle) {
    active = false;
    traceResult("context-begin-quarantined", T5_SERIAL_BUSY, leaseHandle, 0);
    return;
  }
  resetStatusTrace();
  synchronizeSerialDevices();
  ensureUsbRegistered();
  traceResult("context-begin", T5_SERIAL_OK, 0, 0);
}
void nativeSerialPortsEnd() {
  providers.end();
  if (providers.leased() || leaseHandle) {
    active = false;
    traceResult("context-end-quarantined", T5_SERIAL_IO, leaseHandle, 0);
    return;
  }
  releasePhysicalDevice(SerialConsumer::SerialPort);
  releasePhysicalDevice(SerialConsumer::LegacyDirectStream);
  synchronizeSerialDevices();
  active = false;
  resetStatusTrace();
  traceResult("context-end", T5_SERIAL_OK, 0, 0);
}

extern "C" const t5_serial_port_api_v1* t5_serial_port_get_api(uint32_t version) {
  if (version != T5_SERIAL_PORT_API_VERSION || !authorized()) return nullptr;
  return &api;
}

#undef RISCRTE_INSTALLED_SERIAL_PATH
