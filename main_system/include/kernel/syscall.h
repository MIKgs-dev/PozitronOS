#ifndef SYSCALL_H
#define SYSCALL_H

#include "core/isr.h"
#include <stdint.h>
#include "gui/gui.h"
#include "core/event.h"

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
    uint32_t st_ino;
    uint32_t st_size;
    uint32_t st_mode;
    uint8_t  st_type;
    uint32_t st_nlink;
    uint32_t st_atime;
    uint32_t st_mtime;
    uint32_t st_ctime;
} vfs_stat_t;

typedef struct {
    uint32_t d_ino;
    uint8_t  d_type;
    uint8_t  d_namelen;
    char     d_name[256];
} user_dirent_t;

#define POWER_ACTION_SHUTDOWN 1
#define POWER_ACTION_REBOOT   2

void syscall_handler(registers_t* r);

#endif