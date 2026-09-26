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
static Lease retainedShutdownGrant{};
static Lease preparedShutdownGrant{};
static const risc_usb_vbus_charger_api_v1* preparedShutdownApi = nullptr;

struct Session {
  Lease grant{};
  const risc_usb_vbus_charger_api_v1* api = nullptr;
};

bool release(Session& session) {
  if (!session.grant.grant.slot) return true;
  if (!RuntimeInstalledProviders::release(&session.grant)) {
    // Failed quiescence keeps the exact generation pending in the graph.
    // It is never safe to remap a different chip owner after that failure.
    quarantined = true;
    LOG_ERR("BAT", "Board power provider retained after failed release");
    return false;
  }
  session.api = nullptr;
  return true;
}

bool acquire(Session& session) {
  if (quarantined || shutdownPending) return false;
  size_t cursor = 0;
  char id[96]{}, alternate[96]{};
  if (!RuntimeInstalledProviders::nextProvider("board.power.vbus", 1u,
                                               &cursor, id, sizeof(id)) ||
      RuntimeInstalledProviders::nextProvider("board.power.vbus", 1u,
                                               &cursor, alternate, sizeof(alternate)) ||
      !RuntimeInstalledProviders::acquire(id, "board.power.vbus", 1u,
                                           &session.grant)) return false;
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
  state->vbusConnected = (raw.system_status & 0x04u) != 0u ||
                          (raw.vbus_adc & 0x80u) != 0u;
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
  *connected = (raw.system_status & 0x04u) != 0u ||
               (raw.vbus_adc & 0x80u) != 0u;
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
  (void)release(session);  // Unsafe write retains the pending grant.
  return false;
}
}  // namespace BoardPowerPort
#endif  // BOARD_T5S3
