#ifdef X4_HIGH_FPS_LAB
// Retain the complete, hardware-validated v0.1.5 implementation in this
// translation unit, but do not expose its menu.  The ghosting diagnostic below
// reuses only its proven 20-MHz transport, normal 800x600 geometry, controller
// probe, bounded BUSY waits, and immediate side-button abort path.
#define setup x4lab_v015_setup_unused
#define loop x4lab_v015_loop_unused
#include "../source/lab_part_0.inc"
#include "../source/lab_part_1.inc"
#include "../source/lab_part_2.inc"
#include "../source/lab_part_3.inc"
#include "../source/lab_part_4.inc"
#include "../source/lab_part_5.inc"
#undef loop
#undef setup

#include "../source/ghost_part_0.inc"
#include "../source/ghost_part_1.inc"
#include "../source/ghost_part_2.inc"
#include "../source/ghost_part_3.inc"
#include "../source/ghost_part_4.inc"
#include "../source/ghost_part_5.inc"
#include "../source/ghost_part_6.inc"
#endif  // X4_HIGH_FPS_LAB
