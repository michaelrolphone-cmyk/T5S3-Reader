     VisualPattern::Teleport, 0x0F, RAIL_15V0, RAIL_14V0, VDHR_3V0, VCOM_FACTORY, 0x22, 0xD7, 64, 32},
    {"complementary directional / VDH 14V, VDL 15V", VisualWaveform::Complementary,
     VisualPattern::Teleport, 0x0F, RAIL_14V0, RAIL_15V0, VDHR_3V0, VCOM_FACTORY, 0x22, 0xD7, 64, 32},

    {"complementary / 15V / VCOM DC -1.50V", VisualWaveform::Complementary,
     VisualPattern::Teleport, 0x0F, RAIL_15V0, RAIL_15V0, VDHR_3V0, VCOM_N1V5, 0x22, 0xD7, 64, 32},
    {"complementary / 15V / VCOM DC -2.00V", VisualWaveform::Complementary,
     VisualPattern::Teleport, 0x0F, RAIL_15V0, RAIL_15V0, VDHR_3V0, VCOM_N2V0, 0x22, 0xD7, 64, 32},
    {"complementary / 15V / VCOM DC -2.50V", VisualWaveform::Complementary,
     VisualPattern::Teleport, 0x0F, RAIL_15V0, RAIL_15V0, VDHR_3V0, VCOM_N2V5, 0x22, 0xD7, 64, 32},
    {"complementary / 15V / VCOM DC -3.00V", VisualWaveform::Complementary,
     VisualPattern::Teleport, 0x0F, RAIL_15V0, RAIL_15V0, VDHR_3V0, VCOM_N3V0, 0x22, 0xD7, 64, 32},

    {"transition-density 64px / complementary / 15V", VisualWaveform::Complementary,
     VisualPattern::Density, 0x0F, RAIL_15V0, RAIL_15V0, VDHR_3V0, VCOM_FACTORY, 0x22, 0xD7, 64, 28},
    {"transition-density 400px / complementary / 15V", VisualWaveform::Complementary,
     VisualPattern::Density, 0x0F, RAIL_15V0, RAIL_15V0, VDHR_3V0, VCOM_FACTORY, 0x22, 0xD7, 400, 28},
    {"transition-density 800px / complementary / 15V", VisualWaveform::Complementary,
     VisualPattern::Density, 0x0F, RAIL_15V0, RAIL_15V0, VDHR_3V0, VCOM_FACTORY, 0x22, 0xD7, 800, 24},

    {"short gate/source non-overlap + 2-Hsync CDI / complementary / 15V", VisualWaveform::Complementary,
     VisualPattern::Teleport, 0x0F, RAIL_15V0, RAIL_15V0, VDHR_3V0, VCOM_FACTORY, 0x00, 0xDF, 64, 32},
    {"long gate/source non-overlap + 17-Hsync CDI / complementary / 15V", VisualWaveform::Complementary,
     VisualPattern::Teleport, 0x0F, RAIL_15V0, RAIL_15V0, VDHR_3V0, VCOM_FACTORY, 0x44, 0xD0, 64, 24},

    {"temporal Bayer gray stress / complementary / 15V", VisualWaveform::Complementary,
     VisualPattern::TemporalBayer, 0x0F, RAIL_15V0, RAIL_15V0, VDHR_3V0, VCOM_FACTORY, 0x22, 0xD7, 800, 32},
    {"teleporting fine-edge stress / complementary / 15V", VisualWaveform::Complementary,
     VisualPattern::FineEdges, 0x0F, RAIL_15V0, RAIL_15V0, VDHR_3V0, VCOM_FACTORY, 0x22, 0xD7, 64, 32},
};
constexpr size_t VISUAL_MODE_COUNT = sizeof(VISUAL_MODES) / sizeof(VISUAL_MODES[0]);
static_assert(VISUAL_MODE_COUNT == 25, "visual matrix key map assumes 25 modes");

uint8_t *visual_cache = nullptr;
size_t visual_cache_count = 0;
size_t visual_next_mode = 0;
uint32_t visual_current_frame = 0;

uint16_t rail_mv(uint8_t code) {
  return static_cast<uint16_t>(2400U + static_cast<uint16_t>(code & 0x3FU) * 200U);
}
int16_t vcom_mv(uint8_t code) {
  return code == VCOM_FACTORY ? 0 : static_cast<int16_t>(-100 - static_cast<int16_t>(code & 0x7FU) * 50);
}
uint16_t pll_hz(uint8_t code) {
  static constexpr uint16_t rates[16] = {5, 10, 15, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 130, 150, 200};
  return rates[code & 0x0F];
}

void visual_release_cache() {
  if (visual_cache) heap_caps_free(visual_cache);
  visual_cache = nullptr;
  visual_cache_count = 0;
}

inline void set_pixel(uint8_t *row, uint16_t x, bool black) {
  const uint8_t mask = static_cast<uint8_t>(0x80U >> (x & 7U));
  if (black) row[x >> 3] &= static_cast<uint8_t>(~mask);
  else row[x >> 3] |= mask;
}

void render_visual_frame(uint8_t *dst, uint32_t frame, const VisualMode &mode) {
  memset(dst, 0xFF, VISUAL_STRIDE);
  if (mode.pattern == VisualPattern::TemporalBayer) {
    for (uint16_t y = 0; y < VISUAL_H; ++y) {
      uint8_t *row = dst + static_cast<size_t>(y) * ROW_BYTES;
      const uint16_t visible_y = VISUAL_Y + y;
      for (uint16_t x = 0; x < ROW_BYTES; ++x)
        row[x] = bayer_pattern_byte(frame, visible_y, x, VISUAL_Y);
    }
    return;
  }

  // Two simultaneous, opposite-direction witnesses: top is black-on-white;
  // bottom is white-on-black. Their old and new locations never overlap.
  for (uint16_t y = VISUAL_H / 2; y < VISUAL_H; ++y)
    memset(dst + static_cast<size_t>(y) * ROW_BYTES, 0x00, ROW_BYTES);

  if (mode.pattern == VisualPattern::Density) {
    const uint16_t width = std::min<uint16_t>(mode.transition_width, PANEL_WIDTH);
    const uint16_t left = static_cast<uint16_t>((PANEL_WIDTH - width) / 2U);
    const bool active = (frame & 1U) != 0;
    if (active) {
      for (uint16_t y = 4; y < VISUAL_H / 2 - 4; ++y) {
        uint8_t *row = dst + static_cast<size_t>(y) * ROW_BYTES;
        for (uint16_t x = left; x < left + width; ++x) set_pixel(row, x, true);
      }
      for (uint16_t y = VISUAL_H / 2 + 4; y < VISUAL_H - 4; ++y) {
        uint8_t *row = dst + static_cast<size_t>(y) * ROW_BYTES;
        for (uint16_t x = left; x < left + width; ++x) set_pixel(row, x, false);
      }
    }
    return;
  }

  static constexpr uint16_t POSITIONS[4] = {40, 240, 440, 640};
  const uint16_t left = POSITIONS[frame & 3U];
  const uint16_t square_w = 56;
  const uint16_t top0 = 6;
  const uint16_t top1 = 46;
