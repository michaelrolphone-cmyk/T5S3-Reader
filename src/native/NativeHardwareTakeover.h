#pragma once

// Firmware-only query used by shared system UI/input. True only while an ELF
// has exclusive display ownership and firmware overlays must not draw.
bool nativeHardwareTakeoverDisplayActive();
