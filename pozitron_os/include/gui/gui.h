#ifndef GUI_H
#define GUI_H

#include "../kernel/types.h"
#include "../drivers/vesa.h"
#include "../core/event.h"

// ============ БАЗОВЫЕ ТИПЫ ============
typedef struct Widget Widget;
typedef struct Window Window;

typedef enum {
    WIDGET_BUTTON,
    WIDGET_LABEL,
    WIDGET_INPUT,
    WIDGET_CHECKBOX,
    WIDGET_SLIDER,
    WIDGET_PROGRESSBAR,
    WIDGET_COMBOBOX,
    WIDGET_RADIO,
    WIDGET_WINDOW,
    WIDGET_CONTAINER
} WidgetType;

typedef enum {
    STATE_NORMAL,
    STATE_HOVER,
    STATE_PRESSED,
    STATE_FOCUSED,
    STATE_DISABLED
} WidgetState;

// ============ БАЗОВЫЙ ВИДЖЕТ ============
struct Widget {
    uint32_t id;
    WidgetType type;
    uint32_t x, y, width, height;        // Абсолютные координаты
    float rel_x, rel_y;                  // Относительные координаты (0.0 - 1.0)
    float rel_width, rel_height;         // Относительные размеры
    uint8_t visible;
    uint8_t enabled;
    Window* parent_window;
    Widget* next;
    WidgetState state;
    char* text;
    void* data;
    uint32_t data_size;
    void (*on_click)(Widget*, void* userdata);
    void (*on_hover)(Widget*, void* userdata);
    void (*on_leave)(Widget*, void* userdata);
    void* userdata;
    void (*draw)(Widget*);
    void (*handle_event)(Widget*, event_t*);
    uint8_t auto_update;
    uint32_t update_interval;
    uint32_t last_update;
    void (*update_callback)(struct Widget* widget);
    uint8_t needs_redraw : 1;
    uint8_t drag_enabled : 1;
    uint8_t resize_enabled : 1;
    uint8_t dragging : 1;
    uint8_t use_relative : 1;            // Использует относительные координаты
};

// ============ ОКНО ============
struct Window {
    uint32_t id;
    char* title;
    uint32_t x, y, width, height;
    uint32_t title_height;
    uint8_t visible;
    uint8_t has_titlebar;
    int32_t z_index;
    uint8_t focused;
    uint8_t dragging;
    uint8_t resizing;
    uint32_t drag_offset_x, drag_offset_y;
    uint8_t minimized;
    uint8_t maximized;
    uint32_t orig_x, orig_y, orig_width, orig_height;
    uint32_t normal_x, normal_y, normal_width, normal_height;
    uint8_t orig_movable;
    uint8_t orig_resizable;
    Widget* first_widget;
    Widget* last_widget;
    Window* next;
    Window* prev;
    void (*on_close)(Window*);
    void (*on_focus)(Window*);
    void (*on_minimize)(Window*);
    void (*on_maximize)(Window*);
    void (*on_restore)(Window*);
    void (*on_resize)(Window*);          // Callback при изменении размера
    uint8_t closable : 1;
    uint8_t movable : 1;
    uint8_t resizable : 1;
    uint8_t minimizable : 1;
    uint8_t maximizable : 1;
    uint8_t needs_redraw : 1;
    uint8_t in_taskbar : 1;
    uint8_t is_resizing : 1;
};

// ============ ПАНЕЛЬ ЗАДАЧ ============
#define TASKBAR_HEIGHT 32
#define START_BUTTON_WIDTH 80
#define TASKBAR_COLOR 0x2D2D30
#define TASKBAR_HIGHLIGHT 0x3E3E42
#define TASKBAR_SHADOW 0x252526
#define TASKBAR_TEXT_COLOR 0xF1F1F1
#define TASKBAR_BUTTON_COLOR 0x3E3E42
#define TASKBAR_BUTTON_HOVER 0x505054
#define TASKBAR_BUTTON_ACTIVE 0x007ACC

#define MAX_TASKBAR_BUTTONS 64
#define TASKBAR_BUTTON_WIDTH 160
#define TASKBAR_BUTTON_HEIGHT (TASKBAR_HEIGHT - 4)
#define TASKBAR_BUTTON_SPACING 2
#define TASKBAR_SCROLL_BUTTON_WIDTH 20
#define TASKBAR_CLOCK_WIDTH 60

#define WINDOW_BG_COLOR 0xF0F0F0
#define WINDOW_TITLE_COLOR 0x3E3E42
#define WINDOW_TITLE_ACTIVE 0x007ACC
#define WINDOW_BORDER_COLOR 0xD0D0D0
#define WINDOW_BUTTON_COLOR 0xE1E1E1
#define WINDOW_BUTTON_HOVER 0xD0D0D0
#define WINDOW_BUTTON_PRESSED 0xC0C0C0

// ============ ЦВЕТА ДЛЯ НОВЫХ ЭЛЕМЕНТОВ ============
#define SLIDER_TRACK_COLOR 0x808080
#define SLIDER_FILL_COLOR 0x007ACC
#define SLIDER_HANDLE_COLOR 0xFFFFFF
#define CHECKBOX_COLOR 0xFFFFFF
#define CHECKBOX_CHECKED_COLOR 0x007ACC
#define PROGRESSBAR_BG_COLOR 0xCCCCCC
#define PROGRESSBAR_FILL_COLOR 0x007ACC

// ============ БЕЗОПАСНЫЕ МАКРОСЫ ============
#define WINDOW_REGISTRY_SIZE 256
#define IS_VALID_WINDOW_PTR(win) \
    ((win) != NULL && (win)->id != 0 && \
     gui_state.window_registry[(win)->id % WINDOW_REGISTRY_SIZE] == (win))

// ============ GUI CORE API ============
void gui_init(uint32_t screen_width, uint32_t screen_height);
void gui_shutdown(void);
void gui_handle_event(event_t* event);
void gui_render(void);
void gui_force_redraw(void);
void gui_register_window(Window* window);
void gui_unregister_window(uint32_t window_id);
Window* gui_get_window_by_id(uint32_t id);

static inline size_t gui_strlen(const char* str) {
    size_t len = 0;
    if (!str) return 0;
    while (str[len]) len++;
    return len;
}

static inline char* gui_strcpy(char* dest, const char* src) {
    if (!dest || !src) return dest;
    char* ptr = dest;
    while (*src) *ptr++ = *src++;
    *ptr = '\0';
    return dest;
}

static inline char* gui_strncpy(char* dest, const char* src, size_t n) {
    if (!dest || !src || n == 0) return dest;
    size_t i;
    for (i = 0; i < n && src[i]; i++) dest[i] = src[i];
    for (; i < n; i++) dest[i] = '\0';
    return dest;
}

// ============ WINDOW MANAGER API ============
Window* wm_create_window(const char* title, uint32_t x, uint32_t y, 
                        uint32_t width, uint32_t height, uint8_t flags);
void wm_destroy_window(Window* window);
void wm_bring_to_front(Window* window);
void wm_focus_window(Window* window);
Window* wm_get_focused_window(void);
Window* wm_find_window_at(uint32_t x, uint32_t y);
void wm_move_window(Window* window, uint32_t x, uint32_t y);
void wm_close_window(Window* window);
void wm_minimize_window(Window* window);
void wm_maximize_window(Window* window);
void wm_restore_window(Window* window);
void wm_resize_window(Window* window, uint32_t width, uint32_t height); // НОВОЕ
void wm_dump_info(void);

void shutdown_dialog_callback(Widget* button, void* userdata);

#define WINDOW_CLOSABLE    0x01
#define WINDOW_MOVABLE     0x02
#define WINDOW_RESIZABLE   0x04
#define WINDOW_HAS_TITLE   0x08
#define WINDOW_MINIMIZABLE 0x10
#define WINDOW_MAXIMIZABLE 0x20

// ============ WIDGETS API ============
// Старые функции (абсолютные координаты) - для обратной совместимости
Widget* wg_create_button(Window* parent, const char* text, 
                        uint32_t x, uint32_t y, uint32_t width, uint32_t height);
Widget* wg_create_button_ex(Window* parent, const char* text, 
                           uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                           void (*callback)(Widget*, void*), void* userdata);
Widget* wg_create_label(Window* parent, const char* text, 
                       uint32_t x, uint32_t y);
Widget* wg_create_checkbox(Window* parent, const char* text, 
                          uint32_t x, uint32_t y, uint8_t checked);
Widget* wg_create_slider(Window* parent, uint32_t x, uint32_t y, 
                        uint32_t width, uint32_t min, uint32_t max, uint32_t value);
Widget* wg_create_progressbar(Window* parent, uint32_t x, uint32_t y, 
                             uint32_t width, uint32_t height, uint32_t value);

// НОВЫЕ функции с относительными координатами
Widget* wg_create_button_rel(Window* parent, const char* text,
                            float rel_x, float rel_y, 
                            float rel_width, float rel_height,
                            void (*callback)(Widget*, void*), void* userdata);
Widget* wg_create_label_rel(Window* parent, const char* text,
                           float rel_x, float rel_y);
Widget* wg_create_checkbox_rel(Window* parent, const char* text,
                              float rel_x, float rel_y, uint8_t checked);
Widget* wg_create_slider_rel(Window* parent, float rel_x, float rel_y,
                            float rel_width, float rel_height,
                            uint32_t min, uint32_t max, uint32_t value);
Widget* wg_create_progressbar_rel(Window* parent, float rel_x, float rel_y,
                                 float rel_width, float rel_height, uint32_t value);

void wg_destroy_widget(Widget* widget);
void wg_set_text(Widget* widget, const char* text);
void wg_set_callback_ex(Widget* widget, void (*callback)(Widget*, void*), void* userdata);

// Функции для работы с координатами
void wg_update_position(Widget* widget);                       // Обновить абсолютные координаты
void wg_set_relative_position(Widget* widget, float rel_x, float rel_y,
                             float rel_width, float rel_height); // Установить относительные
void wg_set_absolute_position(Widget* widget, uint32_t x, uint32_t y,
                             uint32_t width, uint32_t height);   // Установить абсолютные
void wg_update_all_widgets(Window* window);                     // Обновить все виджеты окна

// Функции для работы с новыми виджетами
uint8_t wg_get_checkbox_state(Widget* checkbox);
uint32_t wg_get_slider_value(Widget* slider);
void wg_set_slider_value(Widget* slider, uint32_t value);
void wg_set_progressbar_value(Widget* progressbar, uint32_t value);

// ============ УТИЛИТЫ ============
uint8_t point_in_rect(uint32_t px, uint32_t py, uint32_t x, uint32_t y, 
                     uint32_t w, uint32_t h);
uint32_t get_screen_width(void);
uint32_t get_screen_height(void);

// ============ ПАНЕЛЬ ЗАДАЧ ============
void taskbar_init(void);
void taskbar_render(void);
void taskbar_handle_event(event_t* event);
void taskbar_add_window(Window* window);
void taskbar_remove_window(Window* window);
void taskbar_update_window(Window* window);
void start_menu_create(void);
void start_menu_toggle(void);
void start_menu_close(void);
uint8_t start_menu_is_visible(void);
Window* start_menu_get_window(void);

void taskbar_scroll_left(void);
void taskbar_scroll_right(void);
uint32_t taskbar_get_scroll_offset(void);
uint32_t taskbar_get_visible_button_count(void);
uint32_t taskbar_get_total_button_count(void);

// Глобальное состояние GUI
extern struct GUI_State {
    uint32_t screen_width;
    uint32_t screen_height;
    Window* first_window;
    Window* last_window;
    Window* focused_window;
    Window* dragging_window;
    uint32_t window_count;
    uint32_t next_window_id;
    uint32_t next_widget_id;
    uint8_t initialized;
    uint8_t debug_mode;
    Window* window_registry[WINDOW_REGISTRY_SIZE];
} gui_state;

#include "shutdown.h"

#endif