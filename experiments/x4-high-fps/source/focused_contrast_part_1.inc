     FocusPattern::Teleport, FocusExecution::Combined, 1, 1, 1, RAIL_14V0, RAIL_14V0, 0x22, 0xD7, 64, 1, 0, 20},
    {"1+2 plus one black->white GND hold / 14V", FocusWaveform::WhiteGndHold,
     FocusPattern::Teleport, FocusExecution::Combined, 1, 2, 1, RAIL_14V0, RAIL_14V0, 0x22, 0xD7, 64, 1, 0, 22},

    {"1+2 / VDH 15V, VDL 14V", FocusWaveform::AsymmetricComplementary,
     FocusPattern::Teleport, FocusExecution::Combined, 1, 2, 0, RAIL_15V0, RAIL_14V0, 0x22, 0xD7, 64, 1, 0, 24},
    {"1+2 / VDH 14V, VDL 15V", FocusWaveform::AsymmetricComplementary,
     FocusPattern::Teleport, FocusExecution::Combined, 1, 2, 0, RAIL_14V0, RAIL_15V0, 0x22, 0xD7, 64, 1, 0, 24},
    {"1+2 / symmetric 15V", FocusWaveform::AsymmetricComplementary,
     FocusPattern::Teleport, FocusExecution::Combined, 1, 2, 0, RAIL_15V0, RAIL_15V0, 0x22, 0xD7, 64, 1, 0, 24},
    {"1+3 / symmetric 15V", FocusWaveform::AsymmetricComplementary,
     FocusPattern::Teleport, FocusExecution::Combined, 1, 3, 0, RAIL_15V0, RAIL_15V0, 0x22, 0xD7, 64, 1, 0, 22},
    {"2+2 / symmetric 15V", FocusWaveform::AsymmetricComplementary,
     FocusPattern::Teleport, FocusExecution::Combined, 2, 2, 0, RAIL_15V0, RAIL_15V0, 0x22, 0xD7, 64, 1, 0, 22},

    {"400px density / combined 1+2", FocusWaveform::AsymmetricComplementary,
     FocusPattern::Density, FocusExecution::Combined, 1, 2, 0, RAIL_14V0, RAIL_14V0, 0x22, 0xD7, 400, 1, 0, 18},
    {"400px density / two 200px segments / 1+2", FocusWaveform::AsymmetricComplementary,
     FocusPattern::Density, FocusExecution::Segmented, 1, 2, 0, RAIL_14V0, RAIL_14V0, 0x22, 0xD7, 400, 2, 0, 14},
    {"800px density / combined 1+2", FocusWaveform::AsymmetricComplementary,
     FocusPattern::Density, FocusExecution::Combined, 1, 2, 0, RAIL_14V0, RAIL_14V0, 0x22, 0xD7, 800, 1, 0, 14},
    {"800px density / two 400px segments / 1+2", FocusWaveform::AsymmetricComplementary,
     FocusPattern::Density, FocusExecution::Segmented, 1, 2, 0, RAIL_14V0, RAIL_14V0, 0x22, 0xD7, 800, 2, 0, 12},
    {"800px density / four 200px segments / 1+2", FocusWaveform::AsymmetricComplementary,
     FocusPattern::Density, FocusExecution::Segmented, 1, 2, 0, RAIL_14V0, RAIL_14V0, 0x22, 0xD7, 800, 4, 0, 10},
    {"800px density / four 200px segments + 2ms recharge", FocusWaveform::AsymmetricComplementary,
     FocusPattern::Density, FocusExecution::Segmented, 1, 2, 0, RAIL_14V0, RAIL_14V0, 0x22, 0xD7, 800, 4, 2, 10},
    {"400px density / split directional DRFs / no gap", FocusWaveform::AsymmetricComplementary,
     FocusPattern::Density, FocusExecution::SplitDirections, 1, 2, 0, RAIL_14V0, RAIL_14V0, 0x22, 0xD7, 400, 1, 0, 14},
    {"400px density / split directional DRFs + 3ms recharge", FocusWaveform::AsymmetricComplementary,
     FocusPattern::Density, FocusExecution::SplitDirections, 1, 2, 0, RAIL_14V0, RAIL_14V0, 0x22, 0xD7, 400, 1, 3, 14},

    {"default TCON + 2-Hsync CDI / 1+2", FocusWaveform::AsymmetricComplementary,
     FocusPattern::Teleport, FocusExecution::Combined, 1, 2, 0, RAIL_14V0, RAIL_14V0, 0x22, 0xDF, 64, 1, 0, 24},
    {"minimum TCON + default CDI / 1+2", FocusWaveform::AsymmetricComplementary,
     FocusPattern::Teleport, FocusExecution::Combined, 1, 2, 0, RAIL_14V0, RAIL_14V0, 0x00, 0xD7, 64, 1, 0, 24},
    {"default TCON + 17-Hsync CDI / 1+2", FocusWaveform::AsymmetricComplementary,
     FocusPattern::Teleport, FocusExecution::Combined, 1, 2, 0, RAIL_14V0, RAIL_14V0, 0x22, 0xD0, 64, 1, 0, 20},
    {"long TCON + default CDI / 1+2", FocusWaveform::AsymmetricComplementary,
     FocusPattern::Teleport, FocusExecution::Combined, 1, 2, 0, RAIL_14V0, RAIL_14V0, 0x44, 0xD7, 64, 1, 0, 20},
};
constexpr size_t FOCUSED_MODE_COUNT = sizeof(FOCUSED_MODES) / sizeof(FOCUSED_MODES[0]);
static_assert(FOCUSED_MODE_COUNT == 25, "focused key map assumes 25 modes");

struct FocusRect {
  uint16_t x;
  uint16_t width;
};

uint8_t *focused_cache = nullptr;
size_t focused_cache_count = 0;
size_t focused_next_mode = 0;
uint32_t focused_current_frame = 0;

uint16_t focused_rail_mv(uint8_t code) {
  return static_cast<uint16_t>(2400U + static_cast<uint16_t>(code & 0x3FU) * 200U);
}

inline void focused_set_pixel(uint8_t *row, uint16_t x, bool black) {
  const uint8_t mask = static_cast<uint8_t>(0x80U >> (x & 7U));
  if (black) row[x >> 3] &= static_cast<uint8_t>(~mask);
  else row[x >> 3] |= mask;
}

void focused_render_frame(uint8_t *dst, uint32_t frame, const FocusMode &mode) {
  memset(dst, 0xFF, FOCUSED_STRIDE);
  for (uint16_t y = FOCUSED_H / 2; y < FOCUSED_H; ++y)
    memset(dst + static_cast<size_t>(y) * ROW_BYTES, 0x00, ROW_BYTES);

  if (mode.pattern == FocusPattern::Density) {
    const uint16_t width = std::min<uint16_t>(mode.transition_width, PANEL_WIDTH);
    const uint16_t left = static_cast<uint16_t>((PANEL_WIDTH - width) / 2U);
    if ((frame & 1U) != 0) {
      for (uint16_t y = 6; y < FOCUSED_H / 2 - 6; ++y) {
        uint8_t *row = dst + static_cast<size_t>(y) * ROW_BYTES;
        for (uint16_t x = left; x < left + width; ++x) focused_set_pixel(row, x, true);
      }
      for (uint16_t y = FOCUSED_H / 2 + 6; y < FOCUSED_H - 6; ++y) {
        uint8_t *row = dst + static_cast<size_t>(y) * ROW_BYTES;
        for (uint16_t x = left; x < left + width; ++x) focused_set_pixel(row, x, false);
      }
    }
    return;
  }

  const uint16_t left = focused_position_x(frame);
  for (uint16_t y = 6; y < 6 + FOCUSED_TARGET_H; ++y) {
    uint8_t *row = dst + static_cast<size_t>(y) * ROW_BYTES;
    for (uint16_t x = left; x < left + FOCUSED_TARGET_W; ++x) focused_set_pixel(row, x, true);
  }
  for (uint16_t y = 46; y < 46 + FOCUSED_TARGET_H; ++y) {
    uint8_t *row = dst + static_cast<size_t>(y) * ROW_BYTES;
    for (uint16_t x = left; x < left + FOCUSED_TARGET_W; ++x) focused_set_pixel(row, x, false);
  }
}

void focused_release_cache() {
  if (focused_cache) heap_caps_free(focused_cache);
  focused_cache = nullptr;
  focused_cache_count = 0;
}

bool focused_prepare_cache(const FocusMode &mode) {
  focused_release_cache();
  focused_cache_count = static_cast<size_t>(mode.logical_frames) + 1U;
  const size_t bytes = FOCUSED_STRIDE * focused_cache_count;
  focused_cache = static_cast<uint8_t *>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!focused_cache) focused_cache = static_cast<uint8_t *>(heap_caps_malloc(bytes, MALLOC_CAP_8BIT));
  if (!focused_cache) {
    Serial.printf("[X4LAB] focused cache allocation failed bytes=%lu\n", static_cast<unsigned long>(bytes));
