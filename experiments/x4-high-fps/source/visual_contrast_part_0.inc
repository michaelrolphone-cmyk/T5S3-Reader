/*
 * X4LAB v0.1.8 visual contrast and waveform characterization matrix.
 *
 * The electrical hard-abort ISR, controller identity gate, normal 800x600
 * geometry, +120 visible gate offset, 20 MHz transport, bounded BUSY waits,
 * and OTP clean baseline are inherited verbatim from stable X4LAB v0.1.5.
 *
 * This matrix intentionally explores documented UC8179/UC8279 controls that
 * can be judged by eye: transition-selective waveform shape, pulse frequency,
 * source rail amplitude, VCOM DC, transition density, TCON/CDI timing, and
 * grayscale stress.  It never changes TRES/GSST geometry or panel SPI speed.
 */

namespace {

constexpr const char *VISUAL_LAB_VERSION = "0.1.8";
constexpr uint16_t VISUAL_Y = 200;
constexpr uint16_t VISUAL_H = 80;
constexpr size_t VISUAL_STRIDE = static_cast<size_t>(VISUAL_H) * ROW_BYTES;
constexpr uint8_t V_CMD_POWER_SETTING = 0x01;
constexpr uint8_t V_CMD_POWER_OFF = 0x02;
constexpr uint8_t V_CMD_TCON = 0x60;
constexpr uint8_t V_CMD_VCOM_DC = 0x82;
constexpr uint8_t VCOM_FACTORY = 0xFF;
constexpr uint8_t LEVEL_GND = 0x00;
constexpr uint8_t LEVEL_HIGH = 0x40;
constexpr uint8_t LEVEL_LOW = 0x80;
constexpr uint8_t LEVEL_VDHR = 0xC0;

// Codes from the documented 0.2 V VDH/VDL ladder.
constexpr uint8_t RAIL_12V6 = 0x33;
constexpr uint8_t RAIL_14V0 = 0x3A;
constexpr uint8_t RAIL_15V0 = 0x3F;
constexpr uint8_t VDHR_3V0 = 0x03;
constexpr uint8_t VDHR_8V0 = 0x1C;

// VDCS code N corresponds to -(0.10 + 0.05*N) V.
constexpr uint8_t VCOM_N1V5 = 0x1C;
constexpr uint8_t VCOM_N2V0 = 0x26;
constexpr uint8_t VCOM_N2V5 = 0x30;
constexpr uint8_t VCOM_N3V0 = 0x3A;

enum class VisualWaveform : uint8_t {
  AbsoluteTwo,
  DifferentialTwo,
  Complementary,
  ComplementaryDouble,
  KickVssHold,
  KickVdhrHold,
  ReverseActivation,
};

enum class VisualPattern : uint8_t {
  Teleport,
  Density,
  TemporalBayer,
  FineEdges,
};

struct VisualMode {
  const char *name;
  VisualWaveform waveform;
  VisualPattern pattern;
  uint8_t pll;
  uint8_t vdh;
  uint8_t vdl;
  uint8_t vdhr;
  uint8_t vcom;
  uint8_t tcon;
  uint8_t cdi;
  uint16_t transition_width;
  uint16_t frames;
};

// Modes 1-7: waveform architecture. 8-10: frequency. 11-14: rail
// amplitude/direction. 15-18: VCOM. 19-21: transition-density load.
// 22-23: source/gate timing. 24-25: dither and fine-edge stress.
constexpr VisualMode VISUAL_MODES[] = {
    {"absolute A2 2-frame reference / 200Hz / 14V", VisualWaveform::AbsoluteTwo,
     VisualPattern::Teleport, 0x0F, RAIL_14V0, RAIL_14V0, VDHR_3V0, VCOM_FACTORY, 0x22, 0xD7, 64, 32},
    {"differential A2 2-frame / VCOM-DC reference / 200Hz / 14V", VisualWaveform::DifferentialTwo,
     VisualPattern::Teleport, 0x0F, RAIL_14V0, RAIL_14V0, VDHR_3V0, VCOM_FACTORY, 0x22, 0xD7, 64, 32},
    {"complementary directional overdrive 1+1 / 200Hz / 14V", VisualWaveform::Complementary,
     VisualPattern::Teleport, 0x0F, RAIL_14V0, RAIL_14V0, VDHR_3V0, VCOM_FACTORY, 0x22, 0xD7, 64, 32},
    {"complementary directional overdrive 2+2 / 200Hz / 14V", VisualWaveform::ComplementaryDouble,
     VisualPattern::Teleport, 0x0F, RAIL_14V0, RAIL_14V0, VDHR_3V0, VCOM_FACTORY, 0x22, 0xD7, 64, 24},
    {"maximum kick + symmetric GND hold / 200Hz / 14V", VisualWaveform::KickVssHold,
     VisualPattern::Teleport, 0x0F, RAIL_14V0, RAIL_14V0, VDHR_3V0, VCOM_FACTORY, 0x22, 0xD7, 64, 24},
    {"maximum kick + VDHR hold / 200Hz / 14V / VDHR=8V", VisualWaveform::KickVdhrHold,
     VisualPattern::Teleport, 0x0F, RAIL_14V0, RAIL_14V0, VDHR_8V0, VCOM_FACTORY, 0x22, 0xD7, 64, 24},
    {"short reverse activation + maximum directional kick / 200Hz / 14V", VisualWaveform::ReverseActivation,
     VisualPattern::Teleport, 0x0F, RAIL_14V0, RAIL_14V0, VDHR_3V0, VCOM_FACTORY, 0x22, 0xD7, 64, 24},

    {"complementary directional / 100Hz / 14V", VisualWaveform::Complementary,
     VisualPattern::Teleport, 0x0B, RAIL_14V0, RAIL_14V0, VDHR_3V0, VCOM_FACTORY, 0x22, 0xD7, 64, 24},
    {"complementary directional / 130Hz / 14V", VisualWaveform::Complementary,
     VisualPattern::Teleport, 0x0D, RAIL_14V0, RAIL_14V0, VDHR_3V0, VCOM_FACTORY, 0x22, 0xD7, 64, 28},
    {"complementary directional / 150Hz / 14V", VisualWaveform::Complementary,
     VisualPattern::Teleport, 0x0E, RAIL_14V0, RAIL_14V0, VDHR_3V0, VCOM_FACTORY, 0x22, 0xD7, 64, 28},

    {"complementary directional / 200Hz / symmetric 12.6V", VisualWaveform::Complementary,
     VisualPattern::Teleport, 0x0F, RAIL_12V6, RAIL_12V6, VDHR_3V0, VCOM_FACTORY, 0x22, 0xD7, 64, 32},
    {"complementary directional / 200Hz / symmetric 15V", VisualWaveform::Complementary,
     VisualPattern::Teleport, 0x0F, RAIL_15V0, RAIL_15V0, VDHR_3V0, VCOM_FACTORY, 0x22, 0xD7, 64, 32},
    {"complementary directional / VDH 15V, VDL 14V", VisualWaveform::Complementary,
