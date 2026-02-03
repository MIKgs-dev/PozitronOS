#ifndef SHUTDOWN_H
#define SHUTDOWN_H

#include "gui.h"

// Функции для системы выключения
void shutdown_dialog_callback(Widget* button, void* userdata);
uint8_t is_shutdown_mode_active(void);
void render_darken_effect(void);
void update_shutdown_animation(void);
Window* get_shutdown_dialog(void);
uint8_t get_shutdown_state(void);

#endif