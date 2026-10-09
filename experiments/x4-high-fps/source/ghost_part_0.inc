/*
 * X4LAB v0.1.7 ghosting / powered-idle diagnostic.
 *
 * This file deliberately reuses the stable v0.1.5 electrical envelope:
 *   - UC8279 LUT ID 0x68/0x69 only
 *   - fixed 800x600 controller geometry, visible rows 120..599
 *   - ordinary full-width partial windows
 *   - 20-MHz SPI only
 *   - PLL 0x0e/0x0f only
 *   - no TCON, compact TRES/GSST, voltage, VCOM, booster or PMIC changes
 *
 * Every mode drives the complete visible 800x480 region.  The destructive
 * v0.1.6 scan-geometry/timing experiments are not present or reachable.
 */

namespace {

constexpr const char* GHOST_LAB_VERSION = "0.1.7";
constexpr uint8_t GHOST_CMD_POWER_OFF = 0x02;
constexpr uint32_t GHOST_SETTLE_MS = 2300;
constexpr uint32_t GHOST_PATTERN_HOLD_MS = 30000;
constexpr uint32_t GHOST_OBSERVE_MS = 60000;
constexpr uint32_t GHOST_SPLIT_MS = 30000;
constexpr uint32_t GHOST_LOG_INTERVAL_MS = 5000;
constexpr size_t GHOST_VISIBLE_BYTES = static_cast<size_t>(ROW_BYTES) * PANEL_HEIGHT;

// The product driver corrected the absolute transition-bank polarity after the
// v0.1.5 laboratory run.  These diagnostics use that corrected mapping:
// registers 0x21..0x24 = 0x81, 0x81, 0x41, 0x41 for a one-frame pulse.
constexpr uint8_t GHOST_ABSOLUTE_BLACK_RAIL = 0x80;
constexpr uint8_t GHOST_ABSOLUTE_WHITE_RAIL = 0x40;

enum class GhostAction : uint8_t {
  PoweredOn,
  PanelOff,
  RawInverseOn,
  MaintenanceAt30s,
  SyncOldOn,
  SyncOldOff,
  TargetCompOn,
  TargetCompOff,
  PowerCycleNoRefresh,
};

struct GhostMode {
  const char* name;
  GhostAction action;
};

constexpr GhostMode GHOST_MODES[] = {
    {"clear + 2.3s settle; powered ON for 60s", GhostAction::PoweredOn},
    {"clear + 2.3s settle; POF for 60s", GhostAction::PanelOff},
    {"clear + settle; 1F inverted pattern then 1F white; powered ON", GhostAction::RawInverseOn},
    {"clear + settle; powered ON with one resident DRF at t=30s", GhostAction::MaintenanceAt30s},
    {"clear + settle; synchronize DTM1=white; powered ON", GhostAction::SyncOldOn},
    {"clear + settle; synchronize DTM1=white then POF", GhostAction::SyncOldOff},
    {"clear + settle; changed-pixel reverse then 2F white restore; sync; ON", GhostAction::TargetCompOn},
    {"clear + settle; changed-pixel reverse then 2F white restore; sync; POF", GhostAction::TargetCompOff},
    {"clear + settle; sync; POF 30s, PON without DRF, ON 30s", GhostAction::PowerCycleNoRefresh},
};
constexpr size_t GHOST_MODE_COUNT = sizeof(GHOST_MODES) / sizeof(GHOST_MODES[0]);
static_assert(GHOST_MODE_COUNT == 9, "serial menu is intentionally 1..9");

uint8_t* ghost_pattern = nullptr;
uint8_t* ghost_inverse = nullptr;
uint8_t* ghost_white = nullptr;
size_t ghost_next_mode = 0;
const char* ghost_dtm1_state = "UNKNOWN";
const char* ghost_dtm2_state = "UNKNOWN";

uint32_t ghost_hash(const uint8_t* data, size_t length) {
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < length; ++i) {
    hash ^= data[i];
    hash *= 16777619u;
  }
  return hash;
}

void ghost_render_pattern(uint8_t* destination) {
  memset(destination, 0xFF, GHOST_VISIBLE_BYTES);
  for (uint16_t y = 0; y < PANEL_HEIGHT; ++y) {
    uint8_t* row = destination + static_cast<size_t>(y) * ROW_BYTES;
    for (uint16_t x = 0; x < PANEL_WIDTH; ++x) {
      bool black = false;
      if (x < 200) {
        black = ((((x / 20U) + (y / 120U)) & 1U) == 0U);
      } else if (x < 400) {
        black = ((((x - 200U) / 32U) + (y / 24U)) & 1U) == 0U;
      } else if (x < 600) {
        black = ((((x - 400U) + y * 2U) / 28U) & 1U) == 0U;
      } else {
        const uint16_t lx = x - 600U;
        const bool outer = lx < 12U || lx >= 188U || y < 12U || y >= PANEL_HEIGHT - 12U;
        const bool cross = (lx >= 86U && lx < 114U) || (y >= 212U && y < 268U);
        const bool blocks = (((lx / 50U) + (y / 60U)) & 1U) == 0U;
        black = outer || (cross ^ blocks);
      }

      // Asymmetric fiducials make row/column shifts and old-image echoes easy
      // to identify.  The narrow white spine remains white in the burn image.
      if ((y >= 38U && y < 44U) || (y >= 118U && y < 126U) ||
          (y >= 238U && y < 246U) || (y >= 358U && y < 366U) ||
          (y >= 438U && y < 446U)) {
        black = ((x / 64U) & 1U) == 0U;
      }
      if (x >= 390U && x < 410U) black = false;
      if (x < 80U && y < 80U) black = true;
      if (x >= 720U && y < 80U) black = false;
      if (x >= 720U && y >= 400U) black = true;
