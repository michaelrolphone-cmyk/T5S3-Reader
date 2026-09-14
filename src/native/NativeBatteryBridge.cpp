#include <T5AppApi.h>
#include <T5BatteryApi.h>

#include <Board.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace {

bool active() { return t5_app_get_api(T5_APP_ABI_VERSION) != nullptr; }

void copyText(char* dst, size_t capacity, const char* src) {
  if (!dst || capacity == 0) return;
  if (!src) src = "";
  size_t i = 0;
  while (src[i] && i + 1 < capacity) {
    dst[i] = src[i];
    ++i;
  }
  dst[i] = '\0';
}

bool readState(t5_battery_state_t* out) {
  if (!out || !active()) return false;
  std::memset(out, 0, sizeof(*out));

  copyText(out->board_name, sizeof(out->board_name), Board::displayName());
  out->detailed_telemetry = Board::capabilities().hasDetailedBatteryTelemetry ? 1u : 0u;
  out->profile_capacity_mah = Board::batteryProfile().capacityMah;

  (void)Board::beginBatteryManagement();
  Board::BatteryState state{};
  const bool available = Board::readBatteryState(&state);
  out->available = available ? 1u : 0u;

  out->charger_ready = state.chargerReady ? 1u : 0u;
  out->gauge_ready = state.gaugeReady ? 1u : 0u;
  out->charger_read_ok = state.chargerReadOk ? 1u : 0u;
  out->gauge_read_ok = state.gaugeReadOk ? 1u : 0u;
  out->vbus_connected = state.vbusConnected ? 1u : 0u;
  out->charge_enabled = state.chargeEnabled ? 1u : 0u;
  out->charging = state.charging ? 1u : 0u;
  out->charge_done = state.chargeDone ? 1u : 0u;
  out->gauge_battery_full_flag = state.gaugeBatteryFullFlag ? 1u : 0u;
  out->gauge_gauging_full_flag = state.gaugeGaugingFullFlag ? 1u : 0u;
  out->gauge_taper_flag = state.gaugeTaperFlag ? 1u : 0u;
  out->gauge_charge_inhibit = state.gaugeChargeInhibit ? 1u : 0u;
  out->charger_vbus_status = state.chargerVbusStatus;
  out->charger_status = static_cast<uint8_t>(state.chargerStatus);
  out->gauge_state = static_cast<uint8_t>(state.gaugeState);
  out->input_limit_ma = state.inputLimitMa;
  out->charge_current_ma = state.chargeCurrentMa;
  out->precharge_current_ma = state.prechargeCurrentMa;
  out->termination_current_ma = state.terminationCurrentMa;
  out->charger_adc_current_ma = state.chargerAdcCurrentMa;
  out->charge_voltage_mv = state.chargeVoltageMv;
  out->system_voltage_mv = state.systemVoltageMv;
  out->battery_voltage_mv = state.batteryVoltageMv;
  out->vbus_voltage_mv = state.vbusVoltageMv;
  out->gauge_voltage_mv = state.gaugeVoltageMv;
  out->gauge_charge_voltage_mv = state.gaugeChargeVoltageMv;
  out->gauge_taper_current_ma = state.gaugeTaperCurrentMa;
  out->soc_percent = state.socPercent;
  out->soh_percent = state.sohPercent;
  out->full_capacity_mah = state.fullCapacityMah;
  out->remaining_capacity_mah = state.remainingCapacityMah;
  out->temperature_dk = state.temperatureDk;
  out->battery_status_raw = state.batteryStatusRaw;
  out->gauging_status_raw = state.gaugingStatusRaw;
  out->current_ma = state.currentMa;
  out->average_current_ma = state.averageCurrentMa;
  return true;
}

const t5_battery_api_v1 api = {T5_BATTERY_API_VERSION, sizeof(t5_battery_api_v1), readState};

}  // namespace

extern "C" const t5_battery_api_v1* t5_battery_get_api(uint32_t version) {
  return version == T5_BATTERY_API_VERSION && active() ? &api : nullptr;
}
