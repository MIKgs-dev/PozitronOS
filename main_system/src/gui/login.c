#include "gui/login.h"
#include "gui/gui.h"
#include "drivers/vesa.h"
#include "drivers/serial.h"
#include "drivers/timer.h"
#include "kernel/auth.h"
#include "kernel/memory.h"
#include "lib/string.h"
#include "lib/mini_printf.h"

static Window* login_window = NULL;
static Widget* username_input = NULL;
static Widget* password_input = NULL;
static Widget* login_button = NULL;
static Widget* error_label = NULL;
static Widget* hint_label = NULL;
static uint8_t login_locked = 0;
static uint8_t attempts = 0;
static char current_hint[256];
static char login_result[128];

static void do_login(void);

static void on_username_key(Widget* w, event_t* ev) {
    if (ev->type == EVENT_KEY_PRESS && ev->key.scancode == 0x1C) {
        gui_set_focus(password_input);
    }
}

static void on_password_key(Widget* w, event_t* ev) {
    if (ev->type == EVENT_KEY_PRESS && ev->key.scancode == 0x1C) {
        do_login();
    }
}

static void do_login(void) {
    if (login_locked) return;
    
    char* username = wg_input_get_text(username_input);
    char* password = wg_input_get_text(password_input);
    
    if (!username || strlen(username) == 0) {
        wg_set_text(error_label, "Please enter username");
        return;
    }
    
    wg_set_text(login_button, "Verifying...");
    gui_render();
    if (vesa_is_double_buffer_enabled()) vesa_swap_buffers();
    
    auth_status_t status = auth_verify_with_lockout(username, password);
    
    if (status == AUTH_OK) {
        serial_puts("[LOGIN] Login successful\n");
        if (username && strlen(username) > 0) {
            strncpy(login_result, username, sizeof(login_result) - 1);
            login_result[sizeof(login_result) - 1] = '\0';
        } else {
            login_result[0] = '\0';
        }
        wm_destroy_window(login_window);
        login_window = NULL;
        return;
    }
    
    attempts++;
    wg_set_text(login_button, "Login");
    
    switch (status) {
        case AUTH_ERR_USER_NOT_FOUND:
            wg_set_text(error_label, "User not found");
            break;
        case AUTH_ERR_WRONG_PASSWORD:
            wg_set_text(error_label, "Wrong password");
            char* hint = auth_get_hint(username);
            if (hint && strlen(hint) > 0) {
                sprintf(current_hint, "Hint: %s", hint);
                wg_set_text(hint_label, current_hint);
                kfree(hint);
            }
            break;
        case AUTH_ERR_LOCKED:
            wg_set_text(error_label, "Account locked");
            login_locked = 1;
            break;
        default:
            wg_set_text(error_label, "Login error");
            break;
    }
    
    wg_input_set_text(password_input, "");
    gui_set_focus(password_input);
    
    if (attempts >= 5 && !login_locked) {
        login_locked = 1;
        wg_set_text(error_label, "Account locked. Reboot required.");
        wg_set_text(login_button, "Locked");
    }
}

static void on_login_click(Widget* btn, void* userdata) {
    (void)btn;
    (void)userdata;
    do_login();
}

char* show_login_screen(const char* autologin_username) {
    uint32_t screen_width = vesa_get_width();
    uint32_t screen_height = vesa_get_height();
    
    uint32_t win_width = 400;
    uint32_t win_height = 260;
    uint32_t win_x = (screen_width - win_width) / 2;
    uint32_t win_y = (screen_height - win_height) / 2;
    
    login_result[0] = '\0';
    
    login_window = wm_create_window("PozitronOS Login",
                                    win_x, win_y, win_width, win_height,
                                    WINDOW_HAS_TITLE);
    
    if (!login_window) {
        serial_puts("[LOGIN] Failed to create window\n");
        return NULL;
    }
    
    login_window->closable = 0;
    login_window->minimizable = 0;
    login_window->maximizable = 0;
    login_window->movable = 0;
    login_window->in_taskbar = 0;
    
    wg_create_label(login_window, "Username:", 0.15f, 0.30f);
    username_input = wg_create_input(login_window, 0.40f, 0.28f, 0.50f, 0.08f, "");
    
    wg_create_label(login_window, "Password:", 0.15f, 0.45f);
    password_input = wg_create_input(login_window, 0.40f, 0.43f, 0.50f, 0.08f, "");
    wg_input_set_password_mode(password_input, 1);
    
    hint_label = wg_create_label_aligned(login_window, "", 0.5f, 0.55f, TEXT_ALIGN_CENTER);
    error_label = wg_create_label_aligned(login_window, "", 0.5f, 0.63f, TEXT_ALIGN_CENTER);
    
    login_button = wg_create_button(login_window, "Login", 
                                    0.35f, 0.75f, 0.30f, 0.10f,
                                    on_login_click, NULL);
    
    username_input->handle_event = on_username_key;
    password_input->handle_event = on_password_key;
    
    if (autologin_username && strlen(autologin_username) > 0) {
        wg_input_set_text(username_input, autologin_username);
        gui_set_focus(password_input);
    } else {
        gui_set_focus(username_input);
    }
    
    serial_puts("[LOGIN] Window created\n");
    
    while (login_window && login_window->id != 0) {
        event_t event;
        while (event_poll(&event)) {
            gui_handle_event(&event);
        }
        
        vesa_hide_cursor();
        if (vesa_is_background_cached()) {
            vesa_restore_background_dirty();
        }
        gui_render();
        vesa_show_cursor();
        vesa_cursor_update();
        
        if (vesa_is_double_buffer_enabled()) {
            vesa_swap_buffers();
        }
        
        asm volatile("hlt");
    }
    
    if (login_result[0]) {
        char* result = (char*)kmalloc(strlen(login_result) + 1);
        if (result) strcpy(result, login_result);
        return result;
    }
    
    return NULL;
}