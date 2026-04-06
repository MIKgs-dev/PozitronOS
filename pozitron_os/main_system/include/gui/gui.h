#ifndef GUI_H
#define GUI_H

#include "../kernel/types.h"
#include "../drivers/vesa.h"
#include "../core/event.h"

#define TEXT_ALIGN_LEFT     0x00
#define TEXT_ALIGN_CENTER   0x01
#define TEXT_ALIGN_RIGHT    0x02
#define TEXT_ALIGN_TOP      0x00
#define TEXT_ALIGN_MIDDLE   0x04
#define TEXT_ALIGN_BOTTOM   0x08

#define TEXT_ALIGN_CENTER_MIDDLE (TEXT_ALIGN_CENTER | TEXT_ALIGN_MIDDLE)

typedef struct Widget Widget;
typedef struct Window Window;

typedef enum {
    WIDGET_BUTTON,
    WIDGET_LABEL,
    WIDGET_INPUT,
    WIDGET_CHECKBOX,
    WIDGET_SLIDER,
    WIDGET_PROGRESSBAR,
    WIDGET_LIST,
    WIDGET_LIST_ITEM,
    WIDGET_DROPDOWN,
    WIDGET_SCROLLBAR,
    WIDGET_MENUBAR,
    WIDGET_MENU_ITEM,
    WIDGET_TAB,
    WIDGET_TAB_PAGE,
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
    STATE_DISABLED,
    STATE_SELECTED,
    STATE_ACTIVE
} WidgetState;

typedef struct {
    uint32_t id;
    char text[64];
    uint8_t selected;
    uint8_t enabled;
    void* data;
    void (*on_activate)(void* data);
} ListItem;

typedef struct {
    ListItem** items;
    uint32_t item_count;
    uint32_t visible_items;
    uint32_t scroll_offset;
    uint32_t selected_index;
    uint8_t multi_select;
    uint8_t show_scrollbar;
} ListData;

typedef struct {
    ListData* list;
    uint8_t expanded;
    uint32_t dropdown_height;
} DropdownData;

typedef struct {
    uint32_t min;
    uint32_t max;
    uint32_t value;
    uint32_t page_size;
    uint8_t vertical;
    uint8_t dragging;
    uint32_t drag_start_pos;
    uint32_t drag_start_value;
} ScrollbarData;

typedef struct {
    char* buffer;
    uint32_t buffer_size;
    uint32_t cursor_pos;
    uint32_t selection_start;
    uint32_t selection_end;
    uint8_t cursor_visible;
    uint32_t cursor_blink_time;
    uint8_t password_mode;
    char password_char;
    uint32_t scroll_offset;
    uint8_t multiline;
    uint32_t line_height;
    uint32_t visible_lines;
    int32_t drag_start_x;
    int32_t drag_start_y;
    uint8_t drag_threshold_passed;
} InputData;

typedef struct {
    ListItem** menus;
    uint32_t menu_count;
    uint32_t active_menu;
    uint8_t menu_open;
} MenubarData;

typedef struct {
    ListItem** tabs;
    uint32_t tab_count;
    uint32_t active_tab;
} TabData;

struct Widget {
    uint32_t id;
    WidgetType type;
    uint32_t x, y, width, height;
    float rel_x, rel_y;
    float rel_width, rel_height;
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
    void (*on_change)(Widget*, void* userdata);
    void (*on_focus)(Widget*);
    void (*on_blur)(Widget*);
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
    uint8_t use_relative : 1;
    uint8_t can_focus : 1;
    uint8_t focused : 1;
    uint8_t text_align;
};

struct Window {
    uint32_t id;
    char* title;
    int32_t x, y;
    uint32_t width, height;
    uint32_t title_height;
    uint8_t visible;
    uint8_t has_titlebar;
    int32_t z_index;
    uint8_t focused;
    uint8_t dragging;
    uint8_t resizing;
    int32_t drag_offset_x, drag_offset_y;
    uint32_t resize_corner;
    int32_t resize_start_x, resize_start_y;
    uint32_t resize_start_width, resize_start_height;
    uint8_t minimized;
    uint8_t maximized;
    int32_t orig_x, orig_y;
    uint32_t orig_width, orig_height;
    int32_t normal_x, normal_y;
    uint32_t normal_width, normal_height;
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
    void (*on_resize)(Window*);
    uint8_t closable : 1;
    uint8_t movable : 1;
    uint8_t resizable : 1;
    uint8_t minimizable : 1;
    uint8_t maximizable : 1;
    uint8_t needs_redraw : 1;
    uint8_t in_taskbar : 1;
    uint8_t is_resizing : 1;
    Widget* focused_widget;
};

#define TASKBAR_HEIGHT 32
#define START_BUTTON_WIDTH 80
#define TASKBAR_COLOR 0x2D2D30
#define TASKBAR_HIGHLIGHT 0x3E3E42
#define TASKBAR_SHADOW 0x252526
#define TASKBAR_TEXT_COLOR 0xF1F1F1
#define TASKBAR_BUTTON_COLOR 0x3E3E42
#define TASKBAR_BUTTON_HOVER 0x505054
#define TASKBAR_BUTTON_ACTIVE 0x007ACC

#define MAX_TASKBAR_BUTTONS 70
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

#define SLIDER_TRACK_COLOR 0x808080
#define SLIDER_FILL_COLOR 0x007ACC
#define SLIDER_HANDLE_COLOR 0xFFFFFF
#define CHECKBOX_COLOR 0xFFFFFF
#define CHECKBOX_CHECKED_COLOR 0x007ACC
#define PROGRESSBAR_BG_COLOR 0xCCCCCC
#define PROGRESSBAR_FILL_COLOR 0x007ACC
#define INPUT_BG_COLOR 0xFFFFFF
#define INPUT_TEXT_COLOR 0x000000
#define INPUT_SELECTION_COLOR 0x007ACC
#define LIST_BG_COLOR 0xFFFFFF
#define LIST_TEXT_COLOR 0x000000
#define LIST_SELECTED_COLOR 0x007ACC
#define LIST_HOVER_COLOR 0xE0E0E0
#define MENUBAR_BG_COLOR 0xE0E0E0
#define MENUBAR_TEXT_COLOR 0x000000
#define MENUBAR_HOVER_COLOR 0xC0C0C0
#define MENUBAR_ACTIVE_COLOR 0x007ACC
#define SCROLLBAR_TRACK_COLOR 0xCCCCCC
#define SCROLLBAR_HANDLE_COLOR 0x808080
#define SCROLLBAR_HANDLE_HOVER 0x606060
#define SCROLLBAR_BUTTON_COLOR 0xE0E0E0

#define WINDOW_REGISTRY_SIZE 256
#define IS_VALID_WINDOW_PTR(win) \
    ((win) != NULL && (win)->id != 0 && \
     gui_state.window_registry[(win)->id % WINDOW_REGISTRY_SIZE] == (win))

static inline uint8_t point_in_rect(int32_t px, int32_t py, int32_t x, int32_t y, 
                                    uint32_t w, uint32_t h) {
    return (px >= x && px < x + (int32_t)w && py >= y && py < y + (int32_t)h);
}

void gui_init(uint32_t screen_width, uint32_t screen_height);
void gui_shutdown(void);
void gui_handle_event(event_t* event);
void gui_render(void);
void gui_force_redraw(void);
void gui_register_window(Window* window);
void gui_unregister_window(uint32_t window_id);
Window* gui_get_window_by_id(uint32_t id);

void gui_set_focus(Widget* widget);
void gui_clear_focus(void);
Widget* gui_get_focused_widget(void);
void gui_focus_next(void);
void gui_focus_prev(void);

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

Window* wm_create_window(const char* title, uint32_t x, uint32_t y, 
                        uint32_t width, uint32_t height, uint8_t flags);
void wm_destroy_window(Window* window);
void wm_bring_to_front(Window* window);
void wm_focus_window(Window* window);
Window* wm_get_focused_window(void);
Window* wm_find_window_at(uint32_t x, uint32_t y);
void wm_move_window(Window* window, int32_t x, int32_t y);
void wm_close_window(Window* window);
void wm_minimize_window(Window* window);
void wm_maximize_window(Window* window);
void wm_restore_window(Window* window);
void wm_resize_window(Window* window, uint32_t width, uint32_t height);
void wm_start_resize(Window* window, uint32_t corner, int32_t mouse_x, int32_t mouse_y);
void wm_do_resize(Window* window, int32_t mouse_x, int32_t mouse_y);
void wm_end_resize(Window* window);
void wm_dump_info(void);

void shutdown_dialog_callback(Widget* button, void* userdata);

#define WINDOW_CLOSABLE    0x01
#define WINDOW_MOVABLE     0x02
#define WINDOW_RESIZABLE   0x04
#define WINDOW_HAS_TITLE   0x08
#define WINDOW_MINIMIZABLE 0x10
#define WINDOW_MAXIMIZABLE 0x20

Widget* wg_create_button(Window* parent, const char* text,
                        float rel_x, float rel_y, 
                        float rel_width, float rel_height,
                        void (*callback)(Widget*, void*), void* userdata);

Widget* wg_create_button_ex(Window* parent, const char* text,
                           float rel_x, float rel_y, 
                           float rel_width, float rel_height,
                           uint8_t align,
                           void (*callback)(Widget*, void*), void* userdata);

Widget* wg_create_label(Window* parent, const char* text,
                       float rel_x, float rel_y);

Widget* wg_create_label_ex(Window* parent, const char* text,
                          float rel_x, float rel_y, uint8_t align);

Widget* wg_create_checkbox(Window* parent, const char* text,
                          float rel_x, float rel_y, uint8_t checked);

Widget* wg_create_slider(Window* parent, float rel_x, float rel_y,
                        float rel_width, float rel_height,
                        uint32_t min, uint32_t max, uint32_t value);

Widget* wg_create_progressbar(Window* parent, float rel_x, float rel_y,
                             float rel_width, float rel_height, uint32_t value);

Widget* wg_create_input(Window* parent, float rel_x, float rel_y,
                       float rel_width, float rel_height, const char* initial_text);

Widget* wg_create_list(Window* parent, float rel_x, float rel_y,
                      float rel_width, float rel_height, uint32_t visible_items);

Widget* wg_create_dropdown(Window* parent, float rel_x, float rel_y,
                          float rel_width, float rel_height, uint32_t visible_items);

Widget* wg_create_scrollbar(Window* parent, float rel_x, float rel_y,
                           float rel_width, float rel_height, uint8_t vertical);

Widget* wg_create_menubar(Window* parent, float rel_x, float rel_y, float rel_width);

Widget* wg_create_tab(Window* parent, float rel_x, float rel_y,
                     float rel_width, float rel_height);

void wg_set_text_align(Widget* widget, uint8_t align);
void wg_input_set_text(Widget* input, const char* text);
void wg_input_insert_char(Widget* input, char c);
void wg_input_delete_char(Widget* input);
void wg_input_backspace(Widget* input);
void wg_input_cursor_left(Widget* input);
void wg_input_cursor_right(Widget* input);
void wg_input_cursor_home(Widget* input);
void wg_input_cursor_end(Widget* input);
void wg_input_select_all(Widget* input);
char* wg_input_get_text(Widget* input);

uint32_t wg_list_add_item(Widget* list, const char* text, void* data);
void wg_list_remove_item(Widget* list, uint32_t index);
void wg_list_clear(Widget* list);
uint32_t wg_list_get_selected(Widget* list);
void wg_list_set_selected(Widget* list, uint32_t index);
void wg_list_scroll_to(Widget* list, uint32_t index);
void wg_list_scroll(Widget* list, int32_t delta);

void wg_dropdown_add_item(Widget* dropdown, const char* text, void* data);
uint32_t wg_dropdown_get_selected(Widget* dropdown);
void wg_dropdown_set_selected(Widget* dropdown, uint32_t index);
void wg_dropdown_expand(Widget* dropdown);
void wg_dropdown_collapse(Widget* dropdown);

void wg_scrollbar_set_range(Widget* scrollbar, uint32_t min, uint32_t max, uint32_t page);
void wg_scrollbar_set_value(Widget* scrollbar, uint32_t value);
uint32_t wg_scrollbar_get_value(Widget* scrollbar);

uint32_t wg_menubar_add_menu(Widget* menubar, const char* text);
void wg_menubar_add_item(Widget* menubar, uint32_t menu_index, const char* text, 
                        void (*callback)(void*), void* data);

uint32_t wg_tab_add_page(Widget* tab, const char* title);
void wg_tab_set_active(Widget* tab, uint32_t index);
uint32_t wg_tab_get_active(Widget* tab);

void wg_destroy_widget(Widget* widget);
void wg_set_text(Widget* widget, const char* text);
void wg_set_callback(Widget* widget, void (*callback)(Widget*, void*), void* userdata);

void wg_update_position(Widget* widget);
void wg_set_relative_position(Widget* widget, float rel_x, float rel_y,
                             float rel_width, float rel_height);
void wg_update_all_widgets(Window* window);

uint8_t wg_get_checkbox_state(Widget* checkbox);
uint32_t wg_get_slider_value(Widget* slider);
void wg_set_slider_value(Widget* slider, uint32_t value);
void wg_set_progressbar_value(Widget* progressbar, uint32_t value);

uint32_t get_screen_width(void);
uint32_t get_screen_height(void);

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

void taskbar_enable(void);
void taskbar_disable(void);

typedef enum {
    CURSOR_DEFAULT,
    CURSOR_MOVE,
    CURSOR_RESIZE_NS,
    CURSOR_RESIZE_EW,
    CURSOR_RESIZE_NWSE,
    CURSOR_RESIZE_NESW,
    CURSOR_WAIT,
    CURSOR_TEXT,
    CURSOR_HAND,
    CURSOR_FORBIDDEN
} cursor_type_t;

void vesa_cursor_set_type(cursor_type_t type);

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
    Widget* focus_widget;
    uint32_t focus_chain_index;
} gui_state;

#include "shutdown.h"

#endif