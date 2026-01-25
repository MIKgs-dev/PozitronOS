#include "gui/gui.h"
#include "drivers/serial.h"
#include "drivers/vesa.h"
#include "kernel/memory.h"

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

// ============ СОЗДАНИЕ ВИДЖЕТОВ ============
Widget* wg_create_button(Window* parent, const char* text, 
                        uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    return wg_create_button_ex(parent, text, x, y, width, height, NULL, NULL);
}

Widget* wg_create_button_ex(Window* parent, const char* text, 
                           uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                           void (*callback)(Widget*, void*), void* userdata) {
    if (!parent || !IS_VALID_WINDOW_PTR(parent)) return NULL;
    
    Widget* widget = (Widget*)kmalloc(sizeof(Widget));
    if (!widget) return NULL;
    
    widget->id = gui_state.next_widget_id++;
    widget->type = WIDGET_BUTTON;
    widget->x = parent->x + x;
    widget->y = parent->y + y;
    widget->width = width;
    widget->height = height;
    widget->visible = 1;
    widget->enabled = 1;
    widget->parent_window = parent;
    widget->next = NULL;
    widget->state = STATE_NORMAL;
    
    if (text) {
        uint32_t len = 0;
        while (text[len] && len < 255) len++;
        widget->text = (char*)kmalloc(len + 1);
        if (widget->text) {
            for (uint32_t i = 0; i < len; i++) widget->text[i] = text[i];
            widget->text[len] = '\0';
        } else {
            widget->text = NULL;
        }
    } else {
        widget->text = NULL;
    }
    
    widget->data = NULL;
    widget->data_size = 0;
    widget->on_click = callback;
    widget->on_hover = NULL;
    widget->on_leave = NULL;
    widget->userdata = userdata;
    widget->draw = NULL;
    widget->handle_event = NULL;
    widget->needs_redraw = 1;
    widget->drag_enabled = 0;
    widget->resize_enabled = 0;
    widget->dragging = 0;
    
    add_widget_to_window(parent, widget);
    return widget;
}

Widget* wg_create_label(Window* parent, const char* text, 
                       uint32_t x, uint32_t y) {
    if (!parent || !IS_VALID_WINDOW_PTR(parent)) return NULL;
    
    Widget* widget = (Widget*)kmalloc(sizeof(Widget));
    if (!widget) return NULL;
    
    widget->id = gui_state.next_widget_id++;
    widget->type = WIDGET_LABEL;
    widget->x = parent->x + x;
    widget->y = parent->y + y;
    widget->width = 100;
    widget->height = 16;
    widget->visible = 1;
    widget->enabled = 1;
    widget->parent_window = parent;
    widget->next = NULL;
    widget->state = STATE_NORMAL;
    
    if (text) {
        uint32_t len = 0;
        while (text[len] && len < 255) len++;
        widget->text = (char*)kmalloc(len + 1);
        if (widget->text) {
            for (uint32_t i = 0; i < len; i++) widget->text[i] = text[i];
            widget->text[len] = '\0';
            widget->width = len * 8 + 4;
        } else {
            widget->text = NULL;
        }
    } else {
        widget->text = NULL;
    }
    
    widget->data = NULL;
    widget->data_size = 0;
    widget->on_click = NULL;
    widget->on_hover = NULL;
    widget->on_leave = NULL;
    widget->userdata = NULL;
    widget->draw = NULL;
    widget->handle_event = NULL;
    widget->needs_redraw = 1;
    widget->drag_enabled = 0;
    widget->resize_enabled = 0;
    widget->dragging = 0;
    
    add_widget_to_window(parent, widget);
    return widget;
}

Widget* wg_create_checkbox(Window* parent, const char* text, 
                          uint32_t x, uint32_t y, uint8_t checked) {
    if (!parent || !IS_VALID_WINDOW_PTR(parent)) return NULL;
    
    Widget* widget = (Widget*)kmalloc(sizeof(Widget));
    if (!widget) return NULL;
    
    widget->id = gui_state.next_widget_id++;
    widget->type = WIDGET_CHECKBOX;
    widget->x = parent->x + x;
    widget->y = parent->y + y;
    widget->width = 120;
    widget->height = 20;
    widget->visible = 1;
    widget->enabled = 1;
    widget->parent_window = parent;
    widget->next = NULL;
    widget->state = STATE_NORMAL;
    
    if (text) {
        uint32_t len = 0;
        while (text[len] && len < 255) len++;
        widget->text = (char*)kmalloc(len + 1);
        if (widget->text) {
            for (uint32_t i = 0; i < len; i++) widget->text[i] = text[i];
            widget->text[len] = '\0';
            widget->width = len * 8 + 25;
        }
    }
    
    widget->data = kmalloc(sizeof(uint8_t));
    if (widget->data) {
        *((uint8_t*)widget->data) = checked ? 1 : 0;
    }
    widget->data_size = sizeof(uint8_t);
    
    widget->on_click = NULL;
    widget->on_hover = NULL;
    widget->on_leave = NULL;
    widget->userdata = NULL;
    widget->draw = NULL;
    widget->handle_event = NULL;
    widget->needs_redraw = 1;
    widget->drag_enabled = 0;
    widget->resize_enabled = 0;
    widget->dragging = 0;
    
    add_widget_to_window(parent, widget);
    return widget;
}

Widget* wg_create_slider(Window* parent, uint32_t x, uint32_t y, 
                        uint32_t width, uint32_t min, uint32_t max, uint32_t value) {
    if (!parent || !IS_VALID_WINDOW_PTR(parent) || width < 40) return NULL;
    if (max <= min) max = min + 1;
    if (value < min) value = min;
    if (value > max) value = max;
    
    Widget* widget = (Widget*)kmalloc(sizeof(Widget));
    if (!widget) return NULL;
    
    widget->id = gui_state.next_widget_id++;
    widget->type = WIDGET_SLIDER;
    widget->x = parent->x + x;
    widget->y = parent->y + y;
    widget->width = width;
    widget->height = 20;
    widget->visible = 1;
    widget->enabled = 1;
    widget->parent_window = parent;
    widget->next = NULL;
    widget->state = STATE_NORMAL;
    widget->text = NULL;
    
    widget->data = kmalloc(sizeof(uint32_t) * 3);
    if (widget->data) {
        uint32_t* data = (uint32_t*)widget->data;
        data[0] = min;
        data[1] = max;
        data[2] = value;
    }
    widget->data_size = sizeof(uint32_t) * 3;
    
    widget->on_click = NULL;
    widget->on_hover = NULL;
    widget->on_leave = NULL;
    widget->userdata = NULL;
    widget->draw = NULL;
    widget->handle_event = NULL;
    widget->needs_redraw = 1;
    widget->drag_enabled = 1;
    widget->resize_enabled = 0;
    widget->dragging = 0;
    
    add_widget_to_window(parent, widget);
    return widget;
}

Widget* wg_create_progressbar(Window* parent, uint32_t x, uint32_t y, 
                             uint32_t width, uint32_t height, uint32_t value) {
    if (!parent || !IS_VALID_WINDOW_PTR(parent) || width < 20 || height < 8) return NULL;
    if (value > 100) value = 100;
    
    Widget* widget = (Widget*)kmalloc(sizeof(Widget));
    if (!widget) return NULL;
    
    widget->id = gui_state.next_widget_id++;
    widget->type = WIDGET_PROGRESSBAR;
    widget->x = parent->x + x;
    widget->y = parent->y + y;
    widget->width = width;
    widget->height = height;
    widget->visible = 1;
    widget->enabled = 1;
    widget->parent_window = parent;
    widget->next = NULL;
    widget->state = STATE_NORMAL;
    widget->text = NULL;
    
    widget->data = kmalloc(sizeof(uint32_t));
    if (widget->data) {
        *((uint32_t*)widget->data) = value;
    }
    widget->data_size = sizeof(uint32_t);
    
    widget->on_click = NULL;
    widget->on_hover = NULL;
    widget->on_leave = NULL;
    widget->userdata = NULL;
    widget->draw = NULL;
    widget->handle_event = NULL;
    widget->needs_redraw = 1;
    widget->drag_enabled = 0;
    widget->resize_enabled = 0;
    widget->dragging = 0;
    
    add_widget_to_window(parent, widget);
    return widget;
}

// ============ УПРАВЛЕНИЕ ВИДЖЕТАМИ ============
void wg_destroy_widget(Widget* widget) {
    if (!widget) return;
    
    if (widget->parent_window && IS_VALID_WINDOW_PTR(widget->parent_window)) {
        remove_widget_from_window(widget->parent_window, widget);
    }
    
    if (widget->text) kfree(widget->text);
    if (widget->data) kfree(widget->data);
    kfree(widget);
}

void wg_set_text(Widget* widget, const char* text) {
    if (!widget || !text) return;
    
    if (widget->text) kfree(widget->text);
    
    uint32_t len = 0;
    while (text[len] && len < 255) len++;
    widget->text = (char*)kmalloc(len + 1);
    if (widget->text) {
        for (uint32_t i = 0; i < len; i++) widget->text[i] = text[i];
        widget->text[len] = '\0';
        widget->needs_redraw = 1;
        
        if (widget->parent_window && IS_VALID_WINDOW_PTR(widget->parent_window)) {
            widget->parent_window->needs_redraw = 1;
        }
    }
}

void wg_set_callback_ex(Widget* widget, void (*callback)(Widget*, void*), void* userdata) {
    if (!widget) return;
    widget->on_click = callback;
    widget->userdata = userdata;
}

// ============ ФУНКЦИИ ДЛЯ РАБОТЫ С НОВЫМИ ВИДЖЕТАМИ ============
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