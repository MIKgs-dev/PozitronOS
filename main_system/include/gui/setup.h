#ifndef GUI_SETUP_H
#define GUI_SETUP_H

#include <stdint.h>

void show_setup_window(void);
uint8_t is_first_boot(void);
uint8_t is_setup_complete(void);
char* get_last_username(void);
uint8_t is_autologin_enabled(void);

#endif