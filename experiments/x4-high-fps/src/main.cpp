#ifdef X4_HIGH_FPS_LAB
// Keep every proven v0.1.5 primitive and hard-abort path in this translation
// unit, but replace its menu with the focused directional-balance matrix.
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

#include "../source/focused_contrast_part_0.inc"
#include "../source/focused_contrast_part_1.inc"
#include "../source/focused_contrast_part_2.inc"
#include "../source/focused_contrast_part_3.inc"
#include "../source/focused_contrast_part_4.inc"
#include "../source/focused_contrast_part_5.inc"
#endif  // X4_HIGH_FPS_LAB
