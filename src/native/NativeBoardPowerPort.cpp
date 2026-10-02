#include <BoardPowerPort.h>

#if defined(BOARD_T5S3_PRO) || defined(BOARD_T5S3)
#include <BoardT5S3.h>
#include <RiscUsbVbusV1.h>
#include "runtime/drivers/InstalledProviderGraph.h"
#include <Logging.h>
#include <cstdint>

// Compatibility boundary only. Hardware register access and all charge,
// source, shutdown and recovery policies reside in the installed owner ELF.
namespace BoardPowerPort {
namespace {
using RuntimeInstalledProviders::Lease;
static bool quarantined = false;
static bool shutdownPending = false;
static bool shutdownPreparationAttempted = false;
// One serialized board operation may leave one exact release pending. Never
// retain its interface: release has already revoked that grant's callbacks.
static Lease pendingReleaseGrant{};
static Lease retainedShutdownGrant{};
static Lease preparedShutdownGrant{};
static const risc_usb_vbus_charger_api_v1* preparedShutdownApi = nullptr;

struct Session {
  Lease grant{};
  const risc_usb_vbus_charger_api_v1* api = nullptr;
};

void retainRelease(Session& session) {
  if (session.grant.grant.slot) {
    pendingReleaseGrant = session.grant;
    pendingReleaseGrant.interface = nullptr;
  }
  session.grant = {};
  session.api = nullptr;
}

bool retryRelease() {
  if (!pendingReleaseGrant.grant.slot) return true;
  // One attempt per subsequent operation, without rescanning or remapping.
  // The graph retains the same generation and dependencies on failure.
  if (!RuntimeInstalledProviders::release(&pendingReleaseGrant)) return false;
  pendingReleaseGrant = {};
  return true;
}

bool release(Session& session) {
  if (!session.grant.grant.slot) return true;
  if (!RuntimeInstalledProviders::release(&session.grant)) {
    retainRelease(session);
    LOG_ERR("BAT", "Board power provider retained after failed release");
    return false;
  }
  session.api = nullptr;
  return true;
}

bool acquire(Session& session) {
  if (quarantined || shutdownPending || !retryRelease()) return false;
  size_t cursor = 0;
  char id[96]{}, alternate[96]{};
  if (!RuntimeInstalledProviders::nextProvider("board.power.vbus", 1u,
                                               &cursor, id, sizeof(id)) ||
      RuntimeInstalledProviders::nextProvider("board.power.vbus", 1u,
                                               &cursor, alternate, sizeof(alternate))) return false;
  if (!RuntimeInstalledProviders::acquire(id, "board.power.vbus", 1u,
                                           &session.grant)) {
    // Admission can fail after mapping, leaving an unusable exact grant for
    // checked teardown. Do not orphan it or call its interface.
    retainRelease(session);
    return false;
  }
  const auto* base = static_cast<const risc_usb_vbus_api_v1*>(session.grant.interface);
  if (!base || base->api_version != RISC_USB_VBUS_API_V1 ||
      base->struct_size < sizeof(risc_usb_vbus_charger_api_v1)) {
    (void)release(session);
    return false;
  }
  session.api = static_cast<const risc_usb_vbus_charger_api_v1*>(session.grant.interface);
  if (!session.api->read_charger || !session.api->configure_charger ||
      !session.api->request_shutdown) {
    (void)release(session);
    return false;
  }
  return true;
}

bool snapshot(risc_bq25896_charger_snapshot_v1& out) {
  Session session{};
  if (!acquire(session)) return false;
  const bool ok = session.api->read_charger(session.api->base.context, &out);
  return release(session) && ok;
}
}  // namespace

bool configure() {
  Session session{};
  if (!acquire(session)) return false;
  const bool ok = session.api->configure_charger(session.api->base.context);
  // A partial PMIC write prevents ELF quiescence and retains this generation.
  return release(session) && ok;
}

bool read(BoardT5S3::BatteryState* state) {
  if (!state) return false;
  risc_bq25896_charger_snapshot_v1 raw{};
  if (!snapshot(raw)) return false;
  const uint8_t charge = static_cast<uint8_t>((raw.system_status >> 3u) & 0x03u);
  state->chargerReady = true;
  state->chargerReadOk = true;
  state->chargerVbusStatus = static_cast<uint8_t>((raw.system_status >> 5u) & 0x07u);
  state->chargerStatus = static_cast<BoardT5S3::BatteryChargeStatus>(charge);
  state->vbusConnected = raw.external_power;
  state->chargeEnabled = (raw.power_control & 0x10u) != 0u;
  state->charging = state->chargeEnabled && (charge == 1u || charge == 2u);
  state->chargeDone = charge == 3u;
  state->inputLimitMa = static_cast<uint16_t>(100u +
                                              50u * (raw.input_control & 0x3fu));
  state->chargeCurrentMa = static_cast<uint16_t>(64u * (raw.charge_current & 0x7fu));
  state->prechargeCurrentMa = static_cast<uint16_t>(64u +
      64u * ((raw.precharge_termination >> 4u) & 0x0fu));
  state->terminationCurrentMa = static_cast<uint16_t>(64u +
      64u * (raw.precharge_termination & 0x0fu));
  state->chargeVoltageMv = static_cast<uint16_t>(3840u +
      16u * ((raw.charge_voltage >> 2u) & 0x3fu));
  state->batteryVoltageMv = static_cast<uint16_t>(2304u +
      20u * (raw.battery_adc & 0x7fu));
  state->systemVoltageMv = static_cast<uint16_t>(2304u +
      20u * (raw.system_adc & 0x7fu));
  // REG11's conversion can be stale when the VBUS-good flag is clear.
  state->vbusVoltageMv = (raw.vbus_adc & 0x80u) ?
      static_cast<uint16_t>(2600u + 100u * (raw.vbus_adc & 0x7fu)) : 0u;
  state->gaugeChargeVoltageMv = state->chargeVoltageMv;
  state->gaugeTaperCurrentMa = state->terminationCurrentMa;
  // REG12 is not exposed by the current snapshot ABI; do not invent a value.
  state->chargerAdcCurrentMa = 0u;
  return true;
}

bool externalPower(bool* connected) {
  if (!connected) return false;
  risc_bq25896_charger_snapshot_v1 raw{};
  if (!snapshot(raw)) return false;
  *connected = raw.external_power;
  return true;
}

bool prepareShutdown() {
  if (quarantined || shutdownPending) return false;
  if (preparedShutdownGrant.grant.slot) return true;
  // HalDisplay::deepSleep() deinitializes SD after this call. A failed first
  // reservation must not trigger a second driver load from disconnected SD.
  if (shutdownPreparationAttempted) return false;
  shutdownPreparationAttempted = true;
  Session session{};
  if (!acquire(session)) return false;
  preparedShutdownGrant = session.grant;
  preparedShutdownApi = session.api;
  session.grant = {};
  session.api = nullptr;
  return true;
}

bool shutdown() {
  if (quarantined || shutdownPending || !retryRelease()) return false;
  Session session{};
  if (preparedShutdownGrant.grant.slot) {
    session.grant = preparedShutdownGrant;
    session.api = preparedShutdownApi;
    preparedShutdownGrant = {};
    preparedShutdownApi = nullptr;
  } else {
    // Reservation was attempted before SD_CS was made INPUT and failed.
    // Never try a fresh SD-backed ELF mapping after that irreversible step.
    if (shutdownPreparationAttempted || !acquire(session)) return false;
  }
  const bool requested = session.api->request_shutdown(session.api->base.context);
  if (requested) {
    // BATFET_DIS may remove power before readback. Keep ELF, I2C claim and
    // original generation pinned until physical reboot; never release here.
    retainedShutdownGrant = session.grant;
    session.grant = {};
    shutdownPending = true;
    return true;
  }
  if (!release(session)) {
    // A false shutdown result cannot distinguish a rejected command from a
    // BATFET write that may have removed I2C power. Preserve the preexisting
    // one-way quarantine: never retry teardown or admit a new board operation.
    quarantined = true;
  }
  return false;
}
}  // namespace BoardPowerPort
#endif  // BOARD_T5S3
