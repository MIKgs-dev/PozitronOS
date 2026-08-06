#ifndef SHUTDOWN_H
#define SHUTDOWN_H

#include "gui/gui.h"

void shutdown_dialog_callback(Widget* button, void* userdata);
void update_shutdown_animation(void);
void render_darken_effect(void);
uint8_t is_shutdown_mode_active(void);
Window* get_shutdown_dialog(void);
void force_reset_shutdown_state(void);

#endif