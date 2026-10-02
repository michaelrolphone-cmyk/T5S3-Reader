#pragma once
/* Temporary X4 Pro main screen. Uses the same SSD1677 sequence as x4pro-panel.
 * HomeActivity still targets the T5S3 canvas, so this is the boot screen until
 * that renderer consumes display.output. */
void x4BootToMainScreen();
void x4BootLoop();
