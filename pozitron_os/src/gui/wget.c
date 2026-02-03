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

// ============ ФУНКЦИИ ДЛЯ РАБОТЫ С КООРДИНАТАМИ ============
void wg_update_position(Widget* widget) {
    if (!widget || !widget->parent_window) return;
    
    Window* parent = widget->parent_window;
    
    if (widget->use_relative) {
        // Вычисляем абсолютные координаты из относительных
        widget->x = parent->x + (uint32_t)(widget->rel_x * parent->width);
        widget->y = parent->y + (uint32_t)(widget->rel_y * parent->height);
        widget->width = (uint32_t)(widget->rel_width * parent->width);
        widget->height = (uint32_t)(widget->rel_height * parent->height);
    }
    // Если не используем относительные координаты, оставляем как есть
    
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

void wg_set_absolute_position(Widget* widget, uint32_t x, uint32_t y,
                             uint32_t width, uint32_t height) {
    if (!widget || !widget->parent_window) return;
    
    Window* parent = widget->parent_window;
    
    widget->x = x;
    widget->y = y;
    widget->width = width;
    widget->height = height;
    widget->use_relative = 0;
    
    // Вычисляем относительные координаты для будущих изменений размера
    if (parent->width > 0 && parent->height > 0) {
        widget->rel_x = (float)(x - parent->x) / parent->width;
        widget->rel_y = (float)(y - parent->y) / parent->height;
        widget->rel_width = (float)width / parent->width;
        widget->rel_height = (float)height / parent->height;
    }
    
    widget->needs_redraw = 1;
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
    widget->userdata = NULL;
    widget->draw = NULL;
    widget->handle_event = NULL;
    widget->needs_redraw = 1;
    widget->drag_enabled = 0;
    widget->resize_enabled = 0;
    widget->dragging = 0;
    widget->use_relative = 0; // По умолчанию - абсолютные координаты
    widget->rel_x = 0.0f;
    widget->rel_y = 0.0f;
    widget->rel_width = 0.0f;
    widget->rel_height = 0.0f;
    
    add_widget_to_window(parent, widget);
    return widget;
}

// ============ СТАРЫЕ ФУНКЦИИ (АБСОЛЮТНЫЕ КООРДИНАТЫ) ============
Widget* wg_create_button(Window* parent, const char* text, 
                        uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    return wg_create_button_ex(parent, text, x, y, width, height, NULL, NULL);
}

Widget* wg_create_button_ex(Window* parent, const char* text, 
                           uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                           void (*callback)(Widget*, void*), void* userdata) {
    Widget* widget = create_widget_base(parent, WIDGET_BUTTON);
    if (!widget) return NULL;
    
    wg_set_absolute_position(widget, parent->x + x, parent->y + y, width, height);
    
    if (text) {
        uint32_t len = 0;
        while (text[len] && len < 255) len++;
        widget->text = (char*)kmalloc(len + 1);
        if (widget->text) {
            for (uint32_t i = 0; i < len; i++) widget->text[i] = text[i];
            widget->text[len] = '\0';
        }
    }
    
    widget->on_click = callback;
    widget->userdata = userdata;
    
    return widget;
}

Widget* wg_create_label(Window* parent, const char* text, 
                       uint32_t x, uint32_t y) {
    Widget* widget = create_widget_base(parent, WIDGET_LABEL);
    if (!widget) return NULL;
    
    uint32_t width = 100;
    if (text) {
        uint32_t len = 0;
        while (text[len] && len < 255) len++;
        width = len * 8 + 4;
        widget->text = (char*)kmalloc(len + 1);
        if (widget->text) {
            for (uint32_t i = 0; i < len; i++) widget->text[i] = text[i];
            widget->text[len] = '\0';
        }
    }
    
    wg_set_absolute_position(widget, parent->x + x, parent->y + y, width, 16);
    
    return widget;
}

Widget* wg_create_checkbox(Window* parent, const char* text, 
                          uint32_t x, uint32_t y, uint8_t checked) {
    Widget* widget = create_widget_base(parent, WIDGET_CHECKBOX);
    if (!widget) return NULL;
    
    uint32_t width = 120;
    if (text) {
        uint32_t len = 0;
        while (text[len] && len < 255) len++;
        width = len * 8 + 25;
        widget->text = (char*)kmalloc(len + 1);
        if (widget->text) {
            for (uint32_t i = 0; i < len; i++) widget->text[i] = text[i];
            widget->text[len] = '\0';
        }
    }
    
    wg_set_absolute_position(widget, parent->x + x, parent->y + y, width, 20);
    
    widget->data = kmalloc(sizeof(uint8_t));
    if (widget->data) {
        *((uint8_t*)widget->data) = checked ? 1 : 0;
    }
    widget->data_size = sizeof(uint8_t);
    
    return widget;
}

Widget* wg_create_slider(Window* parent, uint32_t x, uint32_t y, 
                        uint32_t width, uint32_t min, uint32_t max, uint32_t value) {
    if (!parent || !IS_VALID_WINDOW_PTR(parent) || width < 40) return NULL;
    if (max <= min) max = min + 1;
    if (value < min) value = min;
    if (value > max) value = max;
    
    Widget* widget = create_widget_base(parent, WIDGET_SLIDER);
    if (!widget) return NULL;
    
    wg_set_absolute_position(widget, parent->x + x, parent->y + y, width, 20);
    widget->drag_enabled = 1;
    
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

Widget* wg_create_progressbar(Window* parent, uint32_t x, uint32_t y, 
                             uint32_t width, uint32_t height, uint32_t value) {
    if (!parent || !IS_VALID_WINDOW_PTR(parent) || width < 20 || height < 8) return NULL;
    if (value > 100) value = 100;
    
    Widget* widget = create_widget_base(parent, WIDGET_PROGRESSBAR);
    if (!widget) return NULL;
    
    wg_set_absolute_position(widget, parent->x + x, parent->y + y, width, height);
    
    widget->data = kmalloc(sizeof(uint32_t));
    if (widget->data) {
        *((uint32_t*)widget->data) = value;
    }
    widget->data_size = sizeof(uint32_t);
    
    return widget;
}

// ============ НОВЫЕ ФУНКЦИИ (ОТНОСИТЕЛЬНЫЕ КООРДИНАТЫ) ============
Widget* wg_create_button_rel(Window* parent, const char* text,
                            float rel_x, float rel_y, 
                            float rel_width, float rel_height,
                            void (*callback)(Widget*, void*), void* userdata) {
    Widget* widget = create_widget_base(parent, WIDGET_BUTTON);
    if (!widget) return NULL;
    
    wg_set_relative_position(widget, rel_x, rel_y, rel_width, rel_height);
    
    if (text) {
        uint32_t len = 0;
        while (text[len] && len < 255) len++;
        widget->text = (char*)kmalloc(len + 1);
        if (widget->text) {
            for (uint32_t i = 0; i < len; i++) widget->text[i] = text[i];
            widget->text[len] = '\0';
        }
    }
    
    widget->on_click = callback;
    widget->userdata = userdata;
    
    return widget;
}

Widget* wg_create_label_rel(Window* parent, const char* text,
                           float rel_x, float rel_y) {
    Widget* widget = create_widget_base(parent, WIDGET_LABEL);
    if (!widget) return NULL;
    
    // Для label ширина вычисляется автоматически
    float rel_width = 0.2f; // 20% ширины окна по умолчанию
    if (text) {
        uint32_t len = 0;
        while (text[len] && len < 255) len++;
        rel_width = (float)(len * 8 + 4) / parent->width;
        if (rel_width > 0.8f) rel_width = 0.8f; // Ограничиваем 80%
        
        widget->text = (char*)kmalloc(len + 1);
        if (widget->text) {
            for (uint32_t i = 0; i < len; i++) widget->text[i] = text[i];
            widget->text[len] = '\0';
        }
    }
    
    wg_set_relative_position(widget, rel_x, rel_y, rel_width, 0.04f); // ~16px при 400px окне
    
    return widget;
}

Widget* wg_create_checkbox_rel(Window* parent, const char* text,
                              float rel_x, float rel_y, uint8_t checked) {
    Widget* widget = create_widget_base(parent, WIDGET_CHECKBOX);
    if (!widget) return NULL;
    
    float rel_width = 0.3f; // 30% ширины окна по умолчанию
    if (text) {
        uint32_t len = 0;
        while (text[len] && len < 255) len++;
        rel_width = (float)(len * 8 + 25) / parent->width;
        if (rel_width > 0.5f) rel_width = 0.5f; // Ограничиваем 50%
        
        widget->text = (char*)kmalloc(len + 1);
        if (widget->text) {
            for (uint32_t i = 0; i < len; i++) widget->text[i] = text[i];
            widget->text[len] = '\0';
        }
    }
    
    wg_set_relative_position(widget, rel_x, rel_y, rel_width, 0.05f); // ~20px при 400px окне
    
    widget->data = kmalloc(sizeof(uint8_t));
    if (widget->data) {
        *((uint8_t*)widget->data) = checked ? 1 : 0;
    }
    widget->data_size = sizeof(uint8_t);
    
    return widget;
}

Widget* wg_create_slider_rel(Window* parent, float rel_x, float rel_y,
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

Widget* wg_create_progressbar_rel(Window* parent, float rel_x, float rel_y,
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