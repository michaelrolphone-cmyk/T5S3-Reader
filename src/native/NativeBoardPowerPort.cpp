#include <BoardPowerPort.h>

#if defined(BOARD_T5S3_PRO) || defined(BOARD_T5S3)
#include <BoardT5S3.h>
#include <RiscUsbVbusV1.h>
#include "runtime/drivers/InstalledProviderGraph.h"
#include <Logging.h>
#include <cstdint>

// Board-specific compatibility adapter only: no Wire, address, I2C HAL,
// charger reset, USB role arbitration, or hardware register transactions.
// The independently installed board.power.vbus ELF owns all those operations.
namespace BoardPowerPort {
namespace {
using RuntimeInstalledProviders::Lease;
static bool quarantined = false;
static bool shutdownPending = false;
static Lease retainedShutdownGrant{};

struct Session {
  Lease grant{};
  const risc_usb_vbus_charger_api_v1* api = nullptr;
};

bool release(Session& session) {
  if (!session.grant.grant.slot) return true;
  if (!RuntimeInstalledProviders::release(&session.grant)) {
    // The graph retains the same generation as a pending release. Its ELF
    // and bus claim must never be force-unloaded or replaced after an
    // uncertain PMIC transition or unverified hardware quiescence.
    quarantined = true;
    LOG_ERR("BAT", "Board power provider retained after failed release");
    return false;
  }
  session.api = nullptr;
  return true;
}

bool acquire(Session& session) {
  if (quarantined || shutdownPending) return false;
  // A board-specific adapter may request this semantic capability, but it
  // must not assume a package ID or select an arbitrary ambiguous provider.
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
  // On a partial PMIC write the ELF refuses quiescence, and release() pins
  // the original grant rather than risking a second device owner.
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
  // Do not report a stale conversion as an externally present VBUS voltage.
  state->vbusVoltageMv = (raw.vbus_adc & 0x80u) ?
      static_cast<uint16_t>(2600u + 100u * (raw.vbus_adc & 0x7fu)) : 0u;
  state->gaugeChargeVoltageMv = state->chargeVoltageMv;
  state->gaugeTaperCurrentMa = state->terminationCurrentMa;
  // The current snapshot ABI does not contain REG12. Keep this field
  // explicitly unavailable rather than synthesizing a PMIC measurement.
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

bool shutdown() {
  Session session{};
  if (!acquire(session)) return false;
  const bool requested = session.api->request_shutdown(session.api->base.context);
  if (requested) {
    // BATFET_DIS can remove power before a readback. Do not release this
    // grant or let another caller remap a provider on a dying power rail.
    retainedShutdownGrant = session.grant;
    session.grant = {};
    shutdownPending = true;
    return true;
  }
  (void)release(session);  // Unsafe transition retains the pending grant.
  return false;
}
}  // namespace BoardPowerPort
#endif  // BOARD_T5S3
