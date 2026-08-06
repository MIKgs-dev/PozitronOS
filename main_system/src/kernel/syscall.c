#include "kernel/syscall.h"
#include "drivers/serial.h"
#include "drivers/vga.h"
#include "kernel/memory.h"
#include "kernel/task.h"
#include "drivers/timer.h"
#include "drivers/keyboard.h"
#include "drivers/acpi.h"
#include "gui/gui.h"
#include "lib/string.h"
#include "kernel/timer_utils.h"
#include "gui/shutdown.h"
#include "fs/vfs.h"
#include "kernel/notif.h"
#include "core/event.h"

extern void show_verification_dialog(void);
extern uint8_t shutdown_state;
extern uint8_t selected_action;
extern uint8_t darken_level;
extern uint8_t shutdown_immediate;
extern uint32_t original_window_count;
extern void hide_all_windows(void);

#define VALIDATE_PTR(ptr, size, msg) \
    if (!validate_user_buffer((const void*)(ptr), (size))) { \
        serial_puts("[SECURITY ALERT] PID "); \
        serial_puts_num(current_task ? current_task->id : 0); \
        serial_puts(" killed: " msg "\n"); \
        if (current_task) { \
            wm_destroy_windows_by_pid(current_task->id); \
        } \
        task_exit(); \
        return -1; \
    }

static int validate_user_string(const char* path, uint32_t max_len) {
    if (!path) return 0;
    
    uint32_t len = 0;
    while (len < max_len) {
        if (!validate_user_buffer((const void*)(path + len), 1)) {
            return 0;
        }
        
        if (path[len] == '\0') {
            return 1; 
        }
        len++;
    }
    
    return 0;
}

#define VALIDATE_STR(ptr, max_size, msg) \
    if (!validate_user_string((const char*)(ptr), (max_size))) { \
        serial_puts("[SECURITY ALERT] PID "); \
        serial_puts_num(current_task ? current_task->id : 0); \
        serial_puts(" killed: " msg "\n"); \
        if (current_task) { \
            wm_destroy_windows_by_pid(current_task->id); \
        } \
        task_exit(); \
        return -1; \
    }

static int syscall_exit(int code) {
    serial_puts("[SYSCALL] Exit with code ");
    serial_puts_num(code);
    serial_puts("\n");

    if (current_task) {
        wm_destroy_windows_by_pid(current_task->id);
    }
    
    task_exit();
    return 0;
}

static int syscall_serial_write(const char* str, int len) {
    if (!str || len <= 0) return -1;
    if (len > 4096) len = 4096;
    
    VALIDATE_PTR(str, len, "invalid buffer in syscall_write");

    int actual_len = 0;
    for(int i = 0; i < len; i++) {
        if (str[i] == '\0') break; 
        if(str[i] == '\n') {
            serial_write('\r');
            vga_putchar('\n');
        } else {
            serial_write(str[i]);
            vga_putchar(str[i]);
        }
        actual_len++;
    }
    return actual_len;
}

static int syscall_create_window(uint32_t params_ptr) {
    if (!params_ptr) return 0;
    
    VALIDATE_PTR(params_ptr, sizeof(win_params_t), "invalid params_ptr in syscall_create_window");

    win_params_t* p = (win_params_t*)params_ptr;

    if (p->title) {
        VALIDATE_STR(p->title, 128, "invalid title string in syscall_create_window");
    }

    uint8_t user_flags = p->flags;

    if (user_flags & 0x02) { // WINDOW_MOVABLE
        user_flags |= 0x08;  // WINDOW_HAS_TITLE
    }

    Window* win = wm_create_window(p->title, p->x, p->y, p->width, p->height, user_flags);
    
    if (!win) return 0;

    if (current_task) {
        win->owner_pid = current_task->id;
    } else {
        win->owner_pid = 0;
    }

    win->orig_movable = win->movable;
    win->orig_resizable = win->resizable;

    win->needs_redraw = 1;
    
    if (win->in_taskbar) {
        taskbar_update_window(win);
    }

    return win->id;
}

static int syscall_close_window(uint32_t window_id) {
    Window* win = gui_get_window_by_id(window_id);
    
    if (!win) return -1;
    
    if (win->owner_pid != current_task->id) {
        serial_puts("[SYSCALL] WARNING: Process ");
        serial_puts_num(current_task->id);
        serial_puts(" tried to close window ");
        serial_puts_num(window_id);
        serial_puts(" owned by PID ");
        serial_puts_num(win->owner_pid);
        serial_puts("\n");
        return -2;
    }
    
    wm_close_window(win);
    return 0;
}

static int syscall_set_window_title(uint32_t window_id, const char* title) {
    Window* win = gui_get_window_by_id(window_id);
    
    if (!win) return -1;
    
    if (win->owner_pid != current_task->id) {
        serial_puts("[SYSCALL] WARNING: Process ");
        serial_puts_num(current_task->id);
        serial_puts(" tried to set title of window ");
        serial_puts_num(window_id);
        serial_puts(" owned by PID ");
        serial_puts_num(win->owner_pid);
        serial_puts("\n");
        return -2;
    }
    
    if (!title) return -3;
    
    VALIDATE_STR(title, 128, "invalid title string in syscall_set_window_title");
    
    if (win->title) {
        kfree(win->title);
        win->title = NULL;
    }
    
    uint32_t len = 0;
    while (title[len] && len < 127) len++;
    
    win->title = (char*)kmalloc(len + 1);
    if (!win->title) return -4;
    
    for (uint32_t i = 0; i < len; i++) {
        win->title[i] = title[i];
    }
    win->title[len] = '\0';
    
    if (win->in_taskbar) {
        taskbar_update_window(win);
    }
    
    win->needs_redraw = 1;
    return 0;
}

static int syscall_msleep(uint32_t ms) {
    if (!current_task) return -1;
    if (ms == 0) return 0;

    uint32_t ticks = (ms + 9) / 10; 
    
    current_task->sleep_ticks = ticks;
    current_task->state = TASK_STATE_SLEEPING;

    asm volatile("int $0x20"); 

    return 0;
}

static int syscall_create_label(uint32_t params_ptr) {
    if (!params_ptr) return -1;

    VALIDATE_PTR(params_ptr, sizeof(label_params_t), "invalid params_ptr in syscall_create_label");

    label_params_t* p = (label_params_t*)params_ptr;

    Window* win = gui_get_window_by_id(p->window_id);
    if (!win) return -1;

    if (current_task && win->owner_pid != current_task->id) {
        serial_puts("[SYSCALL] WARNING: PID ");
        serial_puts_num(current_task->id);
        serial_puts(" tried to add label to foreign window!\n");
        return -2;
    }

    if (!p->text) return -3;
    
    VALIDATE_STR(p->text, 128, "invalid text pointer in syscall_create_label");

    uint32_t len = 0;
    while (p->text[len] && len < 127) len++;

    char* kernel_text = (char*)kmalloc(len + 1);
    if (!kernel_text) return -4;

    for (uint32_t i = 0; i < len; i++) {
        kernel_text[i] = p->text[i];
    }
    kernel_text[len] = '\0';

    Widget* lbl = NULL;
    
    if (p->align != 0) {
        lbl = wg_create_label_aligned(win, kernel_text, p->rel_x, p->rel_y, p->align);
    } else {
        lbl = wg_create_label(win, kernel_text, p->rel_x, p->rel_y);
    }

    kfree(kernel_text);

    if (!lbl) return -5;

    win->needs_redraw = 1;
    return lbl->id;
}

int sys_set_widget_text(uint32_t widget_id, const char* user_text) {
    if (!user_text) return -1;

    VALIDATE_STR(user_text, 256, "invalid user_text pointer in sys_set_widget_text");

    Widget* widget = gui_get_widget_by_id(widget_id);
    if (!widget) return -2;

    Window* win = widget->parent_window;
    if (win && current_task && win->owner_pid != current_task->id) {
        return -3;
    }
    
    uint32_t len = 0;
    while (user_text[len] && len < 255) {
        len++;
    }

    char* kernel_text = (char*)kmalloc(len + 1);
    if (!kernel_text) return -4;

    for (uint32_t i = 0; i < len; i++) {
        kernel_text[i] = user_text[i];
    }
    kernel_text[len] = '\0';
    wg_set_text(widget, kernel_text);
    kfree(kernel_text);
    return 0;
}

void kernel_button_redirect_callback(Widget* widget, void* userdata) {
    event_t ev;
    ev.type = 9;
    ev.target.window_id = widget->parent_window->id;
    ev.target.widget_id = widget->id;
    ev.timestamp = timer_get_ticks();
    
    wm_post_to_window(widget->parent_window, ev);
}

int sys_create_button(button_params_t* u_params) {
    if (!u_params) return -1;
    
    VALIDATE_PTR(u_params, sizeof(button_params_t), "invalid u_params pointer in sys_create_button");
    
    if (u_params->text) {
        VALIDATE_STR(u_params->text, 128, "invalid button text pointer in sys_create_button");
    }
    
    Window* win = gui_get_window_by_id(u_params->window_id);
    if (!win) return -2;
    
    Widget* btn = wg_create_button_aligned(
        win, 
        u_params->text, 
        u_params->rel_x, 
        u_params->rel_y, 
        u_params->rel_width, 
        u_params->rel_height, 
        u_params->align,
        kernel_button_redirect_callback,
        NULL
    );
    
    if (!btn) return -3;
    
    return btn->id;
}

int sys_poll_event(int window_id, event_t* u_event) {
    Window* win = gui_get_window_by_id(window_id);
    if (!win || !u_event) return 0;
    
    if (win->owner_pid != current_task->id) return 0; 
    
    VALIDATE_PTR(u_event, sizeof(event_t), "invalid u_event pointer in sys_poll_event");
    
    int result = 0;
    asm volatile("cli");
    
    if (win->win_event_count > 0) {
        *u_event = win->win_events[win->win_event_head];
        win->win_event_head = (win->win_event_head + 1) % WIN_EVENT_QUEUE_SIZE;
        win->win_event_count--;
        result = 1;
    }
    
    asm volatile("sti");
    return result;
}

static int syscall_power_control(uint32_t action) {
    serial_puts("[POWER] Process ");
    serial_puts_num(current_task ? current_task->id : 0);
    serial_puts(" invoked UI power control, action: ");
    serial_puts_num(action);
    serial_puts("\n");

    if (is_shutdown_mode_active()) {
        return -4; 
    }

    if (action == POWER_ACTION_SHUTDOWN || action == POWER_ACTION_REBOOT) {
        selected_action = action;
        
        darken_level = 0;
        shutdown_immediate = 0;
        original_window_count = 0;
        
        hide_all_windows();
        
        show_verification_dialog();
        
        shutdown_state = 1; 
        
        return 0;
    } 
    
    return -1;
}

static int syscall_vfs_open(const char* path, int flags) {
    if (!path || !current_task) return -1;
    
    VALIDATE_STR(path, 256, "invalid path pointer in syscall_vfs_open");

    int fd = -1;
    for (int i = 0; i < MAX_PROCESS_FILES; i++) {
        if (current_task->file_table[i] == NULL) {
            fd = i;
            break;
        }
    }
    if (fd == -1) return -2;

    struct vfs_file* file = NULL;
    int ret = vfs_open(path, flags, &file);
    if (ret != 0 || !file) return -3;

    current_task->file_table[fd] = file;
    return fd;
}

static int syscall_vfs_read(int fd, void* user_buf, uint32_t size) {
    if (!current_task || !user_buf || size == 0) return -1;
    if (fd < 0 || fd >= MAX_PROCESS_FILES) return -2;

    struct vfs_file* file = current_task->file_table[fd];
    if (!file) return -3;

    VALIDATE_PTR(user_buf, size, "invalid user buffer in syscall_vfs_read");

    uint32_t bytes_read = 0;
    int ret = vfs_read(file, user_buf, size, &bytes_read);
    if (ret != 0) return -4;

    return (int)bytes_read;
}

static int syscall_vfs_write(int fd, const void* user_buf, uint32_t size) {
    if (!current_task || !user_buf || size == 0) return -1;
    if (fd < 0 || fd >= MAX_PROCESS_FILES) return -2;

    struct vfs_file* file = current_task->file_table[fd];
    if (!file) return -3;

    VALIDATE_PTR(user_buf, size, "invalid user buffer in syscall_vfs_write");

    uint32_t bytes_written = 0;
    int ret = vfs_write(file, user_buf, size, &bytes_written); // Вызов вашей функции VFS
    if (ret != 0) return -4;

    return (int)bytes_written;
}

static int syscall_vfs_close(int fd) {
    if (!current_task) return -1;
    if (fd < 0 || fd >= MAX_PROCESS_FILES) return -2;

    struct vfs_file* file = current_task->file_table[fd];
    if (!file) return -3;

    vfs_close(file);
    current_task->file_table[fd] = NULL;
    return 0;
}

static int syscall_show_notif(uint32_t type, const char* title, const char* message) {
    if (type > NOTIF_ERROR) {
        return -1;
    }

    if (!title || !message) return -2;

    VALIDATE_STR(title, NOTIF_MAX_TITLE, "invalid title string in syscall_show_notif");
    VALIDATE_STR(message, NOTIF_MAX_MSG, "invalid message string in syscall_show_notif");

    switch (type) {
        case NOTIF_INFO:
            notif_info(title, message);
            break;
        case NOTIF_WARNING:
            notif_warning(title, message);
            break;
        case NOTIF_ERROR:
            notif_error(title, message);
            break;
        default:
            return -1;
    }

    return 0;
}

static int syscall_enable_canvas(uint32_t window_id) {
    Window* win = gui_get_window_by_id(window_id);
    if (!win) return -1;
    if (win->owner_pid != current_task->id) return -2;
    if (win->canvas_enabled) return 0;
    
    uint32_t max_width = 4096;
    uint32_t max_height = 4096;
    win->canvas_capacity = max_width * max_height;
    
    win->canvas_back = (uint32_t*)kmalloc(win->canvas_capacity * sizeof(uint32_t));
    if (!win->canvas_back) {
        max_width = 2048;
        max_height = 2048;
        win->canvas_capacity = max_width * max_height;
        win->canvas_back = (uint32_t*)kmalloc(win->canvas_capacity * sizeof(uint32_t));
        if (!win->canvas_back) return -5;
    }
    
    win->canvas_front = (uint32_t*)kmalloc(win->canvas_capacity * sizeof(uint32_t));
    if (!win->canvas_front) {
        kfree(win->canvas_back);
        win->canvas_back = NULL;
        return -5;
    }
    
    win->canvas_width = win->width;
    win->canvas_height = win->height - win->title_height;
    
    for (uint32_t i = 0; i < win->canvas_capacity; i++) {
        win->canvas_back[i] = 0x00000000;
        win->canvas_front[i] = 0x00000000;
    }
    
    win->canvas_enabled = 1;
    win->canvas_swap_pending = 0;
    
    return 0;
}

static int syscall_disable_canvas(uint32_t window_id) {
    Window* win = gui_get_window_by_id(window_id);
    if (!win) return -1;
    if (win->owner_pid != current_task->id) return -2;
    if (!win->canvas_enabled) return 0;
    
    if (win->canvas_back) {
        kfree(win->canvas_back);
        win->canvas_back = NULL;
    }
    if (win->canvas_front) {
        kfree(win->canvas_front);
        win->canvas_front = NULL;
    }
    
    win->canvas_width = 0;
    win->canvas_height = 0;
    win->canvas_capacity = 0;
    win->canvas_enabled = 0;
    win->canvas_swap_pending = 0;
    
    return 0;
}

static int syscall_draw_pixel(uint32_t window_id, uint32_t xy_packed, uint32_t color) {
    int x = (xy_packed >> 16) & 0xFFFF;
    int y = xy_packed & 0xFFFF;
    
    Window* win = gui_get_window_by_id(window_id);
    if (!win) return -1;
    if (win->owner_pid != current_task->id) return -2;
    if (!win->canvas_enabled || !win->canvas_back) return -3;
    
    if (x < 0 || x >= (int32_t)win->canvas_width || 
        y < 0 || y >= (int32_t)win->canvas_height) return -4;
    
    win->canvas_back[y * win->canvas_width + x] = color;
    
    return 0;
}

static int syscall_draw_rect(uint32_t window_id, uint32_t packed, uint32_t color) {
    int x = packed & 0xFF;
    int y = (packed >> 8) & 0xFF;
    int w = (packed >> 16) & 0xFF;
    int h = (packed >> 24) & 0xFF;
    
    Window* win = gui_get_window_by_id(window_id);
    if (!win) return -1;
    if (win->owner_pid != current_task->id) return -2;
    if (!win->canvas_enabled || !win->canvas_back) return -3;
    
    int start_x = (x < 0) ? 0 : x;
    int start_y = (y < 0) ? 0 : y;
    int end_x = (x + w > win->canvas_width) ? win->canvas_width : x + w;
    int end_y = (y + h > win->canvas_height) ? win->canvas_height : y + h;
    
    if (start_x >= end_x || start_y >= end_y) return -4;
    
    for (int row = start_y; row < end_y; row++) {
        uint32_t* row_ptr = win->canvas_back + row * win->canvas_width + start_x;
        for (int col = start_x; col < end_x; col++) {
            row_ptr[col - start_x] = color;
        }
    }
    
    return 0;
}

static int syscall_canvas_fill(uint32_t window_id, uint32_t color) {
    Window* win = gui_get_window_by_id(window_id);
    if (!win) return -1;
    if (win->owner_pid != current_task->id) return -2;
    if (!win->canvas_enabled || !win->canvas_back) return -3;
    
    uint32_t total_pixels = win->canvas_width * win->canvas_height;
    for (uint32_t i = 0; i < total_pixels; i++) {
        win->canvas_back[i] = color;
    }
    
    return 0;
}

static int syscall_flush_window(uint32_t window_id) {
    Window* win = gui_get_window_by_id(window_id);
    if (!win) return -1;
    if (win->owner_pid != current_task->id) return -2;
    if (!win->canvas_enabled || !win->canvas_back || !win->canvas_front) return -3;
    
    uint32_t* tmp = win->canvas_front;
    win->canvas_front = win->canvas_back;
    win->canvas_back = tmp;
    
    win->canvas_swap_pending = 1;
    win->needs_redraw = 1;
    
    vesa_mark_dirty(win->x, win->y + win->title_height, 
                    win->width, win->height);
    
    return 0;
}

static int syscall_get_window_info(uint32_t window_id, window_info_t* user_info) {
    if (!user_info) return -1;
    
    VALIDATE_PTR(user_info, sizeof(window_info_t), "invalid user_info pointer in syscall_get_window_info");
    
    Window* win = gui_get_window_by_id(window_id);
    if (!win) return -1;
    
    window_info_t info;
    info.window_id = win->id;
    info.x = win->x;
    info.y = win->y;
    info.width = win->width;
    info.height = win->height;
    info.client_width = win->width;
    info.client_height = win->height - win->title_height;
    info.is_visible = win->visible;
    info.is_minimized = win->minimized;
    info.is_maximized = win->maximized;
    info.is_focused = win->focused;
    info.has_titlebar = win->has_titlebar;
    info.title_height = win->title_height;
    
    if (!validate_user_buffer(user_info, sizeof(window_info_t))) {
        return -2;
    }
    
    uint8_t* dst = (uint8_t*)user_info;
    uint8_t* src = (uint8_t*)&info;
    for (uint32_t i = 0; i < sizeof(window_info_t); i++) {
        dst[i] = src[i];
    }
    
    return 0;
}

static int syscall_draw_block_2x2(uint32_t window_id, uint32_t xy_packed, uint32_t color) {
    int x = (xy_packed >> 16) & 0xFFFF;
    int y = xy_packed & 0xFFFF;
    
    Window* win = gui_get_window_by_id(window_id);
    if (!win) return -1;
    if (win->owner_pid != current_task->id) return -2;
    if (!win->canvas_enabled || !win->canvas_back) return -3;
    
    if (x < 0 || x + 2 > (int32_t)win->canvas_width || 
        y < 0 || y + 2 > (int32_t)win->canvas_height) return -4;
    
    uint32_t idx = y * win->canvas_width + x;
    win->canvas_back[idx] = color;
    win->canvas_back[idx + 1] = color;
    win->canvas_back[idx + win->canvas_width] = color;
    win->canvas_back[idx + win->canvas_width + 1] = color;
    
    return 0;
}

static int syscall_draw_block_4x4(uint32_t window_id, uint32_t xy_packed, uint32_t color) {
    int x = (xy_packed >> 16) & 0xFFFF;
    int y = xy_packed & 0xFFFF;
    
    Window* win = gui_get_window_by_id(window_id);
    if (!win) return -1;
    if (win->owner_pid != current_task->id) return -2;
    if (!win->canvas_enabled || !win->canvas_back) return -3;
    
    if (x < 0 || x + 4 > (int32_t)win->canvas_width || 
        y < 0 || y + 4 > (int32_t)win->canvas_height) return -4;
    
    for (int dy = 0; dy < 4; dy++) {
        uint32_t* row = win->canvas_back + (y + dy) * win->canvas_width + x;
        for (int dx = 0; dx < 4; dx++) {
            row[dx] = color;
        }
    }
    
    return 0;
}

static int syscall_draw_block_8x8(uint32_t window_id, uint32_t xy_packed, uint32_t color) {
    int x = (xy_packed >> 16) & 0xFFFF;
    int y = xy_packed & 0xFFFF;
    
    Window* win = gui_get_window_by_id(window_id);
    if (!win) return -1;
    if (win->owner_pid != current_task->id) return -2;
    if (!win->canvas_enabled || !win->canvas_back) return -3;
    
    if (x < 0 || x + 8 > (int32_t)win->canvas_width || 
        y < 0 || y + 8 > (int32_t)win->canvas_height) return -4;
    
    for (int dy = 0; dy < 8; dy++) {
        uint32_t* row = win->canvas_back + (y + dy) * win->canvas_width + x;
        for (int dx = 0; dx < 8; dx++) {
            row[dx] = color;
        }
    }
    
    return 0;
}

static int syscall_draw_block(uint32_t window_id, uint32_t params_packed, uint32_t color) {
    int x = params_packed & 0xFF;
    int y = (params_packed >> 8) & 0xFF;
    int w = (params_packed >> 16) & 0xFF;
    int h = (params_packed >> 24) & 0xFF;
    
    if (w == 0 || h == 0) return 0;
    if (w > 16 || h > 16) return -5;
    
    Window* win = gui_get_window_by_id(window_id);
    if (!win) return -1;
    if (win->owner_pid != current_task->id) return -2;
    if (!win->canvas_enabled || !win->canvas_back) return -3;
    
    if (x < 0 || x + w > (int32_t)win->canvas_width || 
        y < 0 || y + h > (int32_t)win->canvas_height) return -4;
    
    for (int dy = 0; dy < h; dy++) {
        uint32_t* row = win->canvas_back + (y + dy) * win->canvas_width + x;
        for (int dx = 0; dx < w; dx++) {
            row[dx] = color;
        }
    }
    
    return 0;
}

static int syscall_enable_input(uint32_t window_id, uint8_t enable) {
    Window* win = gui_get_window_by_id(window_id);
    if (!win) return -1;
    if (win->owner_pid != current_task->id) return -2;
    win->input_enabled = enable ? 1 : 0;
    return 0;
}

static int syscall_poll_input(uint32_t window_id, input_event_t* user_event) {
    Window* win = gui_get_window_by_id(window_id);
    if (!win || !user_event) return 0;
    if (win->owner_pid != current_task->id) return -1;
    
    VALIDATE_PTR(user_event, sizeof(input_event_t), "invalid user_event pointer");
    
    int result = 0;
    asm volatile("cli");
    
    if (win->input_count > 0) {
        *user_event = win->input_queue[win->input_head];
        win->input_head = (win->input_head + 1) % 128;
        win->input_count--;
        result = 1;
    }
    
    asm volatile("sti");
    return result;
}

static int syscall_wait_input(uint32_t window_id, input_event_t* user_event, uint32_t timeout_ms) {
    Window* win = gui_get_window_by_id(window_id);
    if (!win || !user_event) return -1;
    if (win->owner_pid != current_task->id) return -2;
    
    VALIDATE_PTR(user_event, sizeof(input_event_t), "invalid user_event pointer");
    
    uint32_t start_ticks = timer_get_ticks();
    uint32_t timeout_ticks = timeout_ms / 10;
    
    while (1) {
        if (win->input_count > 0) {
            asm volatile("cli");
            *user_event = win->input_queue[win->input_head];
            win->input_head = (win->input_head + 1) % 128;
            win->input_count--;
            asm volatile("sti");
            return 1;
        }
        
        if (timeout_ms > 0) {
            uint32_t current_ticks = timer_get_ticks();
            if (current_ticks - start_ticks >= timeout_ticks) {
                return 0;
            }
        }
        
        asm volatile("hlt");
    }
}

static int syscall_vfs_stat(const char* path, vfs_stat_t* user_stat) {
    if (!path || !user_stat || !current_task) return -1;
    
    VALIDATE_STR(path, 256, "invalid path in vfs_stat");
    VALIDATE_PTR(user_stat, sizeof(vfs_stat_t), "invalid stat buffer");
    
    struct vfs_inode* inode;
    if (vfs_stat(path, &inode) != 0) {
        return -2;
    }
    
    vfs_stat_t stat;
    stat.st_ino = inode->i_ino;
    stat.st_size = inode->i_size;
    stat.st_mode = inode->i_mode;
    stat.st_type = (inode->i_mode & FS_DIRECTORY) ? FS_DIRECTORY : FS_FILE;
    stat.st_nlink = inode->i_nlink;
    stat.st_atime = inode->i_atime;
    stat.st_mtime = inode->i_mtime;
    stat.st_ctime = inode->i_ctime;
    
    memcpy(user_stat, &stat, sizeof(vfs_stat_t));
    
    return 0;
}

static int syscall_vfs_lseek(int fd, uint32_t offset, int whence) {
    if (!current_task) return -1;
    if (fd < 0 || fd >= MAX_PROCESS_FILES) return -2;
    
    struct vfs_file* file = current_task->file_table[fd];
    if (!file) return -3;
    
    return vfs_lseek(file, offset, whence);
}

static int syscall_vfs_readdir(int fd, user_dirent_t* user_dirent) {
    if (!current_task || !user_dirent) return -1;
    if (fd < 0 || fd >= MAX_PROCESS_FILES) return -2;
    
    struct vfs_file* file = current_task->file_table[fd];
    if (!file) return -3;
    if (!(file->f_flags & FS_O_DIRECTORY)) return -4;
    
    VALIDATE_PTR(user_dirent, sizeof(user_dirent_t), "invalid dirent buffer");
    
    struct vfs_dirent dirent;
    uint32_t bytes_read;
    
    if (vfs_readdir(file, &dirent, &bytes_read) != 0 || bytes_read == 0) {
        return 0;
    }
    
    user_dirent->d_ino = dirent.d_ino;
    user_dirent->d_type = dirent.d_type;
    user_dirent->d_namelen = dirent.d_namelen;
    
    uint32_t name_len = dirent.d_namelen;
    if (name_len > 255) name_len = 255;
    for (uint32_t i = 0; i < name_len; i++) {
        user_dirent->d_name[i] = dirent.d_name[i];
    }
    user_dirent->d_name[name_len] = '\0';
    
    return 1;
}

static int syscall_vfs_mkdir(const char* path, uint32_t mode) {
    if (!path) return -1;
    VALIDATE_STR(path, 256, "invalid path in mkdir");
    return vfs_mkdir(path, mode | FS_DIRECTORY);
}

static int syscall_vfs_rmdir(const char* path) {
    if (!path) return -1;
    VALIDATE_STR(path, 256, "invalid path in rmdir");   
    return vfs_rmdir(path);
}

static int syscall_vfs_unlink(const char* path) {
    if (!path) return -1;
    VALIDATE_STR(path, 256, "invalid path in unlink");
    return vfs_unlink(path);
}

static int syscall_vfs_rename(const char* old_path, const char* new_path) {
    if (!old_path || !new_path) return -1;
    VALIDATE_STR(old_path, 256, "invalid old path in rename");
    VALIDATE_STR(new_path, 256, "invalid new path in rename");
    return vfs_rename(old_path, new_path);
}

static int syscall_vfs_chdir(const char* path) {
    if (!path || !current_task) return -1;
    VALIDATE_STR(path, 256, "invalid path in chdir");
    
    struct vfs_inode* inode;
    if (vfs_stat(path, &inode) != 0) return -2;
    if (!(inode->i_mode & FS_DIRECTORY)) return -3;
    
    strncpy(current_task->cwd, path, 255);
    current_task->cwd[255] = '\0';
    
    return 0;
}

static int syscall_vfs_getcwd(char* user_buf, uint32_t size) {
    if (!user_buf || !current_task) return -1;
    if (size < 2) return -2;
    
    VALIDATE_PTR(user_buf, size, "invalid buffer in getcwd");
    
    uint32_t len = strlen(current_task->cwd) + 1;
    if (len > size) return -3;
    
    strncpy(user_buf, current_task->cwd, size);
    return len;
}

void syscall_handler(registers_t* r) {
    uint32_t syscall_num = r->eax;
    int ret = -1;
    
    switch(syscall_num) {
        case SYS_EXIT:
            ret = syscall_exit(r->ebx);
            break;
        case SYS_SERIAL_WRITE:
            ret = syscall_serial_write((const char*)r->ebx, r->ecx);
            break;
        case SYS_CREATE_WINDOW:
            ret = syscall_create_window(r->ebx);
            break;
        case SYS_CLOSE_WINDOW:
            ret = syscall_close_window(r->ebx);
            break;
        case SYS_SET_WINDOW_TITLE:
            ret = syscall_set_window_title(r->ebx, (const char*)r->ecx);
            break;
        case SYS_MSLEEP:
            ret = syscall_msleep(r->ebx);
            break;
        case SYS_CREATE_LABEL:
            ret = syscall_create_label(r->ebx);
            break;
        case SYS_SET_WIDGET_TEXT:
            ret = sys_set_widget_text(r->ebx, (const char*)r->ecx);
            break;
        case SYS_CREATE_BUTTON:
            ret = sys_create_button((button_params_t*)r->ebx);
            break;
        case SYS_POLL_EVENT:
            ret = sys_poll_event((int)r->ebx, (event_t*)r->ecx);
            break;
        case SYS_POWER_CONTROL:
            ret = syscall_power_control(r->ebx);
            break;
        case SYS_VFS_OPEN:
            ret = syscall_vfs_open((const char*)r->ebx, r->ecx);
            break;
        case SYS_VFS_READ:
            ret = syscall_vfs_read(r->ebx, (void*)r->ecx, r->edx);
            break;
        case SYS_VFS_WRITE:
            ret = syscall_vfs_write(r->ebx, (const void*)r->ecx, r->edx);
            break;
        case SYS_VFS_CLOSE:
            ret = syscall_vfs_close(r->ebx);
            break;
        case SYS_SHOW_NOTIF:
            ret = syscall_show_notif(r->ebx, (const char*)r->ecx, (const char*)r->edx);
            break;
        case SYS_ENABLE_CANVAS:
            ret = syscall_enable_canvas(r->ebx);
            break;
        case SYS_DISABLE_CANVAS:
            ret = syscall_disable_canvas(r->ebx);
            break;
        case SYS_DRAW_PIXEL:
            ret = syscall_draw_pixel(r->ebx, r->ecx, r->edx);
            break;
        case SYS_DRAW_RECT:
            ret = syscall_draw_rect(r->ebx, r->ecx, r->edx);
            break;
        case SYS_FLUSH_WINDOW:
            ret = syscall_flush_window(r->ebx);
            break;
        case SYS_CANVAS_FILL:
            ret = syscall_canvas_fill(r->ebx, r->ecx);
            break;
        case SYS_GET_WINDOW_INFO:
            ret = syscall_get_window_info(r->ebx, (window_info_t*)r->ecx);
            break;
        case SYS_DRAW_BLOCK_2x2:
            ret = syscall_draw_block_2x2(r->ebx, r->ecx, r->edx);
            break;
        case SYS_DRAW_BLOCK_4x4:
            ret = syscall_draw_block_4x4(r->ebx, r->ecx, r->edx);
            break;
        case SYS_DRAW_BLOCK_8x8:
            ret = syscall_draw_block_8x8(r->ebx, r->ecx, r->edx);
            break;
        case SYS_DRAW_BLOCK:
            ret = syscall_draw_block(r->ebx, r->ecx, r->edx);
            break;
        case SYS_ENABLE_INPUT:
            ret = syscall_enable_input(r->ebx, (uint8_t)r->ecx);
            break;
        case SYS_POLL_INPUT:
            ret = syscall_poll_input(r->ebx, (input_event_t*)r->ecx);
            break;
        case SYS_WAIT_INPUT:
            ret = syscall_wait_input(r->ebx, (input_event_t*)r->ecx, r->edx);
            break;
        case SYS_VFS_STAT:
            ret = syscall_vfs_stat((const char*)r->ebx, (vfs_stat_t*)r->ecx);
            break;
        case SYS_VFS_LSEEK:
            ret = syscall_vfs_lseek(r->ebx, r->ecx, r->edx);
            break;
        case SYS_VFS_READDIR:
            ret = syscall_vfs_readdir(r->ebx, (user_dirent_t*)r->ecx);
            break;
        case SYS_VFS_MKDIR:
            ret = syscall_vfs_mkdir((const char*)r->ebx, r->ecx);
            break;
        case SYS_VFS_RMDIR:
            ret = syscall_vfs_rmdir((const char*)r->ebx);
            break;
        case SYS_VFS_UNLINK:
            ret = syscall_vfs_unlink((const char*)r->ebx);
            break;
        case SYS_VFS_RENAME:
            ret = syscall_vfs_rename((const char*)r->ebx, (const char*)r->ecx);
            break;
        case SYS_VFS_CHDIR:
            ret = syscall_vfs_chdir((const char*)r->ebx);
            break;
        case SYS_VFS_GETCWD:
            ret = syscall_vfs_getcwd((char*)r->ebx, r->ecx);
            break;
        default:
            serial_puts("[SYSCALL] Unknown syscall: ");
            serial_puts_num(syscall_num);
            serial_puts("\n");
            break;
    }
    
    r->eax = ret;
}