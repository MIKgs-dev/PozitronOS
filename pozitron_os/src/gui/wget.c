#include "gui/gui.h"
#include "drivers/serial.h"
#include "drivers/vesa.h"
#include "kernel/memory.h"
#include "lib/string.h"

// ============ ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ ============
static void add_widget_to_window(Window* window, Widget* widget) {
    if (!window || !widget) return;
    
    widget->parent_window = window;
    widget->next = NULL;
    
    if (window->last_widget) {
        window->last_widget->next = widget;
        window->last_widget = widget;
    } else {
        window->first_widget = widget;
        window->last_widget = widget;
    }
}

static void remove_widget_from_window(Window* window, Widget* widget) {
    if (!window || !widget) return;
    
    Widget* prev = NULL;
    Widget* current = window->first_widget;
    
    while (current) {
        if (current == widget) {
            if (prev) {
                prev->next = widget->next;
            } else {
                window->first_widget = widget->next;
            }
            
            if (widget == window->last_widget) {
                window->last_widget = prev;
            }
            
            widget->parent_window = NULL;
            widget->next = NULL;
            break;
        }
        
        prev = current;
        current = current->next;
    }
}

// ============ ФУНКЦИИ ДЛЯ РАБОТЫ С КООРДИНАТАМИ ============
void wg_update_position(Widget* widget) {
    if (!widget || !widget->parent_window) return;
    
    Window* parent = widget->parent_window;
    
    if (widget->use_relative) {
        widget->x = parent->x + (int32_t)(widget->rel_x * parent->width);
        widget->y = parent->y + (int32_t)(widget->rel_y * parent->height);
        widget->width = (uint32_t)(widget->rel_width * parent->width);
        widget->height = (uint32_t)(widget->rel_height * parent->height);
    }
    
    widget->needs_redraw = 1;
}

void wg_set_relative_position(Widget* widget, float rel_x, float rel_y,
                             float rel_width, float rel_height) {
    if (!widget) return;
    
    widget->rel_x = rel_x;
    widget->rel_y = rel_y;
    widget->rel_width = rel_width;
    widget->rel_height = rel_height;
    widget->use_relative = 1;
    
    wg_update_position(widget);
}

void wg_update_all_widgets(Window* window) {
    if (!window) return;
    
    Widget* widget = window->first_widget;
    while (widget) {
        wg_update_position(widget);
        widget = widget->next;
    }
    
    window->needs_redraw = 1;
}

// ============ БАЗОВАЯ ФУНКЦИЯ СОЗДАНИЯ ВИДЖЕТА ============
static Widget* create_widget_base(Window* parent, WidgetType type) {
    if (!parent || !IS_VALID_WINDOW_PTR(parent)) return NULL;
    
    Widget* widget = (Widget*)kmalloc(sizeof(Widget));
    if (!widget) return NULL;
    
    widget->id = gui_state.next_widget_id++;
    widget->type = type;
    widget->visible = 1;
    widget->enabled = 1;
    widget->parent_window = parent;
    widget->next = NULL;
    widget->state = STATE_NORMAL;
    widget->text = NULL;
    widget->data = NULL;
    widget->data_size = 0;
    widget->on_click = NULL;
    widget->on_hover = NULL;
    widget->on_leave = NULL;
    widget->on_change = NULL;
    widget->on_focus = NULL;
    widget->on_blur = NULL;
    widget->userdata = NULL;
    widget->draw = NULL;
    widget->handle_event = NULL;
    widget->needs_redraw = 1;
    widget->drag_enabled = 0;
    widget->resize_enabled = 0;
    widget->dragging = 0;
    widget->use_relative = 1;
    widget->can_focus = 0;
    widget->focused = 0;
    widget->rel_x = 0.0f;
    widget->rel_y = 0.0f;
    widget->rel_width = 0.0f;
    widget->rel_height = 0.0f;
    
    add_widget_to_window(parent, widget);
    return widget;
}

// ============ ВСПОМОГАТЕЛЬНАЯ ФУНКЦИЯ ОБРЕЗКИ ТЕКСТА ============
static void clip_text_to_widget(char* dest, const char* src, uint32_t max_width, uint32_t* out_width) {
    if (!dest || !src) return;
    
    uint32_t max_chars = max_width / 8;
    if (max_chars < 1) max_chars = 1;
    
    uint32_t len = 0;
    while (src[len] && len < max_chars) {
        dest[len] = src[len];
        len++;
    }
    dest[len] = '\0';
    
    if (out_width) *out_width = len * 8;
}

// ============ БАЗОВЫЕ ВИДЖЕТЫ ============

Widget* wg_create_button(Window* parent, const char* text,
                        float rel_x, float rel_y, 
                        float rel_width, float rel_height,
                        void (*callback)(Widget*, void*), void* userdata) {
    Widget* widget = create_widget_base(parent, WIDGET_BUTTON);
    if (!widget) return NULL;
    
    wg_set_relative_position(widget, rel_x, rel_y, rel_width, rel_height);
    widget->can_focus = 1;
    
    if (text) {
        uint32_t len = gui_strlen(text);
        widget->text = (char*)kmalloc(len + 1);
        if (widget->text) {
            gui_strcpy(widget->text, text);
        }
    }
    
    widget->on_click = callback;
    widget->userdata = userdata;
    
    return widget;
}

Widget* wg_create_label(Window* parent, const char* text,
                       float rel_x, float rel_y) {
    Widget* widget = create_widget_base(parent, WIDGET_LABEL);
    if (!widget) return NULL;
    
    float rel_width = 0.2f;
    if (text) {
        uint32_t len = gui_strlen(text);
        rel_width = (float)(len * 8 + 4) / parent->width;
        if (rel_width > 0.8f) rel_width = 0.8f;
        
        widget->text = (char*)kmalloc(len + 1);
        if (widget->text) {
            gui_strcpy(widget->text, text);
        }
    }
    
    wg_set_relative_position(widget, rel_x, rel_y, rel_width, 0.04f);
    
    return widget;
}

Widget* wg_create_checkbox(Window* parent, const char* text,
                          float rel_x, float rel_y, uint8_t checked) {
    Widget* widget = create_widget_base(parent, WIDGET_CHECKBOX);
    if (!widget) return NULL;
    
    float rel_width = 0.3f;
    if (text) {
        uint32_t len = gui_strlen(text);
        rel_width = (float)(len * 8 + 25) / parent->width;
        if (rel_width > 0.5f) rel_width = 0.5f;
        
        widget->text = (char*)kmalloc(len + 1);
        if (widget->text) {
            gui_strcpy(widget->text, text);
        }
    }
    
    wg_set_relative_position(widget, rel_x, rel_y, rel_width, 0.05f);
    widget->can_focus = 1;
    
    widget->data = kmalloc(sizeof(uint8_t));
    if (widget->data) {
        *((uint8_t*)widget->data) = checked ? 1 : 0;
    }
    widget->data_size = sizeof(uint8_t);
    
    return widget;
}

Widget* wg_create_slider(Window* parent, float rel_x, float rel_y,
                        float rel_width, float rel_height,
                        uint32_t min, uint32_t max, uint32_t value) {
    if (!parent || !IS_VALID_WINDOW_PTR(parent) || rel_width < 0.1f) return NULL;
    if (max <= min) max = min + 1;
    if (value < min) value = min;
    if (value > max) value = max;
    
    Widget* widget = create_widget_base(parent, WIDGET_SLIDER);
    if (!widget) return NULL;
    
    wg_set_relative_position(widget, rel_x, rel_y, rel_width, rel_height);
    widget->drag_enabled = 1;
    widget->can_focus = 1;
    
    widget->data = kmalloc(sizeof(uint32_t) * 3);
    if (widget->data) {
        uint32_t* data = (uint32_t*)widget->data;
        data[0] = min;
        data[1] = max;
        data[2] = value;
    }
    widget->data_size = sizeof(uint32_t) * 3;
    
    return widget;
}

Widget* wg_create_progressbar(Window* parent, float rel_x, float rel_y,
                             float rel_width, float rel_height, uint32_t value) {
    if (!parent || !IS_VALID_WINDOW_PTR(parent) || rel_width < 0.05f || rel_height < 0.02f) 
        return NULL;
    if (value > 100) value = 100;
    
    Widget* widget = create_widget_base(parent, WIDGET_PROGRESSBAR);
    if (!widget) return NULL;
    
    wg_set_relative_position(widget, rel_x, rel_y, rel_width, rel_height);
    
    widget->data = kmalloc(sizeof(uint32_t));
    if (widget->data) {
        *((uint32_t*)widget->data) = value;
    }
    widget->data_size = sizeof(uint32_t);
    
    return widget;
}

// ============ ПОЛЕ ВВОДА ============
Widget* wg_create_input(Window* parent, float rel_x, float rel_y,
                       float rel_width, float rel_height, const char* initial_text) {
    if (!parent || !IS_VALID_WINDOW_PTR(parent) || rel_width < 0.1f || rel_height < 0.03f) return NULL;
    
    Widget* widget = create_widget_base(parent, WIDGET_INPUT);
    if (!widget) return NULL;
    
    wg_set_relative_position(widget, rel_x, rel_y, rel_width, rel_height);
    widget->can_focus = 1;
    
    InputData* input = (InputData*)kmalloc(sizeof(InputData));
    if (!input) {
        kfree(widget);
        return NULL;
    }
    
    input->buffer_size = 256;
    input->buffer = (char*)kmalloc(input->buffer_size);
    if (!input->buffer) {
        kfree(input);
        kfree(widget);
        return NULL;
    }
    
    if (initial_text) {
        uint32_t len = gui_strlen(initial_text);
        if (len >= input->buffer_size) len = input->buffer_size - 1;
        gui_strncpy(input->buffer, initial_text, len + 1);
    } else {
        input->buffer[0] = '\0';
    }
    
    input->cursor_pos = gui_strlen(input->buffer);
    input->selection_start = input->cursor_pos;
    input->selection_end = input->cursor_pos;
    input->cursor_visible = 1;
    input->cursor_blink_time = 0;
    input->password_mode = 0;  // По умолчанию выключен
    input->password_char = '*';
    input->scroll_offset = 0;
    input->multiline = 0;
    input->line_height = 16;
    input->visible_lines = 1;
    
    widget->data = input;
    widget->data_size = sizeof(InputData);
    
    return widget;
}

// Функция для установки парольного режима
void wg_input_set_password_mode(Widget* input, uint8_t enabled) {
    if (!input || input->type != WIDGET_INPUT || !input->data) return;
    
    InputData* data = (InputData*)input->data;
    data->password_mode = enabled ? 1 : 0;
    input->needs_redraw = 1;
}

void wg_input_set_text(Widget* input, const char* text) {
    if (!input || input->type != WIDGET_INPUT || !input->data) return;
    
    InputData* data = (InputData*)input->data;
    if (!data->buffer) return;
    
    uint32_t len = gui_strlen(text);
    if (len >= data->buffer_size) len = data->buffer_size - 1;
    
    gui_strncpy(data->buffer, text, len + 1);
    data->cursor_pos = len;
    data->selection_start = len;
    data->selection_end = len;
    data->scroll_offset = 0;
    
    input->needs_redraw = 1;
    if (input->on_change) {
        input->on_change(input, input->userdata);
    }
}

void wg_input_insert_char(Widget* input, char c) {
    if (!input || input->type != WIDGET_INPUT || !input->data) return;
    
    InputData* data = (InputData*)input->data;
    if (!data->buffer) return;
    
    uint32_t len = gui_strlen(data->buffer);
    if (len >= data->buffer_size - 1) return;
    
    if (data->selection_start != data->selection_end) {
        uint32_t start = data->selection_start < data->selection_end ? data->selection_start : data->selection_end;
        uint32_t end = data->selection_start > data->selection_end ? data->selection_start : data->selection_end;
        
        for (uint32_t i = start; i < len - (end - start); i++) {
            data->buffer[i] = data->buffer[i + (end - start)];
        }
        data->buffer[len - (end - start)] = '\0';
        data->cursor_pos = start;
        data->selection_start = start;
        data->selection_end = start;
        len = gui_strlen(data->buffer);
    }
    
    for (uint32_t i = len; i > data->cursor_pos; i--) {
        data->buffer[i] = data->buffer[i - 1];
    }
    
    data->buffer[data->cursor_pos] = c;
    data->buffer[len + 1] = '\0';
    data->cursor_pos++;
    data->selection_start = data->cursor_pos;
    data->selection_end = data->cursor_pos;
    
    uint32_t visible_chars = (input->width - 8) / 8;
    if (visible_chars < 1) visible_chars = 1;
    
    if (data->cursor_pos > data->scroll_offset + visible_chars - 1) {
        data->scroll_offset = data->cursor_pos - visible_chars + 1;
    }
    
    input->needs_redraw = 1;
    if (input->on_change) {
        input->on_change(input, input->userdata);
    }
}

void wg_input_delete_char(Widget* input) {
    if (!input || input->type != WIDGET_INPUT || !input->data) return;
    
    InputData* data = (InputData*)input->data;
    if (!data->buffer) return;
    
    uint32_t len = gui_strlen(data->buffer);
    
    if (data->selection_start != data->selection_end) {
        uint32_t start = data->selection_start < data->selection_end ? data->selection_start : data->selection_end;
        uint32_t end = data->selection_start > data->selection_end ? data->selection_start : data->selection_end;
        
        for (uint32_t i = start; i < len - (end - start); i++) {
            data->buffer[i] = data->buffer[i + (end - start)];
        }
        data->buffer[len - (end - start)] = '\0';
        data->cursor_pos = start;
        data->selection_start = start;
        data->selection_end = start;
    } else {
        if (data->cursor_pos >= len) return;
        
        for (uint32_t i = data->cursor_pos; i < len; i++) {
            data->buffer[i] = data->buffer[i + 1];
        }
    }
    
    input->needs_redraw = 1;
    if (input->on_change) {
        input->on_change(input, input->userdata);
    }
}

void wg_input_backspace(Widget* input) {
    if (!input || input->type != WIDGET_INPUT || !input->data) return;
    
    InputData* data = (InputData*)input->data;
    if (!data->buffer) return;
    
    uint32_t len = gui_strlen(data->buffer);
    
    if (data->selection_start != data->selection_end) {
        uint32_t start = data->selection_start < data->selection_end ? data->selection_start : data->selection_end;
        uint32_t end = data->selection_start > data->selection_end ? data->selection_start : data->selection_end;
        
        for (uint32_t i = start; i < len - (end - start); i++) {
            data->buffer[i] = data->buffer[i + (end - start)];
        }
        data->buffer[len - (end - start)] = '\0';
        data->cursor_pos = start;
        data->selection_start = start;
        data->selection_end = start;
    } else {
        if (data->cursor_pos == 0) return;
        
        for (uint32_t i = data->cursor_pos - 1; i < len; i++) {
            data->buffer[i] = data->buffer[i + 1];
        }
        
        data->cursor_pos--;
        data->selection_start = data->cursor_pos;
        data->selection_end = data->cursor_pos;
    }
    
    input->needs_redraw = 1;
    if (input->on_change) {
        input->on_change(input, input->userdata);
    }
}

void wg_input_cursor_left(Widget* input) {
    if (!input || input->type != WIDGET_INPUT || !input->data) return;
    
    InputData* data = (InputData*)input->data;
    if (data->cursor_pos > 0) {
        data->cursor_pos--;
        data->selection_start = data->cursor_pos;
        data->selection_end = data->cursor_pos;
        
        if (data->cursor_pos < data->scroll_offset) {
            data->scroll_offset = data->cursor_pos;
        }
        
        input->needs_redraw = 1;
    }
}

void wg_input_cursor_right(Widget* input) {
    if (!input || input->type != WIDGET_INPUT || !input->data) return;
    
    InputData* data = (InputData*)input->data;
    uint32_t len = gui_strlen(data->buffer);
    if (data->cursor_pos < len) {
        data->cursor_pos++;
        data->selection_start = data->cursor_pos;
        data->selection_end = data->cursor_pos;
        
        uint32_t visible_chars = (input->width - 8) / 8;
        if (visible_chars < 1) visible_chars = 1;
        
        if (data->cursor_pos > data->scroll_offset + visible_chars - 1) {
            data->scroll_offset = data->cursor_pos - visible_chars + 1;
        }
        
        input->needs_redraw = 1;
    }
}

void wg_input_cursor_home(Widget* input) {
    if (!input || input->type != WIDGET_INPUT || !input->data) return;
    
    InputData* data = (InputData*)input->data;
    data->cursor_pos = 0;
    data->selection_start = 0;
    data->selection_end = 0;
    data->scroll_offset = 0;
    
    input->needs_redraw = 1;
}

void wg_input_cursor_end(Widget* input) {
    if (!input || input->type != WIDGET_INPUT || !input->data) return;
    
    InputData* data = (InputData*)input->data;
    uint32_t len = gui_strlen(data->buffer);
    data->cursor_pos = len;
    data->selection_start = len;
    data->selection_end = len;
    
    uint32_t visible_chars = (input->width - 8) / 8;
    if (visible_chars < 1) visible_chars = 1;
    
    if (len > visible_chars) {
        data->scroll_offset = len - visible_chars;
    }
    
    input->needs_redraw = 1;
}

void wg_input_select_all(Widget* input) {
    if (!input || input->type != WIDGET_INPUT || !input->data) return;
    
    InputData* data = (InputData*)input->data;
    uint32_t len = gui_strlen(data->buffer);
    data->selection_start = 0;
    data->selection_end = len;
    data->cursor_pos = len;
    
    input->needs_redraw = 1;
}

char* wg_input_get_text(Widget* input) {
    if (!input || input->type != WIDGET_INPUT || !input->data) return NULL;
    
    InputData* data = (InputData*)input->data;
    return data->buffer;
}

// ============ СПИСОК ============
Widget* wg_create_list(Window* parent, float rel_x, float rel_y,
                      float rel_width, float rel_height, uint32_t visible_items) {
    if (!parent || !IS_VALID_WINDOW_PTR(parent) || rel_width < 0.2f || rel_height < 0.2f) return NULL;
    
    Widget* widget = create_widget_base(parent, WIDGET_LIST);
    if (!widget) return NULL;
    
    wg_set_relative_position(widget, rel_x, rel_y, rel_width, rel_height);
    widget->can_focus = 1;
    
    ListData* list = (ListData*)kmalloc(sizeof(ListData));
    if (!list) {
        kfree(widget);
        return NULL;
    }
    
    list->items = (ListItem**)kmalloc(sizeof(ListItem*) * 100);
    if (!list->items) {
        kfree(list);
        kfree(widget);
        return NULL;
    }
    
    for (uint32_t i = 0; i < 100; i++) {
        list->items[i] = NULL;
    }
    
    list->item_count = 0;
    list->visible_items = visible_items;
    list->scroll_offset = 0;
    list->selected_index = 0;
    list->multi_select = 0;
    list->show_scrollbar = 1;
    
    widget->data = list;
    widget->data_size = sizeof(ListData);
    
    return widget;
}

uint32_t wg_list_add_item(Widget* list, const char* text, void* data) {
    if (!list || list->type != WIDGET_LIST || !list->data) return 0;
    
    ListData* list_data = (ListData*)list->data;
    
    ListItem* item = (ListItem*)kmalloc(sizeof(ListItem));
    if (!item) return 0;
    
    item->id = list_data->item_count + 1;
    gui_strncpy(item->text, text, 63);
    item->selected = 0;
    item->enabled = 1;
    item->data = data;
    item->on_activate = NULL;
    
    if (list_data->item_count >= 100) {
        kfree(item);
        return 0;
    }
    
    list_data->items[list_data->item_count] = item;
    list_data->item_count++;
    
    list->needs_redraw = 1;
    return item->id;
}

void wg_list_remove_item(Widget* list, uint32_t index) {
    if (!list || list->type != WIDGET_LIST || !list->data) return;
    
    ListData* list_data = (ListData*)list->data;
    if (index >= list_data->item_count) return;
    
    if (list_data->items[index]) {
        kfree(list_data->items[index]);
        list_data->items[index] = NULL;
    }
    
    for (uint32_t i = index; i < list_data->item_count - 1; i++) {
        list_data->items[i] = list_data->items[i + 1];
    }
    list_data->items[list_data->item_count - 1] = NULL;
    list_data->item_count--;
    
    if (list_data->selected_index >= list_data->item_count) {
        list_data->selected_index = list_data->item_count > 0 ? list_data->item_count - 1 : 0;
    }
    
    list->needs_redraw = 1;
}

void wg_list_clear(Widget* list) {
    if (!list || list->type != WIDGET_LIST || !list->data) return;
    
    ListData* list_data = (ListData*)list->data;
    
    for (uint32_t i = 0; i < list_data->item_count; i++) {
        if (list_data->items[i]) {
            kfree(list_data->items[i]);
            list_data->items[i] = NULL;
        }
    }
    
    list_data->item_count = 0;
    list_data->selected_index = 0;
    list_data->scroll_offset = 0;
    
    list->needs_redraw = 1;
}

uint32_t wg_list_get_selected(Widget* list) {
    if (!list || list->type != WIDGET_LIST || !list->data) return 0;
    
    ListData* list_data = (ListData*)list->data;
    return list_data->selected_index;
}

void wg_list_set_selected(Widget* list, uint32_t index) {
    if (!list || list->type != WIDGET_LIST || !list->data) return;
    
    ListData* list_data = (ListData*)list->data;
    if (index >= list_data->item_count) return;
    
    list_data->selected_index = index;
    wg_list_scroll_to(list, index);
    
    list->needs_redraw = 1;
    
    if (list->on_change) {
        list->on_change(list, list->userdata);
    }
}

void wg_list_scroll_to(Widget* list, uint32_t index) {
    if (!list || list->type != WIDGET_LIST || !list->data) return;
    
    ListData* list_data = (ListData*)list->data;
    if (index >= list_data->item_count) return;
    
    if (index < list_data->scroll_offset) {
        list_data->scroll_offset = index;
    } else if (index >= list_data->scroll_offset + list_data->visible_items) {
        list_data->scroll_offset = index - list_data->visible_items + 1;
    }
    
    list->needs_redraw = 1;
}

void wg_list_scroll(Widget* list, int32_t delta) {
    if (!list || list->type != WIDGET_LIST || !list->data) return;
    
    ListData* list_data = (ListData*)list->data;
    
    int32_t new_offset = (int32_t)list_data->scroll_offset + delta;
    if (new_offset < 0) new_offset = 0;
    if (new_offset > (int32_t)(list_data->item_count - list_data->visible_items)) {
        new_offset = list_data->item_count - list_data->visible_items;
        if (new_offset < 0) new_offset = 0;
    }
    
    list_data->scroll_offset = (uint32_t)new_offset;
    list->needs_redraw = 1;
}

// ============ ВЫПАДАЮЩИЙ СПИСОК ============
Widget* wg_create_dropdown(Window* parent, float rel_x, float rel_y,
                          float rel_width, float rel_height, uint32_t visible_items) {
    if (!parent || !IS_VALID_WINDOW_PTR(parent) || rel_width < 0.2f || rel_height < 0.03f) return NULL;
    
    Widget* widget = create_widget_base(parent, WIDGET_DROPDOWN);
    if (!widget) return NULL;
    
    wg_set_relative_position(widget, rel_x, rel_y, rel_width, rel_height);
    widget->can_focus = 1;
    
    DropdownData* dd = (DropdownData*)kmalloc(sizeof(DropdownData));
    if (!dd) {
        kfree(widget);
        return NULL;
    }
    
    dd->list = (ListData*)kmalloc(sizeof(ListData));
    if (!dd->list) {
        kfree(dd);
        kfree(widget);
        return NULL;
    }
    
    dd->list->items = (ListItem**)kmalloc(sizeof(ListItem*) * 50);
    if (!dd->list->items) {
        kfree(dd->list);
        kfree(dd);
        kfree(widget);
        return NULL;
    }
    
    for (uint32_t i = 0; i < 50; i++) {
        dd->list->items[i] = NULL;
    }
    
    dd->list->item_count = 0;
    dd->list->visible_items = visible_items;
    dd->list->scroll_offset = 0;
    dd->list->selected_index = 0;
    dd->list->multi_select = 0;
    dd->list->show_scrollbar = 1;
    
    dd->expanded = 0;
    dd->dropdown_height = visible_items * 20 + 4;
    
    widget->data = dd;
    widget->data_size = sizeof(DropdownData);
    
    return widget;
}

void wg_dropdown_add_item(Widget* dropdown, const char* text, void* data) {
    if (!dropdown || dropdown->type != WIDGET_DROPDOWN || !dropdown->data) return;
    
    DropdownData* dd = (DropdownData*)dropdown->data;
    
    ListItem* item = (ListItem*)kmalloc(sizeof(ListItem));
    if (!item) return;
    
    item->id = dd->list->item_count + 1;
    gui_strncpy(item->text, text, 63);
    item->selected = 0;
    item->enabled = 1;
    item->data = data;
    item->on_activate = NULL;
    
    if (dd->list->item_count >= 50) {
        kfree(item);
        return;
    }
    
    dd->list->items[dd->list->item_count] = item;
    dd->list->item_count++;
    
    dropdown->needs_redraw = 1;
}

uint32_t wg_dropdown_get_selected(Widget* dropdown) {
    if (!dropdown || dropdown->type != WIDGET_DROPDOWN || !dropdown->data) return 0;
    
    DropdownData* dd = (DropdownData*)dropdown->data;
    return dd->list->selected_index;
}

void wg_dropdown_set_selected(Widget* dropdown, uint32_t index) {
    if (!dropdown || dropdown->type != WIDGET_DROPDOWN || !dropdown->data) return;
    
    DropdownData* dd = (DropdownData*)dropdown->data;
    if (index >= dd->list->item_count) return;
    
    dd->list->selected_index = index;
    dd->expanded = 0;
    
    dropdown->needs_redraw = 1;
    
    if (dropdown->on_change) {
        dropdown->on_change(dropdown, dropdown->userdata);
    }
}

void wg_dropdown_expand(Widget* dropdown) {
    if (!dropdown || dropdown->type != WIDGET_DROPDOWN || !dropdown->data) return;
    
    DropdownData* dd = (DropdownData*)dropdown->data;
    dd->expanded = 1;
    dropdown->needs_redraw = 1;
}

void wg_dropdown_collapse(Widget* dropdown) {
    if (!dropdown || dropdown->type != WIDGET_DROPDOWN || !dropdown->data) return;
    
    DropdownData* dd = (DropdownData*)dropdown->data;
    dd->expanded = 0;
    dropdown->needs_redraw = 1;
}

// ============ СКРОЛЛБАР ============
Widget* wg_create_scrollbar(Window* parent, float rel_x, float rel_y,
                           float rel_width, float rel_height, uint8_t vertical) {
    if (!parent || !IS_VALID_WINDOW_PTR(parent)) return NULL;
    if (vertical && rel_height < 0.2f) return NULL;
    if (!vertical && rel_width < 0.2f) return NULL;
    
    Widget* widget = create_widget_base(parent, WIDGET_SCROLLBAR);
    if (!widget) return NULL;
    
    wg_set_relative_position(widget, rel_x, rel_y, rel_width, rel_height);
    widget->can_focus = 1;
    widget->drag_enabled = 1;
    
    ScrollbarData* sb = (ScrollbarData*)kmalloc(sizeof(ScrollbarData));
    if (!sb) {
        kfree(widget);
        return NULL;
    }
    
    sb->min = 0;
    sb->max = 100;
    sb->value = 0;
    sb->page_size = 10;
    sb->vertical = vertical;
    sb->dragging = 0;
    sb->drag_start_pos = 0;
    sb->drag_start_value = 0;
    
    widget->data = sb;
    widget->data_size = sizeof(ScrollbarData);
    
    return widget;
}

void wg_scrollbar_set_range(Widget* scrollbar, uint32_t min, uint32_t max, uint32_t page) {
    if (!scrollbar || scrollbar->type != WIDGET_SCROLLBAR || !scrollbar->data) return;
    
    ScrollbarData* sb = (ScrollbarData*)scrollbar->data;
    sb->min = min;
    sb->max = max;
    sb->page_size = page;
    if (sb->value < min) sb->value = min;
    if (sb->value > max) sb->value = max;
    
    scrollbar->needs_redraw = 1;
}

void wg_scrollbar_set_value(Widget* scrollbar, uint32_t value) {
    if (!scrollbar || scrollbar->type != WIDGET_SCROLLBAR || !scrollbar->data) return;
    
    ScrollbarData* sb = (ScrollbarData*)scrollbar->data;
    if (value < sb->min) value = sb->min;
    if (value > sb->max) value = sb->max;
    
    sb->value = value;
    scrollbar->needs_redraw = 1;
    
    if (scrollbar->on_change) {
        scrollbar->on_change(scrollbar, scrollbar->userdata);
    }
}

uint32_t wg_scrollbar_get_value(Widget* scrollbar) {
    if (!scrollbar || scrollbar->type != WIDGET_SCROLLBAR || !scrollbar->data) return 0;
    
    ScrollbarData* sb = (ScrollbarData*)scrollbar->data;
    return sb->value;
}

// ============ МЕНЮ-БАР ============
Widget* wg_create_menubar(Window* parent, float rel_x, float rel_y, float rel_width) {
    if (!parent || !IS_VALID_WINDOW_PTR(parent) || rel_width < 0.3f) return NULL;
    
    Widget* widget = create_widget_base(parent, WIDGET_MENUBAR);
    if (!widget) return NULL;
    
    wg_set_relative_position(widget, rel_x, rel_y, rel_width, 0.05f);
    widget->can_focus = 1;
    
    MenubarData* mb = (MenubarData*)kmalloc(sizeof(MenubarData));
    if (!mb) {
        kfree(widget);
        return NULL;
    }
    
    mb->menus = (ListItem**)kmalloc(sizeof(ListItem*) * 20);
    if (!mb->menus) {
        kfree(mb);
        kfree(widget);
        return NULL;
    }
    
    for (uint32_t i = 0; i < 20; i++) {
        mb->menus[i] = NULL;
    }
    
    mb->menu_count = 0;
    mb->active_menu = 0;
    mb->menu_open = 0;
    
    widget->data = mb;
    widget->data_size = sizeof(MenubarData);
    
    return widget;
}

uint32_t wg_menubar_add_menu(Widget* menubar, const char* text) {
    if (!menubar || menubar->type != WIDGET_MENUBAR || !menubar->data) return 0;
    
    MenubarData* mb = (MenubarData*)menubar->data;
    if (mb->menu_count >= 20) return 0;
    
    ListItem* item = (ListItem*)kmalloc(sizeof(ListItem));
    if (!item) return 0;
    
    item->id = mb->menu_count + 1;
    gui_strncpy(item->text, text, 63);
    item->selected = 0;
    item->enabled = 1;
    item->data = NULL;
    item->on_activate = NULL;
    
    mb->menus[mb->menu_count] = item;
    mb->menu_count++;
    
    menubar->needs_redraw = 1;
    return item->id;
}

void wg_menubar_add_item(Widget* menubar, uint32_t menu_index, const char* text, 
                        void (*callback)(void*), void* data) {
    if (!menubar || menubar->type != WIDGET_MENUBAR || !menubar->data) return;
    
    MenubarData* mb = (MenubarData*)menubar->data;
    if (menu_index >= mb->menu_count) return;
    
    ListItem* item = (ListItem*)kmalloc(sizeof(ListItem));
    if (!item) return;
    
    item->id = 1000 + menu_index * 100 + (mb->menus[menu_index]->data ? ((ListItem**)mb->menus[menu_index]->data)[0]->id : 0) + 1;
    gui_strncpy(item->text, text, 63);
    item->selected = 0;
    item->enabled = 1;
    item->data = data;
    item->on_activate = callback;
    
    if (!mb->menus[menu_index]->data) {
        ListItem** items = (ListItem**)kmalloc(sizeof(ListItem*) * 30);
        if (!items) {
            kfree(item);
            return;
        }
        for (uint32_t i = 0; i < 30; i++) items[i] = NULL;
        items[0] = item;
        mb->menus[menu_index]->data = items;
    } else {
        ListItem** items = (ListItem**)mb->menus[menu_index]->data;
        uint32_t i = 0;
        while (items[i] != NULL && i < 29) i++;
        items[i] = item;
    }
    
    menubar->needs_redraw = 1;
}

// ============ ВКЛАДКИ ============
Widget* wg_create_tab(Window* parent, float rel_x, float rel_y,
                     float rel_width, float rel_height) {
    if (!parent || !IS_VALID_WINDOW_PTR(parent) || rel_width < 0.3f || rel_height < 0.3f) return NULL;
    
    Widget* widget = create_widget_base(parent, WIDGET_TAB);
    if (!widget) return NULL;
    
    wg_set_relative_position(widget, rel_x, rel_y, rel_width, rel_height);
    widget->can_focus = 1;
    
    TabData* tab = (TabData*)kmalloc(sizeof(TabData));
    if (!tab) {
        kfree(widget);
        return NULL;
    }
    
    tab->tabs = (ListItem**)kmalloc(sizeof(ListItem*) * 20);
    if (!tab->tabs) {
        kfree(tab);
        kfree(widget);
        return NULL;
    }
    
    for (uint32_t i = 0; i < 20; i++) {
        tab->tabs[i] = NULL;
    }
    
    tab->tab_count = 0;
    tab->active_tab = 0;
    
    widget->data = tab;
    widget->data_size = sizeof(TabData);
    
    return widget;
}

uint32_t wg_tab_add_page(Widget* tab, const char* title) {
    if (!tab || tab->type != WIDGET_TAB || !tab->data) return 0;
    
    TabData* tab_data = (TabData*)tab->data;
    if (tab_data->tab_count >= 20) return 0;
    
    ListItem* item = (ListItem*)kmalloc(sizeof(ListItem));
    if (!item) return 0;
    
    item->id = tab_data->tab_count + 1;
    gui_strncpy(item->text, title, 63);
    item->selected = 0;
    item->enabled = 1;
    item->data = NULL;
    item->on_activate = NULL;
    
    tab_data->tabs[tab_data->tab_count] = item;
    tab_data->tab_count++;
    
    tab->needs_redraw = 1;
    return item->id;
}

void wg_tab_set_active(Widget* tab, uint32_t index) {
    if (!tab || tab->type != WIDGET_TAB || !tab->data) return;
    
    TabData* tab_data = (TabData*)tab->data;
    if (index >= tab_data->tab_count) return;
    
    tab_data->active_tab = index;
    tab->needs_redraw = 1;
    
    if (tab->on_change) {
        tab->on_change(tab, tab->userdata);
    }
}

uint32_t wg_tab_get_active(Widget* tab) {
    if (!tab || tab->type != WIDGET_TAB || !tab->data) return 0;
    
    TabData* tab_data = (TabData*)tab->data;
    return tab_data->active_tab;
}

// ============ УПРАВЛЕНИЕ ВИДЖЕТАМИ ============
void wg_destroy_widget(Widget* widget) {
    if (!widget) return;
    
    if (widget->parent_window && IS_VALID_WINDOW_PTR(widget->parent_window)) {
        remove_widget_from_window(widget->parent_window, widget);
    }
    
    if (widget->text) kfree(widget->text);
    
    if (widget->data) {
        if (widget->type == WIDGET_INPUT) {
            InputData* input = (InputData*)widget->data;
            if (input->buffer) kfree(input->buffer);
        } else if (widget->type == WIDGET_LIST || widget->type == WIDGET_DROPDOWN) {
            ListData* list = (widget->type == WIDGET_LIST) ? (ListData*)widget->data : 
                            ((DropdownData*)widget->data)->list;
            if (list) {
                for (uint32_t i = 0; i < list->item_count; i++) {
                    if (list->items[i]) kfree(list->items[i]);
                }
                kfree(list->items);
                if (widget->type == WIDGET_DROPDOWN) {
                    kfree(list);
                }
            }
        } else if (widget->type == WIDGET_MENUBAR) {
            MenubarData* mb = (MenubarData*)widget->data;
            for (uint32_t i = 0; i < mb->menu_count; i++) {
                if (mb->menus[i]) {
                    if (mb->menus[i]->data) {
                        ListItem** items = (ListItem**)mb->menus[i]->data;
                        for (uint32_t j = 0; items[j] != NULL && j < 30; j++) {
                            kfree(items[j]);
                        }
                        kfree(items);
                    }
                    kfree(mb->menus[i]);
                }
            }
            kfree(mb->menus);
        } else if (widget->type == WIDGET_TAB) {
            TabData* tab = (TabData*)widget->data;
            for (uint32_t i = 0; i < tab->tab_count; i++) {
                if (tab->tabs[i]) kfree(tab->tabs[i]);
            }
            kfree(tab->tabs);
        }
        
        kfree(widget->data);
    }
    
    kfree(widget);
}

void wg_set_text(Widget* widget, const char* text) {
    if (!widget || !text) return;
    
    if (widget->text) kfree(widget->text);
    
    uint32_t len = gui_strlen(text);
    widget->text = (char*)kmalloc(len + 1);
    if (widget->text) {
        gui_strcpy(widget->text, text);
        widget->needs_redraw = 1;
        
        if (widget->parent_window && IS_VALID_WINDOW_PTR(widget->parent_window)) {
            widget->parent_window->needs_redraw = 1;
        }
    }
}

void wg_set_callback(Widget* widget, void (*callback)(Widget*, void*), void* userdata) {
    if (!widget) return;
    widget->on_click = callback;
    widget->userdata = userdata;
}

uint8_t wg_get_checkbox_state(Widget* checkbox) {
    if (!checkbox || checkbox->type != WIDGET_CHECKBOX || !checkbox->data) return 0;
    return *((uint8_t*)checkbox->data);
}

uint32_t wg_get_slider_value(Widget* slider) {
    if (!slider || slider->type != WIDGET_SLIDER || !slider->data) return 0;
    uint32_t* data = (uint32_t*)slider->data;
    return data[2];
}

void wg_set_slider_value(Widget* slider, uint32_t value) {
    if (!slider || slider->type != WIDGET_SLIDER || !slider->data) return;
    uint32_t* data = (uint32_t*)slider->data;
    if (value < data[0]) value = data[0];
    if (value > data[1]) value = data[1];
    data[2] = value;
    slider->needs_redraw = 1;
    if (slider->parent_window) {
        slider->parent_window->needs_redraw = 1;
    }
}

void wg_set_progressbar_value(Widget* progressbar, uint32_t value) {
    if (!progressbar || progressbar->type != WIDGET_PROGRESSBAR || !progressbar->data) return;
    if (value > 100) value = 100;
    *((uint32_t*)progressbar->data) = value;
    progressbar->needs_redraw = 1;
    if (progressbar->parent_window) {
        progressbar->parent_window->needs_redraw = 1;
    }
}