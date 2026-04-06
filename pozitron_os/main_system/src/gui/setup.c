#include "gui/setup.h"
#include "gui/gui.h"
#include "drivers/vesa.h"
#include "drivers/serial.h"
#include "drivers/timer.h"
#include "fs/vfs.h"
#include "kernel/memory.h"
#include "lib/string.h"
#include "lib/mini_printf.h"

static Window* setup_window = NULL;
static Widget* pcname_input = NULL;
static Widget* username_input = NULL;
static Widget* password_input = NULL;
static Widget* status_label = NULL;
static Widget* finish_button = NULL;
static uint8_t setup_complete = 0;

static void setup_disable_controls(void) {
    if (pcname_input) pcname_input->enabled = 0;
    if (username_input) username_input->enabled = 0;
    if (password_input) password_input->enabled = 0;
    if (finish_button) finish_button->enabled = 0;
}

static void setup_finish_callback(Widget* button, void* userdata) {
    (void)button;
    (void)userdata;
    
    if (setup_complete) return;
    
    char* pcname = wg_input_get_text(pcname_input);
    char* username = wg_input_get_text(username_input);
    char* password = wg_input_get_text(password_input);
    
    if (!pcname || !username || strlen(pcname) == 0 || strlen(username) == 0) {
        if (status_label) {
            wg_set_text(status_label, "  Computer name and username required!");
        }
        return;
    }
    
    serial_puts("[SETUP] Creating user: ");
    serial_puts(username);
    serial_puts("\n");
    
    setup_disable_controls();
    
    if (status_label) {
        wg_set_text(status_label, "  Creating user directory...");
    }
    gui_render();
    if (vesa_is_double_buffer_enabled()) vesa_swap_buffers();
    
    char user_path[256];
    char template_path[256];
    
    sprintf(user_path, "/users/%s", username);
    sprintf(template_path, "/users/Default");
    
    if (vfs_mkdir_p(user_path, 0755) != 0) {
        serial_puts("[SETUP] Failed to create user directory\n");
        if (status_label) wg_set_text(status_label, "  ERROR: Failed to create user directory!");
        return;
    }
    
    if (status_label) {
        wg_set_text(status_label, "  Copying user template...");
    }
    gui_render();
    if (vesa_is_double_buffer_enabled()) vesa_swap_buffers();
    
    if (vfs_copy_template(template_path, user_path) != 0) {
        serial_puts("[SETUP] Failed to copy template\n");
        if (status_label) wg_set_text(status_label, "  ERROR: Failed to copy user template!");
        return;
    }
    
    if (status_label) {
        wg_set_text(status_label, "  Creating system directories...");
    }
    gui_render();
    if (vesa_is_double_buffer_enabled()) vesa_swap_buffers();
    
    if (vfs_mkdir_p("/etc", 0755) == 0) {
        char passwd_line[256];
        if (password && strlen(password) > 0) {
            sprintf(passwd_line, "%s:%s:1000:1000:%s:%s:/sys32/bin/desktop\n",
                    username, password, username, user_path);
        } else {
            sprintf(passwd_line, "%s::1000:1000:%s:%s:/sys32/bin/desktop\n",
                    username, username, user_path);
        }
        
        struct vfs_file* passwd_file;
        if (vfs_open("/etc/passwd", FS_O_WRONLY | FS_O_CREAT, &passwd_file) == 0) {
            uint32_t written;
            vfs_write(passwd_file, passwd_line, strlen(passwd_line), &written);
            vfs_close(passwd_file);
            serial_puts("[SETUP] /etc/passwd created\n");
        }
    }
    
    if (status_label) {
        wg_set_text(status_label, "  Updating system configuration...");
    }
    gui_render();
    if (vesa_is_double_buffer_enabled()) vesa_swap_buffers();
    
    char conf_path[] = "/PozitronOS/Sys32/confs/sys_conf.conf";
    char* conf_content = vfs_read_config(conf_path, NULL);
    
    struct vfs_file* conf_file = NULL;
    char new_content[512];
    
    if (conf_content) {
        char* fstart_pos = strstr(conf_content, "fstart=1");
        if (fstart_pos) {
            fstart_pos[6] = '0';
        }
        sprintf(new_content, "%s\npcname=%s\n", conf_content, pcname);
        kfree(conf_content);
    } else {
        sprintf(new_content, "fstart=0\npcname=%s\n", pcname);
    }
    
    if (vfs_open(conf_path, FS_O_WRONLY | FS_O_CREAT | FS_O_TRUNC, &conf_file) == 0) {
        uint32_t written;
        vfs_write(conf_file, new_content, strlen(new_content), &written);
        vfs_close(conf_file);
        serial_puts("[SETUP] System configuration saved\n");
    }
    
    setup_complete = 1;
    
    if (status_label) {
        wg_set_text(status_label, "  Setup complete! You may now reboot.");
    }
    
    if (finish_button) {
        wg_set_text(finish_button, "Close");
        finish_button->enabled = 1;
        finish_button->on_click = NULL;
    }
    
    serial_puts("[SETUP] First boot setup completed successfully\n");
    serial_puts("[SETUP] Please reboot to continue\n");
}

void show_setup_window(void) {
    uint32_t screen_width = vesa_get_width();
    uint32_t screen_height = vesa_get_height();
    
    uint32_t win_width = 450;
    uint32_t win_height = 380;
    uint32_t win_x = (screen_width - win_width) / 2;
    uint32_t win_y = (screen_height - win_height) / 2;
    
    setup_window = wm_create_window("PozitronOS Setup",
                                    win_x, win_y, win_width, win_height,
                                    WINDOW_HAS_TITLE);
    
    if (!setup_window) {
        serial_puts("[SETUP] Failed to create window\n");
        return;
    }
    
    setup_window->closable = 0;
    setup_window->minimizable = 0;
    setup_window->maximizable = 0;
    setup_window->movable = 0;
    setup_window->in_taskbar = 0;
    
    wg_create_label(setup_window, "       Welcome to PozitronOS!", 0.25f, 0.08f);
    wg_create_label(setup_window, "   Please configure your system:", 0.22f, 0.16f);
    
    wg_create_label(setup_window, "   Computer name:", 0.08f, 0.28f);
    pcname_input = wg_create_input(setup_window, 0.45f, 0.26f, 0.45f, 0.07f, "PozitronPC");
    
    wg_create_label(setup_window, "   Username:", 0.08f, 0.40f);
    username_input = wg_create_input(setup_window, 0.45f, 0.38f, 0.45f, 0.07f, "");
    
    wg_create_label(setup_window, "   Password:", 0.08f, 0.52f);
    password_input = wg_create_input(setup_window, 0.45f, 0.50f, 0.45f, 0.07f, "");
    
    // Функция wg_input_set_password_mode должна быть объявлена в gui.h
    // Если её нет, временно закомментируй или добавь в gui.h:
    // void wg_input_set_password_mode(Widget* input, uint8_t enabled);
    // wg_input_set_password_mode(password_input, 1);
    
    wg_create_label(setup_window, "   (leave empty for no password)", 0.18f, 0.60f);
    
    status_label = wg_create_label(setup_window, "", 0.15f, 0.70f);
    
    finish_button = wg_create_button(setup_window, "Finish", 0.35f, 0.82f, 0.3f, 0.10f,
                                     setup_finish_callback, NULL);
    
    serial_puts("[SETUP] Window created\n");
}

uint8_t is_first_boot(void) {
    char* fstart = vfs_read_config("/PozitronOS/Sys32/confs/sys_conf.conf", "fstart");
    if (fstart) {
        uint8_t result = (strcmp(fstart, "1") == 0);
        kfree(fstart);
        return result;
    }
    return 1;
}