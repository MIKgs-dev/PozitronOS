#ifndef POZITRON_H
#define POZITRON_H

#include <stdint.h>
#include <stddef.h>

#define SYS_EXIT             1
#define SYS_SERIAL_WRITE     2
#define SYS_CREATE_WINDOW    3
#define SYS_CLOSE_WINDOW     4
#define SYS_SET_WINDOW_TITLE 5
#define SYS_MSLEEP           6
#define SYS_CREATE_LABEL     7
#define SYS_SET_WIDGET_TEXT  8
#define SYS_CREATE_BUTTON    9
#define SYS_POLL_EVENT       10
#define SYS_POWER_CONTROL    11
#define SYS_VFS_OPEN         12
#define SYS_VFS_READ         13
#define SYS_VFS_CLOSE        14
#define SYS_VFS_WRITE        15
#define SYS_SHOW_NOTIF       16
#define SYS_ENABLE_CANVAS    17
#define SYS_DISABLE_CANVAS   18
#define SYS_DRAW_PIXEL       19
#define SYS_DRAW_RECT        20
#define SYS_FLUSH_WINDOW     21
#define SYS_CANVAS_FILL      22
#define SYS_GET_WINDOW_INFO  23
#define SYS_DRAW_BLOCK_2x2   24
#define SYS_DRAW_BLOCK_4x4   25
#define SYS_DRAW_BLOCK_8x8   26
#define SYS_DRAW_BLOCK       27
#define SYS_ENABLE_INPUT     28
#define SYS_POLL_INPUT       29
#define SYS_WAIT_INPUT       30
#define SYS_VFS_STAT         31
#define SYS_VFS_LSEEK        32
#define SYS_VFS_READDIR      33
#define SYS_VFS_MKDIR        34
#define SYS_VFS_RMDIR        35
#define SYS_VFS_UNLINK       36
#define SYS_VFS_RENAME       37
#define SYS_VFS_CHDIR        38
#define SYS_VFS_GETCWD       39
#define SYS_VFS_SYMLINK      40

#define POWER_ACTION_SHUTDOWN 1
#define POWER_ACTION_REBOOT   2

#define WINDOW_CLOSABLE    0x01
#define WINDOW_MOVABLE     0x02
#define WINDOW_RESIZABLE   0x04
#define WINDOW_HAS_TITLE   0x08
#define WINDOW_MINIMIZABLE 0x10
#define WINDOW_MAXIMIZABLE 0x20

#define POZ_TEXT_ALIGN_LEFT    0x00
#define POZ_TEXT_ALIGN_CENTER  0x01
#define POZ_TEXT_ALIGN_RIGHT   0x02
#define POZ_TEXT_ALIGN_TOP     0x00
#define POZ_TEXT_ALIGN_MIDDLE  0x04
#define POZ_TEXT_ALIGN_BOTTOM  0x08

#define O_RDONLY         0x0001
#define O_WRONLY         0x0002
#define O_RDWR           0x0003
#define O_CREAT          0x0100
#define O_TRUNC          0x0200
#define O_APPEND         0x0400
#define O_DIRECTORY      0x1000

#define POZ_EVENT_BUTTON_CLICK 9

#define POZ_KEY_ESC          0x01
#define POZ_KEY_1            0x02
#define POZ_KEY_2            0x03
#define POZ_KEY_3            0x04
#define POZ_KEY_4            0x05
#define POZ_KEY_5            0x06
#define POZ_KEY_6            0x07
#define POZ_KEY_7            0x08
#define POZ_KEY_8            0x09
#define POZ_KEY_9            0x0A
#define POZ_KEY_0            0x0B
#define POZ_KEY_MINUS        0x0C
#define POZ_KEY_EQUAL        0x0D
#define POZ_KEY_BACKSPACE    0x0E
#define POZ_KEY_TAB          0x0F
#define POZ_KEY_Q            0x10
#define POZ_KEY_W            0x11
#define POZ_KEY_E            0x12
#define POZ_KEY_R            0x13
#define POZ_KEY_T            0x14
#define POZ_KEY_Y            0x15
#define POZ_KEY_U            0x16
#define POZ_KEY_I            0x17
#define POZ_KEY_O            0x18
#define POZ_KEY_P            0x19
#define POZ_KEY_LBRACKET     0x1A
#define POZ_KEY_RBRACKET     0x1B
#define POZ_KEY_ENTER        0x1C
#define POZ_KEY_LCTRL        0x1D
#define POZ_KEY_A            0x1E
#define POZ_KEY_S            0x1F
#define POZ_KEY_D            0x20
#define POZ_KEY_F            0x21
#define POZ_KEY_G            0x22
#define POZ_KEY_H            0x23
#define POZ_KEY_J            0x24
#define POZ_KEY_K            0x25
#define POZ_KEY_L            0x26
#define POZ_KEY_SEMICOLON    0x27
#define POZ_KEY_QUOTE        0x28
#define POZ_KEY_BACKTICK     0x29
#define POZ_KEY_LSHIFT       0x2A
#define POZ_KEY_BACKSLASH    0x2B
#define POZ_KEY_Z            0x2C
#define POZ_KEY_X            0x2D
#define POZ_KEY_C            0x2E
#define POZ_KEY_V            0x2F
#define POZ_KEY_B            0x30
#define POZ_KEY_N            0x31
#define POZ_KEY_M            0x32
#define POZ_KEY_COMMA        0x33
#define POZ_KEY_DOT          0x34
#define POZ_KEY_SLASH        0x35
#define POZ_KEY_RSHIFT       0x36
#define POZ_KEY_KP_STAR      0x37
#define POZ_KEY_LALT         0x38
#define POZ_KEY_SPACE        0x39
#define POZ_KEY_CAPSLOCK     0x3A
#define POZ_KEY_F1           0x3B
#define POZ_KEY_F2           0x3C
#define POZ_KEY_F3           0x3D
#define POZ_KEY_F4           0x3E
#define POZ_KEY_F5           0x3F
#define POZ_KEY_F6           0x40
#define POZ_KEY_F7           0x41
#define POZ_KEY_F8           0x42
#define POZ_KEY_F9           0x43
#define POZ_KEY_F10          0x44
#define POZ_KEY_NUMLOCK      0x45
#define POZ_KEY_SCROLLLOCK   0x46
#define POZ_KEY_HOME         0x47
#define POZ_KEY_KP_7         0x47
#define POZ_KEY_UP           0x48
#define POZ_KEY_KP_8         0x48
#define POZ_KEY_PAGEUP       0x49
#define POZ_KEY_KP_9         0x49
#define POZ_KEY_KP_MINUS     0x4A
#define POZ_KEY_LEFT         0x4B
#define POZ_KEY_KP_4         0x4B
#define POZ_KEY_KP_5         0x4C
#define POZ_KEY_RIGHT        0x4D
#define POZ_KEY_KP_6         0x4D
#define POZ_KEY_KP_PLUS      0x4E
#define POZ_KEY_END          0x4F
#define POZ_KEY_KP_1         0x4F
#define POZ_KEY_DOWN         0x50
#define POZ_KEY_KP_2         0x50
#define POZ_KEY_PAGEDOWN     0x51
#define POZ_KEY_KP_3         0x51
#define POZ_KEY_INSERT       0x52
#define POZ_KEY_KP_0         0x52
#define POZ_KEY_DELETE       0x53
#define POZ_KEY_KP_DOT       0x53
#define POZ_KEY_F11          0x57
#define POZ_KEY_F12          0x58

#define POZ_IS_KEY_PRESSED(scancode, key)  ((scancode) == (key))
#define POZ_KEY_RELEASED(scancode)         ((scancode) & 0x80)
#define POZ_KEY_CODE(scancode)             ((scancode) & 0x7F)

typedef struct {
    const char* title;
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    uint8_t flags;
} win_params_t;

typedef struct {
    uint32_t window_id;
    const char* text;
    float rel_x;
    float rel_y;
    uint8_t align;
} label_params_t;

typedef struct {
    uint32_t window_id;
    const char* text;
    float rel_x;
    float rel_y;
    float rel_width;
    float rel_height;
    uint8_t align;
} button_params_t;

typedef struct {
    uint32_t type;
    uint32_t data1;
    uint32_t data2;
    uint32_t timestamp;
    union {
        struct {
            uint32_t x, y;
            uint32_t delta;
            uint8_t button;
        } mouse;
        struct {
            uint8_t scancode;
            uint8_t ascii;
            uint8_t modifiers;
        } key;
        struct {
            uint32_t from_id;
            uint32_t to_id;
        } focus;
        struct {
            uint32_t window_id;
            uint32_t widget_id;
        } target;
    };
} event_t;

typedef enum {
    POZ_NOTIF_INFO = 0,
    POZ_NOTIF_WARNING,
    POZ_NOTIF_ERROR
} pozitron_notif_t;

typedef struct {
    uint32_t window_id;
    int32_t x, y;
    uint32_t width, height;
    uint32_t client_width;
    uint32_t client_height;
    uint8_t is_visible;
    uint8_t is_minimized;
    uint8_t is_maximized;
    uint8_t is_focused;
    uint8_t has_titlebar;
    uint32_t title_height;
} window_info_t;

typedef struct {
    uint32_t type;
    uint32_t window_id;
    uint32_t timestamp;
    union {
        struct {
            uint8_t scancode;
            uint8_t ascii;
            uint8_t modifiers;
        } key;
        struct {
            int32_t x;
            int32_t y;
            int32_t delta_x;
            int32_t delta_y;
            uint8_t buttons;
            int8_t wheel;
        } mouse;
    };
} input_event_t;

typedef struct {
    uint32_t d_ino;
    uint8_t  d_type;
    uint8_t  d_namelen;
    char     d_name[256];
} pozitron_dirent_t;

typedef struct {
    uint32_t st_ino;
    uint32_t st_size;
    uint32_t st_mode;
    uint8_t  st_type;
    uint32_t st_nlink;
    uint32_t st_atime;
    uint32_t st_mtime;
    uint32_t st_ctime;
} vfs_stat_t;

#define POZ_PI 3.14159265359f
#define POZ_PI_2 1.57079632679f
#define POZ_PI_4 0.7853981634f

#define sin poz_sin
#define cos poz_cos
#define sqrt poz_sqrt
#define atan2 poz_atan2
#define fabs poz_fabs

#define POZ_EVENT_KEY_DOWN    100
#define POZ_EVENT_KEY_UP      101
#define POZ_EVENT_MOUSE_MOVE  102
#define POZ_EVENT_MOUSE_BUTTON 103
#define POZ_EVENT_MOUSE_WHEEL 104

#define POZ_MOD_SHIFT   0x01
#define POZ_MOD_CTRL    0x02
#define POZ_MOD_ALT     0x04
#define POZ_MOD_CAPS    0x08
#define POZ_MOD_NUM     0x10

static inline float poz_fabs(float x) {
    return (x < 0) ? -x : x;
}

static inline float poz_sin(float x) {
    while (x > 3.14159265359f) x -= 6.28318530718f;
    while (x < -3.14159265359f) x += 6.28318530718f;
    
    float x2 = x * x;
    float x3 = x2 * x;
    float x5 = x3 * x2;
    float x7 = x5 * x2;
    float x9 = x7 * x2;
    
    return x - x3/6.0f + x5/120.0f - x7/5040.0f + x9/362880.0f;
}

static inline float poz_cos(float x) {
    return poz_sin(x + 1.57079632679f);
}

static inline float poz_sqrt(float x) {
    if (x <= 0) return 0;
    
    float guess = x;
    float prev = 0;
    int iter = 0;
    
    while (poz_fabs(guess - prev) > 0.001f && iter < 20) {
        prev = guess;
        guess = (guess + x / guess) / 2;
        iter++;
    }
    
    return guess;
}

static inline float poz_atan2(float y, float x) {
    if (x == 0 && y == 0) return 0;
    
    float angle;
    float abs_y = poz_fabs(y);
    float abs_x = poz_fabs(x);
    
    if (abs_x > abs_y) {
        float ratio = abs_y / abs_x;
        angle = 0.7853981634f * ratio - ratio * (ratio - 1) * (0.2447f + 0.0663f * ratio);
    } else {
        float ratio = abs_x / abs_y;
        angle = 1.57079632679f - 0.7853981634f * ratio + ratio * (ratio - 1) * (0.2447f + 0.0663f * ratio);
    }
    
    if (x < 0 && y >= 0) angle = 3.14159265359f - angle;
    if (x < 0 && y < 0) angle = -3.14159265359f + angle;
    if (x >= 0 && y < 0) angle = -angle;
    
    return angle;
}

static inline int poz_abs(int value) {
    return value < 0 ? -value : value;
}

static inline char* pozitron_itoa(int value, char* str, int base) {
    if (base < 2 || base > 36) { *str = '\0'; return str; }
    
    char* ptr = str, *ptr1 = str, tmp_char;
    int tmp_value;

    do {
        tmp_value = value;
        value /= base;
        *ptr++ = "zyxwvutsrqponmlkjihgfedcba9876543210123456789abcdefghijklmnopqrstuvwxyz"[35 + (tmp_value - value * base)];
    } while (value);

    if (tmp_value < 0) *ptr++ = '-';
    *ptr-- = '\0';
    
    while (ptr1 < ptr) {
        tmp_char = *ptr;
        *ptr-- = *ptr1;
        *ptr1++ = tmp_char;
    }
    return str;
}

static inline char* pozitron_ftoa(float value, char* str, int precision) {
    char* ptr = str;
    
    if (value < 0) {
        *ptr++ = '-';
        value = -value;
    }

    int32_t ipart = (int32_t)value;
    
    pozitron_itoa(ipart, ptr, 10);
    
    while (*ptr) ptr++;
    
    if (precision > 0) {
        *ptr++ = '.';
        
        float fpart = value - (float)ipart;
        
        for (int i = 0; i < precision; i++) {
            fpart *= 10.0f;
        }
        
        int32_t ifpart = (int32_t)(fpart + 0.5f);
        
        char f_buf[16];
        pozitron_itoa(ifpart, f_buf, 10);
        
        int f_len = 0;
        while (f_buf[f_len]) f_len++;
        
        for (int i = 0; i < (precision - f_len); i++) {
            *ptr++ = '0';
        }
        
        char* f_ptr = f_buf;
        while (*f_ptr) *ptr++ = *f_ptr++;
    }
    
    *ptr = '\0';
    return str;
}

static inline float pozitron_atof(const char* s) {
    float rez = 0.0f;
    float fact = 1.0f;
    int point_seen = 0;
    
    if (*s == '-') {
        fact = -1.0f;
        s++;
    }
    
    for (int c = *s; c; c = *++s) {
        if (c == '.') {
            point_seen = 1;
            continue;
        }
        int d = c - '0';
        if (d >= 0 && d <= 9) {
            if (point_seen) fact /= 10.0f;
            rez = rez * 10.0f + (float)d;
        }
    }
    return rez * fact;
}

static inline int syscall1(int num, int arg1) {
    int ret;
    asm volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(arg1) : "memory", "cc");
    return ret;
}

static inline int syscall2(int num, int arg1, int arg2) {
    int ret;
    asm volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(arg1), "c"(arg2) : "memory", "cc");
    return ret;
}

static inline int syscall3(int num, int arg1, int arg2, int arg3) {
    int ret;
    asm volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(arg1), "c"(arg2), "d"(arg3) : "memory", "cc");
    return ret;
}

static inline int syscall4(int num, int arg1, int arg2, int arg3, int arg4) {
    int ret;
    asm volatile("int $0x80" 
                 : "=a"(ret) 
                 : "a"(num), "b"(arg1), "c"(arg2), "d"(arg3), "S"(arg4) 
                 : "memory", "cc");
    return ret;
}

static inline int syscall5(int num, int arg1, int arg2, int arg3, int arg4, int arg5) {
    int ret;
    asm volatile("int $0x80" 
                 : "=a"(ret) 
                 : "a"(num), "b"(arg1), "c"(arg2), "d"(arg3), "S"(arg4), "D"(arg5) 
                 : "memory", "cc");
    return ret;
}

static inline void pozitron_exit(int code) {
    syscall1(SYS_EXIT, code);
    while(1);
}

static inline int pozitron_write(const char* str) {
    int len = 0;
    while(str[len]) len++;
    return syscall2(SYS_SERIAL_WRITE, (int)str, len);
}

static inline int pozitron_create_window(win_params_t* params) {
    return syscall1(SYS_CREATE_WINDOW, (int)params);
}

static inline int pozitron_close_window(int window_id) {
    return syscall1(SYS_CLOSE_WINDOW, window_id);
}

static inline int pozitron_set_window_title(int window_id, const char* title) {
    return syscall2(SYS_SET_WINDOW_TITLE, window_id, (int)title);
}

static inline int pozitron_msleep(uint32_t ms) {
    return syscall1(SYS_MSLEEP, (int)ms);
}

static inline int pozitron_create_label(label_params_t* params) {
    return syscall1(SYS_CREATE_LABEL, (int)params);
}

static inline int pozitron_set_widget_text(int widget_id, const char* text) {
    return syscall2(SYS_SET_WIDGET_TEXT, widget_id, (int)text);
}

static inline int pozitron_create_button(button_params_t* params) {
    return syscall1(SYS_CREATE_BUTTON, (int)params);
}

static inline int pozitron_poll_event(int window_id, event_t* event) {
    return syscall2(SYS_POLL_EVENT, window_id, (int)event);
}

static inline int pozitron_power_control(uint32_t action) {
    return syscall1(SYS_POWER_CONTROL, (int)action);
}

static inline void pozitron_shutdown(void) {
    pozitron_power_control(POWER_ACTION_SHUTDOWN);
    while(1);
}

static inline void pozitron_reboot(void) {
    pozitron_power_control(POWER_ACTION_REBOOT);
    while(1);
}

static inline int pozitron_vfs_open(const char* path, int flags) {
    return syscall2(SYS_VFS_OPEN, (int)path, flags);
}

static inline int pozitron_vfs_read(int fd, void* buf, uint32_t size) {
    return syscall3(SYS_VFS_READ, fd, (int)buf, (int)size);
}

static inline int pozitron_vfs_write(int fd, const void* buf, uint32_t size) {
    return syscall3(SYS_VFS_WRITE, fd, (int)buf, (int)size);
}

static inline int pozitron_vfs_close(int fd) {
    return syscall1(SYS_VFS_CLOSE, fd);
}

static inline int pozitron_vfs_create(const char* path) {
    return pozitron_vfs_open(path, O_WRONLY | O_CREAT | O_TRUNC);
}

static inline int pozitron_show_notif(pozitron_notif_t type, const char* title, const char* message) {
    return syscall3(SYS_SHOW_NOTIF, (int)type, (int)title, (int)message);
}

static inline int pozitron_enable_canvas(int window_id) {
    return syscall1(SYS_ENABLE_CANVAS, window_id);
}

static inline int pozitron_disable_canvas(int window_id) {
    return syscall1(SYS_DISABLE_CANVAS, window_id);
}

static inline int pozitron_draw_pixel(int window_id, int x, int y, uint32_t color) {
    return syscall3(SYS_DRAW_PIXEL, window_id, (x << 16) | (y & 0xFFFF), color);
}

static inline int pozitron_draw_rect(int window_id, int x, int y, int w, int h, uint32_t color) {
    uint32_t packed = (x & 0xFF) | ((y & 0xFF) << 8) | ((w & 0xFF) << 16) | ((h & 0xFF) << 24);
    return syscall3(SYS_DRAW_RECT, window_id, packed, color);
}

static inline int pozitron_flush_window(int window_id) {
    return syscall1(SYS_FLUSH_WINDOW, window_id);
}

static inline int pozitron_canvas_fill(int window_id, uint32_t color) {
    return syscall2(SYS_CANVAS_FILL, window_id, color);
}

static inline int pozitron_get_window_info(int window_id, window_info_t* info) {
    return syscall2(SYS_GET_WINDOW_INFO, window_id, (int)info);
}

static inline int pozitron_draw_block_2x2(int window_id, int x, int y, uint32_t color) {
    return syscall3(SYS_DRAW_BLOCK_2x2, window_id, (x << 16) | (y & 0xFFFF), color);
}

static inline int pozitron_draw_block_4x4(int window_id, int x, int y, uint32_t color) {
    return syscall3(SYS_DRAW_BLOCK_4x4, window_id, (x << 16) | (y & 0xFFFF), color);
}

static inline int pozitron_draw_block_8x8(int window_id, int x, int y, uint32_t color) {
    return syscall3(SYS_DRAW_BLOCK_8x8, window_id, (x << 16) | (y & 0xFFFF), color);
}

static inline int pozitron_draw_block(int window_id, int x, int y, int w, int h, uint32_t color) {
    uint32_t packed = (x & 0xFF) | ((y & 0xFF) << 8) | ((w & 0xFF) << 16) | ((h & 0xFF) << 24);
    return syscall3(SYS_DRAW_BLOCK, window_id, packed, color);
}

static inline int pozitron_enable_input(int window_id, uint8_t enable) {
    return syscall2(SYS_ENABLE_INPUT, window_id, enable);
}

static inline int pozitron_poll_input(int window_id, input_event_t* event) {
    return syscall2(SYS_POLL_INPUT, window_id, (int)event);
}

static inline int pozitron_wait_input(int window_id, input_event_t* event, uint32_t timeout_ms) {
    return syscall3(SYS_WAIT_INPUT, window_id, (int)event, timeout_ms);
}

static inline uint8_t pozitron_is_shift(uint8_t mods) { return mods & POZ_MOD_SHIFT; }
static inline uint8_t pozitron_is_ctrl(uint8_t mods)  { return mods & POZ_MOD_CTRL; }
static inline uint8_t pozitron_is_alt(uint8_t mods)   { return mods & POZ_MOD_ALT; }
static inline uint8_t pozitron_left_button(uint8_t buttons)   { return buttons & 0x01; }
static inline uint8_t pozitron_right_button(uint8_t buttons)  { return buttons & 0x02; }
static inline uint8_t pozitron_middle_button(uint8_t buttons) { return buttons & 0x04; }

static inline int pozitron_vfs_stat(const char* path, vfs_stat_t* stat) {
    return syscall2(SYS_VFS_STAT, (int)path, (int)stat);
}

static inline int pozitron_vfs_lseek(int fd, uint32_t offset, int whence) {
    return syscall3(SYS_VFS_LSEEK, fd, offset, whence);
}

static inline int pozitron_vfs_readdir(int fd, pozitron_dirent_t* dirent) {
    return syscall2(SYS_VFS_READDIR, fd, (int)dirent);
}

static inline int pozitron_vfs_mkdir(const char* path, uint32_t mode) {
    return syscall2(SYS_VFS_MKDIR, (int)path, mode);
}

static inline int pozitron_vfs_rmdir(const char* path) {
    return syscall1(SYS_VFS_RMDIR, (int)path);
}

static inline int pozitron_vfs_unlink(const char* path) {
    return syscall1(SYS_VFS_UNLINK, (int)path);
}

static inline int pozitron_vfs_rename(const char* old_path, const char* new_path) {
    return syscall2(SYS_VFS_RENAME, (int)old_path, (int)new_path);
}

static inline int pozitron_vfs_chdir(const char* path) {
    return syscall1(SYS_VFS_CHDIR, (int)path);
}

static inline int pozitron_vfs_getcwd(char* buf, uint32_t size) {
    return syscall2(SYS_VFS_GETCWD, (int)buf, size);
}

#endif