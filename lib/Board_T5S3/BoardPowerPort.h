#pragma once

// Legacy board/UI compatibility boundary. All BQ25896 register access, charge
// configuration, OTG and BATFET shutdown are delegated to the installed ELF.
// The board library must not know the provider identity, bus address or ABI.
namespace BoardT5S3 { struct BatteryState; }
namespace BoardPowerPort {
bool configure();
bool read(BoardT5S3::BatteryState* state);
bool externalPower(bool* connected);
bool shutdown();
}
