#include <T5ButtonRemapApi.h>

#include "CrossPointSettings.h"

namespace {

bool valid(const t5_button_remap_mapping_t* mapping) {
  if (!mapping) return false;
  bool seen[T5_BUTTON_REMAP_ROLE_COUNT] = {};
  for (uint32_t i = 0; i < T5_BUTTON_REMAP_ROLE_COUNT; ++i) {
    const uint8_t hardware = mapping->role_to_hardware[i];
    if (hardware >= T5_BUTTON_REMAP_ROLE_COUNT || seen[hardware]) return false;
    seen[hardware] = true;
  }
  return true;
}

bool readMapping(t5_button_remap_mapping_t* out) {
  if (!out) return false;
  out->role_to_hardware[T5_BUTTON_REMAP_BACK] = SETTINGS.frontButtonBack;
  out->role_to_hardware[T5_BUTTON_REMAP_CONFIRM] = SETTINGS.frontButtonConfirm;
  out->role_to_hardware[T5_BUTTON_REMAP_LEFT] = SETTINGS.frontButtonLeft;
  out->role_to_hardware[T5_BUTTON_REMAP_RIGHT] = SETTINGS.frontButtonRight;
  return true;
}

bool applyMapping(const t5_button_remap_mapping_t* mapping) {
  if (!valid(mapping)) return false;
  SETTINGS.frontButtonBack = mapping->role_to_hardware[T5_BUTTON_REMAP_BACK];
  SETTINGS.frontButtonConfirm = mapping->role_to_hardware[T5_BUTTON_REMAP_CONFIRM];
  SETTINGS.frontButtonLeft = mapping->role_to_hardware[T5_BUTTON_REMAP_LEFT];
  SETTINGS.frontButtonRight = mapping->role_to_hardware[T5_BUTTON_REMAP_RIGHT];
  return SETTINGS.saveToFile();
}

bool resetDefaults() {
  t5_button_remap_mapping_t mapping{{
      CrossPointSettings::FRONT_HW_BACK,
      CrossPointSettings::FRONT_HW_CONFIRM,
      CrossPointSettings::FRONT_HW_LEFT,
      CrossPointSettings::FRONT_HW_RIGHT,
  }};
  return applyMapping(&mapping);
}

const t5_button_remap_api_v1 api = {
    T5_BUTTON_REMAP_API_VERSION,
    sizeof(t5_button_remap_api_v1),
    readMapping,
    applyMapping,
    resetDefaults,
};

}  // namespace

extern "C" const t5_button_remap_api_v1* t5_button_remap_get_api(uint32_t version) {
  return version == T5_BUTTON_REMAP_API_VERSION ? &api : nullptr;
}
