#ifndef LOGO_H
#define LOGO_H

#include <stdint.h>

#define LOGO_WIDTH 256
#define LOGO_HEIGHT 256
#define LOGO_COLOR 0x3F47CC

#define BOOT_PHASE_FADE_IN   0
#define BOOT_PHASE_FILLING   1
#define BOOT_PHASE_FADE_OUT  2

extern volatile uint8_t boot_phase;
extern volatile uint8_t boot_progress;

void show_boot_logo(void);
void draw_logo(uint32_t x, uint32_t y);
void draw_test_logo(uint32_t x, uint32_t y);
void draw_boot_progress_bar(uint32_t x, uint32_t y, uint8_t percent);
void update_boot_progress(void);
void fade_out_boot_logo(void);

#endif