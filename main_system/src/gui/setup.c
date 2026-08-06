#include "gui/setup.h"
#include "gui/gui.h"
#include "drivers/vesa.h"
#include "drivers/serial.h"
#include "drivers/timer.h"
#include "fs/vfs.h"
#include "kernel/memory.h"
#include "lib/string.h"
#include "lib/mini_printf.h"
#include "gui/login.h"

static Window* setup_window = NULL;
static uint8_t setup_complete = 0;
static uint8_t setup_success = 0;

uint8_t setup_finished = 0;

static char entered_username[64];
static char entered_password[64];
static char entered_pcname[64];
static char entered_password_hint[128];
static uint32_t password_hash;

typedef enum {
    STAGE_WELCOME,
    STAGE_USER_INFO,
    STAGE_PASSWORD,
    STAGE_CONFIRM,
    STAGE_PROGRESS,
    STAGE_COMPLETE
} setup_stage_t;

static setup_stage_t current_stage = STAGE_WELCOME;

static Widget* welcome_next_btn = NULL;
static Widget* userinfo_pcname_input = NULL;
static Widget* userinfo_username_input = NULL;
static Widget* userinfo_next_btn = NULL;
static Widget* password_pass_input = NULL;
static Widget* password_confirm_input = NULL;
static Widget* password_hint_input = NULL;
static Widget* password_back_btn = NULL;
static Widget* password_next_btn = NULL;
static Widget* password_error_label = NULL;
static Widget* confirm_username_label = NULL;
static Widget* confirm_pcname_label = NULL;
static Widget* confirm_password_label = NULL;
static Widget* confirm_hint_label = NULL;
static Widget* confirm_back_btn = NULL;
static Widget* confirm_create_btn = NULL;
static Widget* progress_bar = NULL;
static Widget* progress_label = NULL;
static Widget* complete_continue_btn = NULL;

static void clear_window_widgets(void);
static uint32_t hash_password(const char* password);
static int user_exists(const char* username);
static void update_progress(uint32_t percent, const char* status);
static void do_apply_settings(void);

static void create_welcome_stage(void);
static void create_user_info_stage(void);
static void create_password_stage(void);
static void create_confirm_stage(void);
static void create_progress_stage(void);
static void create_complete_stage(void);

static void on_welcome_next(Widget* btn, void* userdata);
static void on_userinfo_next(Widget* btn, void* userdata);
static void on_password_next(Widget* btn, void* userdata);
static void on_password_back(Widget* btn, void* userdata);
static void on_confirm_back(Widget* btn, void* userdata);
static void on_confirm_create(Widget* btn, void* userdata);
static void on_complete_continue(Widget* btn, void* userdata);

// ============ РЕАЛИЗАЦИИ ============

static void clear_window_widgets(void) {
    if (!setup_window) return;
    
    Widget* w = setup_window->first_widget;
    while (w) {
        Widget* next = w->next;
        wg_destroy_widget(w);
        w = next;
    }
    setup_window->first_widget = NULL;
    setup_window->last_widget = NULL;
}

static uint32_t hash_password(const char* password) {
    uint32_t hash = 5381;
    int c;
    while ((c = *password++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash;
}

static int user_exists(const char* username) {
    struct vfs_file* file;
    if (vfs_open("/etc/passwd", FS_O_RDONLY, &file) != 0) {
        return 0;
    }
    
    char buffer[512];
    uint32_t bytes_read;
    char* content = NULL;
    uint32_t content_size = 0;
    
    while (vfs_read(file, buffer, sizeof(buffer) - 1, &bytes_read) == 0 && bytes_read > 0) {
        buffer[bytes_read] = '\0';
        char* new_content = (char*)kmalloc(content_size + bytes_read + 1);
        if (!new_content) {
            if (content) kfree(content);
            vfs_close(file);
            return 0;
        }
        if (content) {
            memcpy(new_content, content, content_size);
            kfree(content);
        }
        memcpy(new_content + content_size, buffer, bytes_read);
        content_size += bytes_read;
        new_content[content_size] = '\0';
        content = new_content;
    }
    vfs_close(file);
    
    if (!content) return 0;
    
    char* line = strtok(content, "\n");
    int found = 0;
    while (line) {
        char* colon = strchr(line, ':');
        if (colon) {
            *colon = '\0';
            if (strcmp(line, username) == 0) {
                found = 1;
                break;
            }
        }
        line = strtok(NULL, "\n");
    }
    
    kfree(content);
    return found;
}

static void update_progress(uint32_t percent, const char* status) {
    if (progress_bar) {
        wg_set_progressbar_value(progress_bar, percent);
    }
    if (progress_label) {
        char text[256];
        sprintf(text, "%s %d%%", status, percent);
        wg_set_text(progress_label, text);
    }
    if (setup_window) {
        setup_window->needs_redraw = 1;
        gui_render();
        if (vesa_is_double_buffer_enabled()) vesa_swap_buffers();
    }
}

// ============ ЭТАП 1: WELCOME ============
static void create_welcome_stage(void) {
    clear_window_widgets();
    
    wg_create_label_aligned(setup_window, "Welcome to PozitronOS!", 0.5f, 0.25f, TEXT_ALIGN_CENTER);
    wg_create_label_aligned(setup_window, "This wizard will help you set up your system.", 0.5f, 0.38f, TEXT_ALIGN_CENTER);
    wg_create_label_aligned(setup_window, "You will create a user account and configure", 0.5f, 0.45f, TEXT_ALIGN_CENTER);
    wg_create_label_aligned(setup_window, "basic system settings.", 0.5f, 0.52f, TEXT_ALIGN_CENTER);
    
    welcome_next_btn = wg_create_button(setup_window, "Next", 
                                        0.75f, 0.85f, 0.2f, 0.08f,
                                        on_welcome_next, NULL);
}

static void on_welcome_next(Widget* btn, void* userdata) {
    (void)btn;
    (void)userdata;
    current_stage = STAGE_USER_INFO;
    create_user_info_stage();
}

// ============ ЭТАП 2: USER INFO ============
static void create_user_info_stage(void) {
    clear_window_widgets();
    
    wg_create_label_aligned(setup_window, "User Information", 0.5f, 0.10f, TEXT_ALIGN_CENTER);
    
    wg_create_label(setup_window, "Computer name:", 0.12f, 0.25f);

    if (strlen(entered_pcname) == 0) {
        strcpy(entered_pcname, "PozitronPC");
    }
    userinfo_pcname_input = wg_create_input(setup_window, 0.38f, 0.23f, 0.52f, 0.07f, entered_pcname);
    
    wg_create_label(setup_window, "Username:", 0.12f, 0.38f);

    userinfo_username_input = wg_create_input(setup_window, 0.38f, 0.36f, 0.52f, 0.07f, entered_username);
    
    userinfo_next_btn = wg_create_button(setup_window, "Next", 
                                         0.75f, 0.85f, 0.2f, 0.08f,
                                         on_userinfo_next, NULL);
    gui_set_focus(userinfo_username_input);
}

static void on_userinfo_next(Widget* btn, void* userdata) {
    (void)btn;
    (void)userdata;
    
    char* pcname = wg_input_get_text(userinfo_pcname_input);
    char* username = wg_input_get_text(userinfo_username_input);
    
    if (!pcname || strlen(pcname) == 0) {
        return;
    }
    if (!username || strlen(username) == 0) {
        return;
    }
    
    if (user_exists(username)) {
        return;
    }
    
    strcpy(entered_pcname, pcname);
    strcpy(entered_username, username);
    
    current_stage = STAGE_PASSWORD;
    create_password_stage();
}

// ============ ЭТАП 3: PASSWORD ============
static void create_password_stage(void) {
    clear_window_widgets();
    
    wg_create_label_aligned(setup_window, "Set Password", 0.5f, 0.10f, TEXT_ALIGN_CENTER);
    wg_create_label_aligned(setup_window, "You can leave this empty for no password.", 0.5f, 0.18f, TEXT_ALIGN_CENTER);
    
    wg_create_label(setup_window, "Password:", 0.12f, 0.30f);
    password_pass_input = wg_create_input(setup_window, 0.38f, 0.28f, 0.52f, 0.07f, entered_password);
    wg_input_set_password_mode(password_pass_input, 1);
    
    wg_create_label(setup_window, "Confirm password:", 0.12f, 0.42f);
    password_confirm_input = wg_create_input(setup_window, 0.38f, 0.40f, 0.52f, 0.07f, "");
    wg_input_set_password_mode(password_confirm_input, 1);
    
    wg_create_label(setup_window, "Password hint:", 0.12f, 0.54f);
    password_hint_input = wg_create_input(setup_window, 0.38f, 0.52f, 0.52f, 0.07f, entered_password_hint);
    
    password_error_label = wg_create_label_aligned(setup_window, "", 0.5f, 0.64f, TEXT_ALIGN_CENTER);
    
    password_back_btn = wg_create_button(setup_window, "Back", 
                                         0.05f, 0.85f, 0.15f, 0.08f,
                                         on_password_back, NULL);
    password_next_btn = wg_create_button(setup_window, "Next", 
                                         0.75f, 0.85f, 0.2f, 0.08f,
                                         on_password_next, NULL);
    
    gui_set_focus(password_pass_input);
}

static void on_password_next(Widget* btn, void* userdata) {
    (void)btn;
    (void)userdata;
    
    char* pass = wg_input_get_text(password_pass_input);
    char* confirm = wg_input_get_text(password_confirm_input);
    char* hint = wg_input_get_text(password_hint_input);
    
    if (strcmp(pass, confirm) != 0) {
        wg_set_text(password_error_label, "Passwords do not match!");
        return;
    }
    
    strcpy(entered_password, pass);
    strcpy(entered_password_hint, hint ? hint : "");
    password_hash = hash_password(pass);
    
    current_stage = STAGE_CONFIRM;
    create_confirm_stage();
}

static void on_password_back(Widget* btn, void* userdata) {
    (void)btn;
    (void)userdata;
    current_stage = STAGE_USER_INFO;
    create_user_info_stage();
}

// ============ ЭТАП 4: CONFIRM ============
static void create_confirm_stage(void) {
    clear_window_widgets();
    
    wg_create_label_aligned(setup_window, "Confirm Settings", 0.5f, 0.10f, TEXT_ALIGN_CENTER);
    wg_create_label_aligned(setup_window, "Please review your settings:", 0.5f, 0.18f, TEXT_ALIGN_CENTER);
    
    char username_text[128];
    sprintf(username_text, "Username: %s", entered_username);
    confirm_username_label = wg_create_label(setup_window, username_text, 0.15f, 0.30f);
    
    char pcname_text[128];
    sprintf(pcname_text, "Computer name: %s", entered_pcname);
    confirm_pcname_label = wg_create_label(setup_window, pcname_text, 0.15f, 0.38f);
    
    char password_text[128];
    if (strlen(entered_password) > 0) {
        sprintf(password_text, "Password: [set]");
    } else {
        sprintf(password_text, "Password: [none]");
    }
    confirm_password_label = wg_create_label(setup_window, password_text, 0.15f, 0.46f);
    
    char hint_text[128];
    if (strlen(entered_password_hint) > 0) {
        sprintf(hint_text, "Password hint: %s", entered_password_hint);
    } else {
        sprintf(hint_text, "Password hint: [none]");
    }
    confirm_hint_label = wg_create_label(setup_window, hint_text, 0.15f, 0.54f);
    
    confirm_back_btn = wg_create_button(setup_window, "Back", 
                                        0.05f, 0.85f, 0.15f, 0.08f,
                                        on_confirm_back, NULL);
    confirm_create_btn = wg_create_button(setup_window, "Create", 
                                          0.75f, 0.85f, 0.2f, 0.08f,
                                          on_confirm_create, NULL);
}

static void on_confirm_back(Widget* btn, void* userdata) {
    (void)btn;
    (void)userdata;
    current_stage = STAGE_PASSWORD;
    create_password_stage();
}

static void create_progress_stage(void) {
    clear_window_widgets();
    
    wg_create_label_aligned(setup_window, "Setting up your system...", 0.5f, 0.20f, TEXT_ALIGN_CENTER);
    progress_bar = wg_create_progressbar(setup_window, 0.15f, 0.40f, 0.70f, 0.08f, 0);
    progress_label = wg_create_label(setup_window, "", 0.30f, 0.55f);
    
    setup_window->needs_redraw = 1;
    gui_render();
    if (vesa_is_double_buffer_enabled()) vesa_swap_buffers();
}

static void do_apply_settings(void) {
    current_stage = STAGE_PROGRESS;
    create_progress_stage();
    
    update_progress(10, "Creating user directory...");
    char user_path[256];
    sprintf(user_path, "/users/%s", entered_username);
    if (vfs_mkdir_p(user_path, 0755) != 0) {
        serial_puts("[SETUP] Failed to create user directory\n");
        update_progress(0, "ERROR: Failed to create user directory!");
        return;
    }
    serial_puts("[SETUP] Created root directory for user: ");
    serial_puts(user_path);
    serial_puts("\n");
    
    update_progress(30, "Creating user subdirectories...");
    
    const char* user_subdirs[] = {
        "Desktop",
        "Documents",
        "Downloads",
        "Music",
        "Pictures",
        "ProgData",
        "Programs",
        "Videos"
    };
    int num_subdirs = sizeof(user_subdirs) / sizeof(user_subdirs[0]);
    
    char subdir_path[512];
    for (int i = 0; i < num_subdirs; i++) {
        sprintf(subdir_path, "%s/%s", user_path, user_subdirs[i]);
        
        int res = vfs_mkdir_p(subdir_path, 0755);
        if (res != 0) {
            serial_puts("[SETUP] Warning: Failed to create subdir: ");
            serial_puts(subdir_path);
            serial_puts("\n");
        } else {
            serial_puts("[SETUP] Subdir created successfully: ");
            serial_puts(subdir_path);
            serial_puts("\n");
        }
    }
    
    update_progress(50, "Creating system directories...");
    vfs_mkdir_p("/PozitronOS/SysTEMP", 0755);
    vfs_mkdir_p("/PozitronOS/SysFiles/wallpapers", 0755);
    
    update_progress(70, "Creating /etc/passwd...");
    vfs_mkdir_p("/etc", 0755);
    
    char hash_str[9];
    uint32_t hash_tmp = password_hash;
    const char hex_chars[] = "0123456789ABCDEF";
    
    for (int i = 0; i < 8; i++) {
        hash_str[7 - i] = hex_chars[hash_tmp & 0xF];
        hash_tmp >>= 4;
    }
    hash_str[8] = '\0';
    
    // Формат: username:hash:uid:gid:fullname:hint:home:shell
    char passwd_line[512];
    sprintf(passwd_line, "%s:%s:1000:1000:%s:%s:%s:/bin/desktop\n",
            entered_username, hash_str, entered_username, entered_password_hint, user_path);
    
    struct vfs_file* passwd_file;
    if (vfs_open("/etc/passwd", FS_O_WRONLY | FS_O_CREAT, &passwd_file) == 0) {
        uint32_t written;
        vfs_write(passwd_file, passwd_line, strlen(passwd_line), &written);
        vfs_close(passwd_file);
        serial_puts("[SETUP] /etc/passwd created\n");
    }
    
    update_progress(90, "Saving configuration...");
    char conf_path[] = "/PozitronOS/Sys32/confs/sys_conf.conf";
    char new_content[512];
    sprintf(new_content, "firstboot=0\npcname=%s\n", entered_pcname);
    
    struct vfs_file* conf_file;
    if (vfs_open(conf_path, FS_O_WRONLY | FS_O_CREAT | FS_O_TRUNC, &conf_file) == 0) {
        uint32_t written;
        vfs_write(conf_file, new_content, strlen(new_content), &written);
        vfs_close(conf_file);
    }
    
    char session_content[256];
    sprintf(session_content, "last_user=%s\nautologin=1\n", entered_username);
    if (vfs_open("/PozitronOS/Sys32/confs/session.conf", FS_O_WRONLY | FS_O_CREAT | FS_O_TRUNC, &conf_file) == 0) {
        uint32_t written;
        vfs_write(conf_file, session_content, strlen(session_content), &written);
        vfs_close(conf_file);
    }
    
    update_progress(100, "Setup complete!");
    
    for (int i = 0; i < 500000; i++) asm volatile("nop");
    
    setup_success = 1;
    current_stage = STAGE_COMPLETE;
    create_complete_stage();
}

static void on_confirm_create(Widget* btn, void* userdata) {
    (void)btn;
    (void)userdata;
    do_apply_settings();
}

// ============ ЭТАП 6: COMPLETE ============
static void create_complete_stage(void) {
    clear_window_widgets();
    
    wg_create_label_aligned(setup_window, "Setup Complete!", 0.5f, 0.25f, TEXT_ALIGN_CENTER);
    wg_create_label_aligned(setup_window, "Your system has been configured successfully.", 0.5f, 0.38f, TEXT_ALIGN_CENTER);
    wg_create_label_aligned(setup_window, "Please login your user to continue.", 0.5f, 0.50f, TEXT_ALIGN_CENTER);
    
    complete_continue_btn = wg_create_button(setup_window, "Close", 
                                             0.40f, 0.75f, 0.20f, 0.10f,
                                             on_complete_continue, NULL);
}

static void on_complete_continue(Widget* btn, void* userdata) {
    (void)btn;
    (void)userdata;
    setup_complete = 1;
    setup_success = 1;
    wm_destroy_window(setup_window);
    setup_window = NULL;
    char* username = show_login_screen(NULL);
    if (username) {
        start_desktop(username);
        kfree(username);
    }
}

// ============ ПУБЛИЧНЫЕ ФУНКЦИИ ============

void show_setup_window(void) {
    uint32_t screen_width = vesa_get_width();
    uint32_t screen_height = vesa_get_height();
    
    uint32_t win_width = 550;
    uint32_t win_height = 480;
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
    
    current_stage = STAGE_WELCOME;
    setup_success = 0;
    
    memset(entered_username, 0, sizeof(entered_username));
    memset(entered_password, 0, sizeof(entered_password));
    memset(entered_pcname, 0, sizeof(entered_pcname));
    memset(entered_password_hint, 0, sizeof(entered_password_hint));
    
    create_welcome_stage();
    
    serial_puts("[SETUP] Window created\n");
}

uint8_t is_first_boot(void) {
    char* fstart = vfs_read_config("/PozitronOS/Sys32/confs/sys_conf.conf", "firstboot");
    if (fstart) {
        uint8_t result = (strcmp(fstart, "1") == 0);
        kfree(fstart);
        return result;
    }
    return 1;
}

uint8_t is_setup_complete(void) {
    return setup_complete && setup_success;
}

char* get_last_username(void) {
    char* last_user = vfs_read_config("/PozitronOS/Sys32/confs/session.conf", "last_user");
    return last_user;
}

uint8_t is_autologin_enabled(void) {
    char* autologin = vfs_read_config("/PozitronOS/Sys32/confs/session.conf", "autologin");
    if (autologin) {
        uint8_t result = (strcmp(autologin, "1") == 0);
        kfree(autologin);
        return result;
    }
    return 0;
}