/*
 * X4LAB v0.1.9 focused directional-balance and transition-load matrix.
 *
 * Findings from the v0.1.8 visual matrix:
 *   - complementary 2+2 was the strongest moving witness;
 *   - the maximum kick plus GND hold was visible but weaker;
 *   - large negative VCOM offsets darkened the complete display;
 *   - broad transition density collapsed white erasure toward gray;
 *   - short timing remained active while the combined long timing stopped motion.
 *
 * This matrix therefore holds factory VCOM and documented 200-Hz operation,
 * narrows active windows to their actual changed region, and isolates:
 *   1. asymmetric white->black versus black->white pulse counts;
 *   2. source-rail contribution to the weak black->white direction;
 *   3. horizontal segmentation and rail-recharge gaps under broad load;
 *   4. separate directional DRFs; and
 *   5. TCON and CDI variables independently.
 *
 * All stable v0.1.5 geometry, 20-MHz transport, controller identity checks,
 * bounded BUSY waits, and side-button electrical hard aborts are inherited.
 */

namespace {

constexpr const char *FOCUSED_LAB_VERSION = "0.1.9";
constexpr uint16_t FOCUSED_Y = 200;
constexpr uint16_t FOCUSED_H = 80;
constexpr uint16_t FOCUSED_TARGET_W = 64;
constexpr uint16_t FOCUSED_TARGET_H = 28;
constexpr size_t FOCUSED_STRIDE = static_cast<size_t>(FOCUSED_H) * ROW_BYTES;
constexpr uint8_t F_CMD_POWER_SETTING = 0x01;
constexpr uint8_t F_CMD_POWER_OFF = 0x02;
constexpr uint8_t F_CMD_TCON = 0x60;
constexpr uint8_t LEVEL_GND = 0x00;
constexpr uint8_t LEVEL_HIGH = 0x40;
constexpr uint8_t LEVEL_LOW = 0x80;
constexpr uint8_t RAIL_14V0 = 0x3A;
constexpr uint8_t RAIL_15V0 = 0x3F;
constexpr uint8_t VDHR_3V0 = 0x03;

// Adjacent positions never overlap. The old/new union is exactly 160 pixels,
// so the teleport modes use a realistic narrow changed window rather than an
// 800-pixel scan. The path bounces instead of wrapping across the display.
constexpr uint16_t FOCUSED_POSITIONS[] = {80, 176, 272, 368, 464, 560, 656};
constexpr size_t FOCUSED_POSITION_COUNT = sizeof(FOCUSED_POSITIONS) / sizeof(FOCUSED_POSITIONS[0]);

uint16_t focused_position_index(uint32_t frame) {
  const uint32_t period = static_cast<uint32_t>((FOCUSED_POSITION_COUNT - 1U) * 2U);
  const uint32_t phase = period ? frame % period : 0;
  return static_cast<uint16_t>(phase < FOCUSED_POSITION_COUNT ? phase : period - phase);
}
uint16_t focused_position_x(uint32_t frame) {
  return FOCUSED_POSITIONS[focused_position_index(frame)];
}

enum class FocusWaveform : uint8_t {
  AsymmetricComplementary,
  SymmetricKickGndHold,
  WhiteGndHold,
};

enum class FocusPattern : uint8_t {
  Teleport,
  Density,
};

enum class FocusExecution : uint8_t {
  Combined,
  Segmented,
  SplitDirections,
};

enum class FocusPass : uint8_t {
  Combined,
  WhiteToBlackOnly,
  BlackToWhiteOnly,
};

struct FocusMode {
  const char *name;
  FocusWaveform waveform;
  FocusPattern pattern;
  FocusExecution execution;
  uint8_t black_frames;  // white -> black phase count
  uint8_t white_frames;  // black -> white phase count
  uint8_t hold_frames;
  uint8_t vdh;
  uint8_t vdl;
  uint8_t tcon;
  uint8_t cdi;
  uint16_t transition_width;
  uint8_t segments;
  uint8_t gap_ms;
  uint16_t logical_frames;
};

constexpr FocusMode FOCUSED_MODES[] = {
    {"2+2 complementary reference / 14V", FocusWaveform::AsymmetricComplementary,
     FocusPattern::Teleport, FocusExecution::Combined, 2, 2, 0, RAIL_14V0, RAIL_14V0, 0x22, 0xD7, 64, 1, 0, 24},
    {"1+2 asymmetric / 14V", FocusWaveform::AsymmetricComplementary,
     FocusPattern::Teleport, FocusExecution::Combined, 1, 2, 0, RAIL_14V0, RAIL_14V0, 0x22, 0xD7, 64, 1, 0, 28},
    {"1+3 asymmetric / 14V", FocusWaveform::AsymmetricComplementary,
     FocusPattern::Teleport, FocusExecution::Combined, 1, 3, 0, RAIL_14V0, RAIL_14V0, 0x22, 0xD7, 64, 1, 0, 24},
    {"1+4 asymmetric / 14V", FocusWaveform::AsymmetricComplementary,
     FocusPattern::Teleport, FocusExecution::Combined, 1, 4, 0, RAIL_14V0, RAIL_14V0, 0x22, 0xD7, 64, 1, 0, 20},
    {"2+3 asymmetric / 14V", FocusWaveform::AsymmetricComplementary,
     FocusPattern::Teleport, FocusExecution::Combined, 2, 3, 0, RAIL_14V0, RAIL_14V0, 0x22, 0xD7, 64, 1, 0, 22},
    {"2+4 asymmetric / 14V", FocusWaveform::AsymmetricComplementary,
     FocusPattern::Teleport, FocusExecution::Combined, 2, 4, 0, RAIL_14V0, RAIL_14V0, 0x22, 0xD7, 64, 1, 0, 18},
    {"maximum kick + symmetric GND hold reference / 14V", FocusWaveform::SymmetricKickGndHold,
