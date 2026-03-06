#include "gui/gui.h"
#include "drivers/serial.h"
#include "drivers/vesa.h"
#include "kernel/memory.h"
#include "gui/shutdown.h"
#include "drivers/timer.h"
#include "lib/string.h"

struct GUI_State gui_state;

// ============ РЕЕСТР ОКОН ============
void gui_register_window(Window* window) {
    if (!window || window->id == 0) return;
    uint32_t index = window->id % WINDOW_REGISTRY_SIZE;
    gui_state.window_registry[index] = window;
}

void gui_unregister_window(uint32_t window_id) {
    if (window_id == 0) return;
    uint32_t index = window_id % WINDOW_REGISTRY_SIZE;
    gui_state.window_registry[index] = NULL;
}

Window* gui_get_window_by_id(uint32_t id) {
    if (id == 0) return NULL;
    uint32_t index = id % WINDOW_REGISTRY_SIZE;
    Window* win = gui_state.window_registry[index];
    return (win && win->id == id) ? win : NULL;
}

extern uint32_t stack_guard;

// ============ УПРАВЛЕНИЕ ФОКУСОМ ============
void gui_set_focus(Widget* widget) {
    if (!gui_state.initialized) return;
    
    if (gui_state.focus_widget == widget) return;
    
    if (gui_state.focus_widget && IS_VALID_WINDOW_PTR(gui_state.focus_widget->parent_window)) {
        gui_state.focus_widget->focused = 0;
        gui_state.focus_widget->state = STATE_NORMAL;
        gui_state.focus_widget->needs_redraw = 1;
        
        if (gui_state.focus_widget->on_blur) {
            gui_state.focus_widget->on_blur(gui_state.focus_widget);
        }
    }
    
    gui_state.focus_widget = widget;
    
    if (widget) {
        widget->focused = 1;
        widget->state = STATE_FOCUSED;
        widget->needs_redraw = 1;
        
        if (widget->parent_window && IS_VALID_WINDOW_PTR(widget->parent_window)) {
            widget->parent_window->focused_widget = widget;
        }
        
        if (widget->on_focus) {
            widget->on_focus(widget);
        }
        
        event_t focus_event;
        focus_event.type = EVENT_FOCUS_CHANGE;
        focus_event.data1 = (uint32_t)widget;
        focus_event.data2 = 0;
        event_post(focus_event);
    }
}

void gui_clear_focus(void) {
    gui_set_focus(NULL);
}

Widget* gui_get_focused_widget(void) {
    return gui_state.focus_widget;
}

static void build_focus_chain(Window* window, Widget** chain, uint32_t* count) {
    if (!window || !chain || !count) return;
    
    Widget* widget = window->first_widget;
    while (widget && *count < 64) {
        if (widget->visible && widget->enabled && widget->can_focus) {
            chain[*count] = widget;
            (*count)++;
        }
        widget = widget->next;
    }
}

void gui_focus_next(void) {
    if (!gui_state.initialized) return;
    
    Widget* focus_chain[64];
    uint32_t focus_count = 0;
    
    Window* window = gui_state.first_window;
    while (window) {
        if (IS_VALID_WINDOW_PTR(window) && window->visible && !window->minimized) {
            build_focus_chain(window, focus_chain, &focus_count);
        }
        window = window->next;
    }
    
    if (focus_count == 0) return;
    
    uint32_t current_index = 0;
    if (gui_state.focus_widget) {
        for (uint32_t i = 0; i < focus_count; i++) {
            if (focus_chain[i] == gui_state.focus_widget) {
                current_index = i;
                break;
            }
        }
    }
    
    uint32_t next_index = (current_index + 1) % focus_count;
    gui_set_focus(focus_chain[next_index]);
}

void gui_focus_prev(void) {
    if (!gui_state.initialized) return;
    
    Widget* focus_chain[64];
    uint32_t focus_count = 0;
    
    Window* window = gui_state.first_window;
    while (window) {
        if (IS_VALID_WINDOW_PTR(window) && window->visible && !window->minimized) {
            build_focus_chain(window, focus_chain, &focus_count);
        }
        window = window->next;
    }
    
    if (focus_count == 0) return;
    
    uint32_t current_index = 0;
    if (gui_state.focus_widget) {
        for (uint32_t i = 0; i < focus_count; i++) {
            if (focus_chain[i] == gui_state.focus_widget) {
                current_index = i;
                break;
            }
        }
    }
    
    uint32_t prev_index = (current_index == 0) ? focus_count - 1 : current_index - 1;
    gui_set_focus(focus_chain[prev_index]);
}

// ============ ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ ============
static uint8_t point_in_rect_static(int32_t px, int32_t py, int32_t x, int32_t y, 
                                   uint32_t w, uint32_t h) {
    return (px >= x && px < x + (int32_t)w && py >= y && py < y + (int32_t)h);
}

// ============ ИНИЦИАЛИЗАЦИЯ ============
void gui_init(uint32_t screen_width, uint32_t screen_height) {
    if (gui_state.initialized) return;
    
    gui_state.screen_width = screen_width;
    gui_state.screen_height = screen_height;
    gui_state.first_window = NULL;
    gui_state.last_window = NULL;
    gui_state.focused_window = NULL;
    gui_state.dragging_window = NULL;
    gui_state.window_count = 0;
    gui_state.next_window_id = 1;
    gui_state.next_widget_id = 1;
    gui_state.initialized = 1;
    gui_state.debug_mode = 0;
    gui_state.focus_widget = NULL;
    gui_state.focus_chain_index = 0;
    
    for (uint32_t i = 0; i < WINDOW_REGISTRY_SIZE; i++) {
        gui_state.window_registry[i] = NULL;
    }
    
    serial_puts("[GUI] Initialized\n");
}

void gui_shutdown(void) {
    if (!gui_state.initialized) return;
    
    Window* window = gui_state.first_window;
    while (window) {
        Window* next = window->next;
        if (IS_VALID_WINDOW_PTR(window)) {
            wm_destroy_window(window);
        }
        window = next;
    }
    
    gui_state.initialized = 0;
}

// ============ ОБРАБОТКА СПЕЦИАЛЬНЫХ КЛАВИШ ДЛЯ INPUT ============
static void handle_input_special_key(Widget* input, uint8_t key, uint8_t modifiers) {
    if (!input || input->type != WIDGET_INPUT) return;
    
    InputData* data = (InputData*)input->data;
    if (!data) return;
    
    switch (key) {
        case 0x0E: // Backspace
            wg_input_backspace(input);
            break;
            
        case 0x53: // Delete
            wg_input_delete_char(input);
            break;
            
        case 0x4B: // Left arrow
            if (modifiers & 0x02) { // Ctrl
                if (data->buffer) {
                    uint32_t pos = data->cursor_pos;
                    while (pos > 0 && data->buffer[pos-1] == ' ') pos--;
                    while (pos > 0 && data->buffer[pos-1] != ' ') pos--;
                    data->cursor_pos = pos;
                    if (!(modifiers & 0x01)) {
                        data->selection_start = pos;
                        data->selection_end = pos;
                    } else {
                        data->selection_end = pos;
                    }
                }
            } else {
                if (!(modifiers & 0x01)) {
                    wg_input_cursor_left(input);
                } else {
                    if (data->cursor_pos > 0) {
                        data->cursor_pos--;
                        data->selection_end = data->cursor_pos;
                    }
                }
            }
            input->needs_redraw = 1;
            break;
            
        case 0x4D: // Right arrow
            if (modifiers & 0x02) { // Ctrl
                if (data->buffer) {
                    uint32_t len = gui_strlen(data->buffer);
                    uint32_t pos = data->cursor_pos;
                    while (pos < len && data->buffer[pos] == ' ') pos++;
                    while (pos < len && data->buffer[pos] != ' ') pos++;
                    data->cursor_pos = pos;
                    if (!(modifiers & 0x01)) {
                        data->selection_start = pos;
                        data->selection_end = pos;
                    } else {
                        data->selection_end = pos;
                    }
                }
            } else {
                if (!(modifiers & 0x01)) {
                    wg_input_cursor_right(input);
                } else {
                    uint32_t len = gui_strlen(data->buffer);
                    if (data->cursor_pos < len) {
                        data->cursor_pos++;
                        data->selection_end = data->cursor_pos;
                    }
                }
            }
            input->needs_redraw = 1;
            break;
            
        case 0x47: // Home
            data->cursor_pos = 0;
            if (!(modifiers & 0x01)) {
                data->selection_start = 0;
                data->selection_end = 0;
            } else {
                data->selection_end = 0;
            }
            data->scroll_offset = 0;
            input->needs_redraw = 1;
            break;
            
        case 0x4F: // End
            data->cursor_pos = gui_strlen(data->buffer);
            if (!(modifiers & 0x01)) {
                data->selection_start = data->cursor_pos;
                data->selection_end = data->cursor_pos;
            } else {
                data->selection_end = data->cursor_pos;
            }
            input->needs_redraw = 1;
            break;
    }
}

// ============ ОБРАБОТКА ВЫДЕЛЕНИЯ МЫШЬЮ (ПРОСТАЯ ВЕРСИЯ) ============
static void handle_input_mouse_click(Widget* input, int32_t mx, int32_t my, uint8_t shift) {
    if (!input || input->type != WIDGET_INPUT || !input->data) return;
    
    InputData* data = (InputData*)input->data;
    
    int32_t text_x = input->x + 4;
    int32_t click_offset = mx - text_x;
    
    if (click_offset < 0) click_offset = 0;
    
    uint32_t char_pos = click_offset / 8 + data->scroll_offset;
    uint32_t len = gui_strlen(data->buffer);
    
    if (char_pos > len) char_pos = len;
    
    data->cursor_pos = char_pos;
    
    if (!shift) {
        // Без Shift - просто ставим курсор, сбрасываем выделение
        data->selection_start = char_pos;
        data->selection_end = char_pos;
    } else {
        // С Shift - расширяем выделение от текущей позиции
        data->selection_end = char_pos;
    }
    
    // Корректируем скролл
    uint32_t visible_chars = (input->width - 8) / 8;
    if (visible_chars < 1) visible_chars = 1;
    
    if (char_pos > data->scroll_offset + visible_chars - 1) {
        data->scroll_offset = char_pos - visible_chars + 1;
    }
    if (char_pos < data->scroll_offset) {
        data->scroll_offset = char_pos;
    }
    
    input->needs_redraw = 1;
}

static void handle_input_mouse_drag(Widget* input, int32_t mx, int32_t my) {
    if (!input || input->type != WIDGET_INPUT || !input->data) return;
    
    InputData* data = (InputData*)input->data;
    
    int32_t text_x = input->x + 4;
    int32_t drag_offset = mx - text_x;
    
    if (drag_offset < 0) drag_offset = 0;
    
    uint32_t char_pos = drag_offset / 8 + data->scroll_offset;
    uint32_t len = gui_strlen(data->buffer);
    
    if (char_pos > len) char_pos = len;
    
    data->cursor_pos = char_pos;
    data->selection_end = char_pos;
    
    // Корректируем скролл
    uint32_t visible_chars = (input->width - 8) / 8;
    if (visible_chars < 1) visible_chars = 1;
    
    if (char_pos > data->scroll_offset + visible_chars - 1) {
        data->scroll_offset = char_pos - visible_chars + 1;
    }
    if (char_pos < data->scroll_offset) {
        data->scroll_offset = char_pos;
    }
    
    input->needs_redraw = 1;
}

// ============ ОБРАБОТКА СОБЫТИЙ ============
void gui_handle_event(event_t* event) {
    if (!gui_state.initialized || !event) return;

    int32_t mx = (int32_t)event->data1;
    int32_t my = (int32_t)(event->data2 & 0xFFFF);
    uint32_t button = (event->data2 >> 16) & 0xFF;
    uint8_t shift = (event->data2 & 0x01) ? 1 : 0;
    
    if (is_shutdown_mode_active()) {
        Window* shutdown_dialog = get_shutdown_dialog();
        
        if (shutdown_dialog && IS_VALID_WINDOW_PTR(shutdown_dialog)) {
            uint8_t is_inside_dialog = point_in_rect_static(mx, my, 
                                                           shutdown_dialog->x, shutdown_dialog->y,
                                                           shutdown_dialog->width, shutdown_dialog->height);
            
            if (is_inside_dialog) {
                int32_t win_x = mx - shutdown_dialog->x;
                int32_t win_y = my - shutdown_dialog->y;
                
                Widget* widget = shutdown_dialog->first_widget;
                while (widget) {
                    int32_t widget_rel_x = widget->x - shutdown_dialog->x;
                    int32_t widget_rel_y = widget->y - shutdown_dialog->y;
                    
                    uint8_t is_over_widget = (widget->visible && widget->enabled &&
                                             point_in_rect_static(win_x, win_y, 
                                                                widget_rel_x, widget_rel_y,
                                                                widget->width, widget->height));
                    
                    if (event->type == EVENT_MOUSE_MOVE) {
                        if (is_over_widget) {
                            if (widget->state != STATE_HOVER && widget->state != STATE_PRESSED) {
                                widget->state = STATE_HOVER;
                                widget->needs_redraw = 1;
                                shutdown_dialog->needs_redraw = 1;
                            }
                        } else {
                            if (widget->state == STATE_HOVER) {
                                widget->state = STATE_NORMAL;
                                widget->needs_redraw = 1;
                                shutdown_dialog->needs_redraw = 1;
                            }
                        }
                    }
                    else if (event->type == EVENT_MOUSE_CLICK && button == 0) {
                        if (is_over_widget) {
                            widget->state = STATE_PRESSED;
                            widget->needs_redraw = 1;
                            shutdown_dialog->needs_redraw = 1;
                            
                            if (widget->on_click) {
                                widget->on_click(widget, widget->userdata);
                            }
                            return;
                        }
                    }
                    else if (event->type == EVENT_MOUSE_RELEASE && button == 0) {
                        if (widget->state == STATE_PRESSED) {
                            widget->state = STATE_NORMAL;
                            widget->needs_redraw = 1;
                            shutdown_dialog->needs_redraw = 1;
                        }
                    }
                    
                    widget = widget->next;
                }
                return;
            }
        }
        return;
    }
    
    if (event->type == EVENT_KEY_PRESS) {
        uint8_t key = event->data1;
        uint8_t modifiers = event->data2;
        
        switch (key) {
            case 0x3B: // F1
                break;
                
            case 0x3C: // F2
                wm_dump_info();
                break;
                
            case 0x01: // ESC
                if (gui_state.focused_window && 
                    IS_VALID_WINDOW_PTR(gui_state.focused_window)) {
                    wm_close_window(gui_state.focused_window);
                }
                break;
                
            case 0x57: // F11
                if (gui_state.focused_window && 
                    IS_VALID_WINDOW_PTR(gui_state.focused_window) &&
                    gui_state.focused_window->maximizable) {
                    if (gui_state.focused_window->maximized) {
                        wm_restore_window(gui_state.focused_window);
                    } else {
                        wm_maximize_window(gui_state.focused_window);
                    }
                }
                break;
                
            case 0x0F: // Tab
                if (modifiers & 0x02) {
                    gui_focus_prev();
                } else {
                    gui_focus_next();
                }
                break;
                
            default:
                if (gui_state.focus_widget && gui_state.focus_widget->type == WIDGET_INPUT) {
                    handle_input_special_key(gui_state.focus_widget, key, modifiers);
                }
                break;
        }
        return;
    }
    
    if (event->type == EVENT_TEXT_INPUT && gui_state.focus_widget) {
        if (gui_state.focus_widget->type == WIDGET_INPUT) {
            char c = (char)event->data1;
            
            if (c >= 32 && c <= 126) {
                InputData* input = (InputData*)gui_state.focus_widget->data;
                if (input && input->buffer) {
                    if (input->buffer[0] != '\0' && 
                        gui_strlen(input->buffer) < 20 &&
                        strcmp(input->buffer, "Type here...") == 0) {
                        input->buffer[0] = '\0';
                        input->cursor_pos = 0;
                        input->selection_start = 0;
                        input->selection_end = 0;
                    }
                }
                
                wg_input_insert_char(gui_state.focus_widget, c);
            }
        }
        return;
    }

    if (gui_state.dragging_window) {
        Window* window = gui_state.dragging_window;
        
        if (!IS_VALID_WINDOW_PTR(window)) {
            gui_state.dragging_window = NULL;
            return;
        }
        
        if (event->type == EVENT_MOUSE_MOVE) {
            if (window->maximized) {
                gui_state.dragging_window = NULL;
                return;
            }
            
            int32_t new_x = mx - window->drag_offset_x;
            int32_t new_y = my - window->drag_offset_y;
            
            wm_move_window(window, new_x, new_y);
            window->needs_redraw = 1;
        }
        else if (event->type == EVENT_MOUSE_RELEASE && button == 0) {
            gui_state.dragging_window = NULL;
        }
        
        return;
    }

    Window* focused = wm_get_focused_window();
    if (focused && focused->resizing && focused->resizable) {
        if (event->type == EVENT_MOUSE_MOVE) {
            wm_do_resize(focused, mx, my);
        } else if (event->type == EVENT_MOUSE_RELEASE && button == 0) {
            wm_end_resize(focused);
        }
        return;
    }

    if (my >= (int32_t)(gui_state.screen_height - TASKBAR_HEIGHT)) {
        taskbar_handle_event(event);
        return;
    }
    
    Window* start_win = start_menu_get_window();
    if (start_menu_is_visible() && start_win && IS_VALID_WINDOW_PTR(start_win)) {
        uint8_t in_start_menu = point_in_rect_static(mx, my, start_win->x, start_win->y, 
                                                   start_win->width, start_win->height);
        
        if (in_start_menu) {
            if (event->type == EVENT_MOUSE_CLICK && button == 0) {
                wm_focus_window(start_win);
                
                Widget* widget = start_win->first_widget;
                while (widget) {
                    if (widget->visible && widget->enabled &&
                        point_in_rect_static(mx, my, widget->x, widget->y, 
                                           widget->width, widget->height)) {
                        
                        widget->state = STATE_PRESSED;
                        widget->needs_redraw = 1;
                        start_win->needs_redraw = 1;
                        
                        if (widget->on_click) {
                            widget->on_click(widget, widget->userdata);
                        }
                        return;
                    }
                    widget = widget->next;
                }
            }
            else if (event->type == EVENT_MOUSE_MOVE) {
                wm_focus_window(start_win);
                
                Widget* widget = start_win->first_widget;
                while (widget) {
                    if (widget->visible && widget->enabled) {
                        uint8_t is_hovering = point_in_rect_static(mx, my, 
                                                                  widget->x, widget->y,
                                                                  widget->width, widget->height);
                        
                        if (is_hovering) {
                            if (widget->state != STATE_HOVER) {
                                widget->state = STATE_HOVER;
                                widget->needs_redraw = 1;
                                start_win->needs_redraw = 1;
                            }
                        } else {
                            if (widget->state == STATE_HOVER) {
                                widget->state = STATE_NORMAL;
                                widget->needs_redraw = 1;
                                start_win->needs_redraw = 1;
                            }
                        }
                    }
                    widget = widget->next;
                }
                return;
            }
            else if (event->type == EVENT_MOUSE_RELEASE && button == 0) {
                Widget* widget = start_win->first_widget;
                while (widget) {
                    if (widget->state == STATE_PRESSED) {
                        if (point_in_rect_static(mx, my, widget->x, widget->y,
                                                widget->width, widget->height)) {
                            widget->state = STATE_HOVER;
                        } else {
                            widget->state = STATE_NORMAL;
                        }
                        widget->needs_redraw = 1;
                        start_win->needs_redraw = 1;
                    }
                    widget = widget->next;
                }
                return;
            }
        }
    }

    if (event->type == EVENT_MOUSE_CLICK && button == 0) {
        Window* window = wm_find_window_at(mx, my);
        
        if (window && IS_VALID_WINDOW_PTR(window)) {
            wm_focus_window(window);
            
            uint8_t widget_was_clicked = 0;
            Widget* widget = window->first_widget;
            
            while (widget) {
                if (widget->visible && widget->enabled &&
                    point_in_rect_static(mx, my, widget->x, widget->y, 
                                       widget->width, widget->height)) {
                    
                    widget_was_clicked = 1;
                    
                    if (widget->can_focus) {
                        gui_set_focus(widget);
                    }
                    
                    widget->state = STATE_PRESSED;
                    widget->needs_redraw = 1;
                    window->needs_redraw = 1;
                    
                    if (widget->type == WIDGET_INPUT) {
                        handle_input_mouse_click(widget, mx, my, shift);
                        widget->dragging = 1;
                    }
                    else if (widget->type == WIDGET_CHECKBOX && widget->data) {
                        uint8_t* checked = (uint8_t*)widget->data;
                        *checked = !(*checked);
                        if (widget->on_change) {
                            widget->on_change(widget, widget->userdata);
                        }
                    }
                    else if (widget->type == WIDGET_SLIDER && widget->data) {
                        widget->dragging = 1;
                        uint32_t* data = (uint32_t*)widget->data;
                        uint32_t min = data[0];
                        uint32_t max = data[1];
                        
                        if (max > min) {
                            int32_t relative_x = mx - widget->x;
                            if (relative_x < 0) relative_x = 0;
                            if (relative_x > (int32_t)widget->width) relative_x = widget->width;
                            uint32_t value = min + (relative_x * (max - min)) / widget->width;
                            if (value < min) value = min;
                            if (value > max) value = max;
                            data[2] = value;
                            if (widget->on_change) {
                                widget->on_change(widget, widget->userdata);
                            }
                        }
                    }
                    else if (widget->type == WIDGET_DROPDOWN && widget->data) {
                        DropdownData* dd = (DropdownData*)widget->data;
                        dd->expanded = !dd->expanded;
                        if (widget->on_click) {
                            widget->on_click(widget, widget->userdata);
                        }
                    }
                    else if (widget->type == WIDGET_LIST && widget->data) {
                        ListData* list = (ListData*)widget->data;
                        int32_t relative_y = my - widget->y - 20;
                        if (relative_y >= 0 && relative_y < (int32_t)(list->visible_items * 20)) {
                            uint32_t index = list->scroll_offset + relative_y / 20;
                            if (index < list->item_count) {
                                list->selected_index = index;
                                if (widget->on_change) {
                                    widget->on_change(widget, widget->userdata);
                                }
                            }
                        }
                    }
                    else if (widget->type == WIDGET_SCROLLBAR && widget->data) {
                        ScrollbarData* sb = (ScrollbarData*)widget->data;
                        if (sb->vertical) {
                            int32_t handle_y = widget->y + 16;
                            int32_t handle_height = 20;
                            
                            if (my < widget->y + 16) {
                                uint32_t new_value = sb->value - sb->page_size;
                                if (new_value < sb->min) new_value = sb->min;
                                sb->value = new_value;
                                if (widget->on_change) {
                                    widget->on_change(widget, widget->userdata);
                                }
                            }
                            else if (my >= widget->y + (int32_t)widget->height - 16) {
                                uint32_t new_value = sb->value + sb->page_size;
                                if (new_value > sb->max) new_value = sb->max;
                                sb->value = new_value;
                                if (widget->on_change) {
                                    widget->on_change(widget, widget->userdata);
                                }
                            }
                            else if (my >= handle_y && my < handle_y + handle_height) {
                                sb->dragging = 1;
                                sb->drag_start_pos = my;
                                sb->drag_start_value = sb->value;
                            }
                            else {
                                int32_t track_height = widget->height - 32;
                                if (track_height > 0) {
                                    float ratio = (float)(my - widget->y - 16) / track_height;
                                    uint32_t new_value = sb->min + (uint32_t)(ratio * (sb->max - sb->min));
                                    if (new_value < sb->min) new_value = sb->min;
                                    if (new_value > sb->max) new_value = sb->max;
                                    sb->value = new_value;
                                    if (widget->on_change) {
                                        widget->on_change(widget, widget->userdata);
                                    }
                                }
                            }
                        }
                    }
                    
                    if (widget->on_click) {
                        widget->on_click(widget, widget->userdata);
                    }
                    
                    break;
                }
                widget = widget->next;
            }
            
            if (!widget_was_clicked && window->has_titlebar) {
                int32_t title_y = window->y;
                uint32_t title_h = window->title_height;
                
                if (my >= title_y && my < title_y + (int32_t)title_h) {
                    if (window->closable) {
                        int32_t close_x = window->maximized ? 
                                         (int32_t)gui_state.screen_width - 25 : 
                                         window->x + (int32_t)window->width - 25;
                        int32_t close_y = window->y + 5;
                        
                        if (mx >= close_x && mx < close_x + 15 &&
                            my >= close_y && my < close_y + 15) {
                            wm_close_window(window);
                            return;
                        }
                    }
                    
                    if (window->minimizable) {
                        int32_t min_x = window->maximized ? 
                                       (int32_t)gui_state.screen_width - (window->closable ? 45 : 25) : 
                                       window->x + (int32_t)window->width - (window->closable ? 45 : 25);
                        int32_t min_y = window->y + 5;
                        
                        if (mx >= min_x && mx < min_x + 15 &&
                            my >= min_y && my < min_y + 15) {
                            wm_minimize_window(window);
                            return;
                        }
                    }
                    
                    if (window->maximizable) {
                        int32_t max_x = window->maximized ? 
                                       (int32_t)gui_state.screen_width - (window->closable ? 65 : 45) : 
                                       window->x + (int32_t)window->width - (window->closable ? 65 : 45);
                        int32_t max_y = window->y + 5;
                        
                        if (mx >= max_x && mx < max_x + 15 &&
                            my >= max_y && my < max_y + 15) {
                            if (window->maximized) {
                                wm_restore_window(window);
                            } else {
                                wm_maximize_window(window);
                            }
                            return;
                        }
                    }
                    
                    if (window->movable && !window->minimized && !window->maximized) {
                        window->drag_offset_x = mx - window->x;
                        window->drag_offset_y = my - window->y;
                        gui_state.dragging_window = window;
                        window->needs_redraw = 1;
                        return;
                    }
                }
                
                if (window->resizable && !window->maximized && !window->minimized) {
                    uint32_t edge_size = 8;
                    uint32_t corner_size = 12;
                    
                    if (mx <= window->x + (int32_t)corner_size && my <= window->y + (int32_t)corner_size) {
                        wm_start_resize(window, 1, mx, my);
                        return;
                    }
                    if (mx >= window->x + (int32_t)window->width - (int32_t)corner_size && 
                        my <= window->y + (int32_t)corner_size) {
                        wm_start_resize(window, 2, mx, my);
                        return;
                    }
                    if (mx <= window->x + (int32_t)corner_size && 
                        my >= window->y + (int32_t)window->height - (int32_t)corner_size) {
                        wm_start_resize(window, 3, mx, my);
                        return;
                    }
                    if (mx >= window->x + (int32_t)window->width - (int32_t)corner_size && 
                        my >= window->y + (int32_t)window->height - (int32_t)corner_size) {
                        wm_start_resize(window, 4, mx, my);
                        return;
                    }
                    
                    if (mx <= window->x + (int32_t)edge_size && my > window->y + (int32_t)title_h) {
                        wm_start_resize(window, 5, mx, my);
                        return;
                    }
                    if (mx >= window->x + (int32_t)window->width - (int32_t)edge_size && 
                        my > window->y + (int32_t)title_h) {
                        wm_start_resize(window, 6, mx, my);
                        return;
                    }
                    if (my >= window->y + (int32_t)window->height - (int32_t)edge_size) {
                        wm_start_resize(window, 8, mx, my);
                        return;
                    }
                }
            }
        } else {
            if (gui_state.focused_window && 
                IS_VALID_WINDOW_PTR(gui_state.focused_window)) {
                gui_state.focused_window->focused = 0;
                gui_state.focused_window->needs_redraw = 1;
            }
            gui_state.focused_window = NULL;
            gui_clear_focus();
        }
    }
    else if (event->type == EVENT_MOUSE_MOVE) {
        Window* window = gui_state.first_window;
        while (window) {
            if (IS_VALID_WINDOW_PTR(window)) {
                Widget* widget = window->first_widget;
                while (widget) {
                    if (widget->dragging) {
                        if (widget->type == WIDGET_INPUT) {
                            handle_input_mouse_drag(widget, mx, my);
                        }
                        else if (widget->type == WIDGET_SLIDER && widget->data) {
                            uint32_t* data = (uint32_t*)widget->data;
                            uint32_t min = data[0];
                            uint32_t max = data[1];
                            
                            int32_t relative_x = mx - widget->x;
                            if (relative_x < 0) relative_x = 0;
                            if (relative_x > (int32_t)widget->width) relative_x = widget->width;
                            
                            uint32_t value = min + (relative_x * (max - min)) / widget->width;
                            if (value < min) value = min;
                            if (value > max) value = max;
                            
                            data[2] = value;
                            widget->needs_redraw = 1;
                            window->needs_redraw = 1;
                            
                            if (widget->on_change) {
                                widget->on_change(widget, widget->userdata);
                            }
                        }
                        else if (widget->type == WIDGET_SCROLLBAR && widget->data) {
                            ScrollbarData* sb = (ScrollbarData*)widget->data;
                            if (sb->dragging && sb->vertical) {
                                int32_t delta = my - sb->drag_start_pos;
                                int32_t track_height = widget->height - 32;
                                if (track_height > 20) {
                                    float delta_ratio = (float)delta / (track_height - 20);
                                    int32_t value_delta = (int32_t)(delta_ratio * (sb->max - sb->min));
                                    int32_t new_value = sb->drag_start_value + value_delta;
                                    if (new_value < (int32_t)sb->min) new_value = sb->min;
                                    if (new_value > (int32_t)sb->max) new_value = sb->max;
                                    sb->value = new_value;
                                    if (widget->on_change) {
                                        widget->on_change(widget, widget->userdata);
                                    }
                                }
                            }
                        }
                        return;
                    }
                    widget = widget->next;
                }
            }
            window = window->next;
        }
        
        window = gui_state.first_window;
        while (window) {
            if (IS_VALID_WINDOW_PTR(window) && 
                window->visible && !window->minimized && 
                point_in_rect_static(mx, my, window->x, window->y, 
                                   window->width, window->height)) {
                
                Widget* widget = window->first_widget;
                while (widget) {
                    if (widget->visible && widget->enabled) {
                        uint8_t is_hovering = point_in_rect_static(mx, my, 
                                                                  widget->x, widget->y,
                                                                  widget->width, widget->height);
                        
                        if (is_hovering) {
                            if (widget->state != STATE_HOVER && widget->state != STATE_PRESSED) {
                                widget->state = STATE_HOVER;
                                widget->needs_redraw = 1;
                                window->needs_redraw = 1;
                            }
                        } else {
                            if (widget->state == STATE_HOVER) {
                                widget->state = STATE_NORMAL;
                                widget->needs_redraw = 1;
                                window->needs_redraw = 1;
                            }
                        }
                    }
                    widget = widget->next;
                }
                break;
            }
            window = window->next;
        }
    }
    else if (event->type == EVENT_MOUSE_RELEASE && button == 0) {
        Window* window = gui_state.first_window;
        while (window) {
            if (IS_VALID_WINDOW_PTR(window)) {
                Widget* widget = window->first_widget;
                while (widget) {
                    if (widget->dragging) {
                        widget->dragging = 0;
                        if (widget->type == WIDGET_SCROLLBAR && widget->data) {
                            ScrollbarData* sb = (ScrollbarData*)widget->data;
                            sb->dragging = 0;
                        }
                        widget->state = widget->focused ? STATE_FOCUSED : STATE_NORMAL;
                        widget->needs_redraw = 1;
                        window->needs_redraw = 1;
                    }
                    else if (widget->state == STATE_PRESSED) {
                        if (point_in_rect_static(mx, my, widget->x, widget->y,
                                                widget->width, widget->height)) {
                            widget->state = STATE_HOVER;
                        } else if (widget->focused) {
                            widget->state = STATE_FOCUSED;
                        } else {
                            widget->state = STATE_NORMAL;
                        }
                        widget->needs_redraw = 1;
                        window->needs_redraw = 1;
                    }
                    widget = widget->next;
                }
            }
            window = window->next;
        }
    }
    else if (event->type == EVENT_MOUSE_WHEEL) {
        Window* window = wm_find_window_at(mx, my);
        if (window && IS_VALID_WINDOW_PTR(window)) {
            Widget* widget = window->first_widget;
            while (widget) {
                if (widget->visible && widget->enabled &&
                    point_in_rect_static(mx, my, widget->x, widget->y,
                                       widget->width, widget->height)) {
                    
                    if (widget->type == WIDGET_LIST && widget->data) {
                        int32_t delta = (event->data2 & 0x80000000) ? -1 : 1;
                        wg_list_scroll(widget, -delta * 3);
                    }
                    break;
                }
                widget = widget->next;
            }
        }
    }
}

// ============ РЕНДЕРИНГ ============
void gui_render(void) {
    if (!gui_state.initialized) return;

    if (is_shutdown_mode_active()) {
        render_darken_effect();
        
        Window* dialog = get_shutdown_dialog();
        if (dialog && IS_VALID_WINDOW_PTR(dialog) && dialog->visible) {
            vesa_draw_rect(dialog->x, dialog->y, dialog->width, dialog->height, WINDOW_BG_COLOR);
            
            if (dialog->has_titlebar) {
                vesa_draw_rect(dialog->x, dialog->y, dialog->width, 
                             dialog->title_height, WINDOW_TITLE_ACTIVE);
                
                if (dialog->title) {
                    uint32_t text_x = dialog->x + 8;
                    uint32_t text_y = dialog->y + (dialog->title_height - 16) / 2;
                    vesa_draw_text(text_x, text_y, dialog->title, 
                                 0xFFFFFF, WINDOW_TITLE_ACTIVE);
                }
            }
            
            vesa_draw_rect(dialog->x, dialog->y, dialog->width, 1, WINDOW_BORDER_COLOR);
            vesa_draw_rect(dialog->x, dialog->y + dialog->height - 1, 
                         dialog->width, 1, WINDOW_BORDER_COLOR);
            vesa_draw_rect(dialog->x, dialog->y, 1, dialog->height, WINDOW_BORDER_COLOR);
            vesa_draw_rect(dialog->x + dialog->width - 1, dialog->y, 
                         1, dialog->height, WINDOW_BORDER_COLOR);
            
            Widget* widget = dialog->first_widget;
            while (widget) {
                if (!widget->visible) {
                    widget = widget->next;
                    continue;
                }
                
                if (widget->type == WIDGET_BUTTON) {
                    uint32_t btn_color;
                    switch (widget->state) {
                        case STATE_HOVER: btn_color = WINDOW_BUTTON_HOVER; break;
                        case STATE_PRESSED: btn_color = WINDOW_BUTTON_PRESSED; break;
                        case STATE_DISABLED: btn_color = 0xCCCCCC; break;
                        default: btn_color = WINDOW_BUTTON_COLOR; break;
                    }
                    
                    vesa_draw_rect(widget->x, widget->y, widget->width, widget->height, btn_color);
                    vesa_draw_rect(widget->x, widget->y, widget->width, 1, WINDOW_BORDER_COLOR);
                    vesa_draw_rect(widget->x, widget->y + widget->height - 1, 
                                 widget->width, 1, WINDOW_BORDER_COLOR);
                    vesa_draw_rect(widget->x, widget->y, 1, widget->height, WINDOW_BORDER_COLOR);
                    vesa_draw_rect(widget->x + widget->width - 1, widget->y, 
                                 1, widget->height, WINDOW_BORDER_COLOR);
                    
                    if (widget->text) {
                        uint32_t text_len = gui_strlen(widget->text);
                        uint32_t text_x = widget->x + (widget->width - text_len * 8) / 2;
                        uint32_t text_y = widget->y + (widget->height - 16) / 2;
                        vesa_draw_text(text_x, text_y, widget->text, 0x000000, btn_color);
                    }
                }
                else if (widget->type == WIDGET_LABEL) {
                    if (widget->text) {
                        vesa_draw_text(widget->x, widget->y, widget->text, 0x000000, WINDOW_BG_COLOR);
                    }
                }
                
                widget = widget->next;
            }
            
            vesa_mark_dirty(dialog->x - 5, dialog->y - 5,
                           dialog->width + 10, dialog->height + 10);
        }
        
        return;
    }
    
    uint8_t has_visible_windows = 0;
    Window* window = gui_state.first_window;
    while (window) {
        if (IS_VALID_WINDOW_PTR(window) && window->visible && !window->minimized) {
            has_visible_windows = 1;
            break;
        }
        window = window->next;
    }
    
    if (has_visible_windows) {
        Window* windows[64];
        uint32_t count = 0;
        
        window = gui_state.first_window;
        while (window && count < 64) {
            if (IS_VALID_WINDOW_PTR(window) && window->visible && !window->minimized) {
                windows[count++] = window;
            }
            window = window->next;
        }
        
        for (uint32_t i = 0; i < count - 1; i++) {
            for (uint32_t j = 0; j < count - i - 1; j++) {
                if (windows[j]->z_index > windows[j + 1]->z_index) {
                    Window* temp = windows[j];
                    windows[j] = windows[j + 1];
                    windows[j + 1] = temp;
                }
            }
        }
        
        for (uint32_t i = 0; i < count; i++) {
            window = windows[i];
            
            if (!IS_VALID_WINDOW_PTR(window) || !window->visible || window->minimized) 
                continue;

            if (!window->needs_redraw && !window->dragging && !window->resizing) {
                continue;
            }
            
            if (window->has_titlebar && !window->maximized) {
                vesa_draw_rect(window->x + 2, window->y + 2, 
                             window->width, window->height, 0x888888);
            }
            
            vesa_draw_rect(window->x, window->y, window->width, window->height, WINDOW_BG_COLOR);
            
            if (window->has_titlebar) {
                uint32_t title_color = window->focused ? WINDOW_TITLE_ACTIVE : WINDOW_TITLE_COLOR;
                vesa_draw_rect(window->x, window->y, window->width, 
                             window->title_height, title_color);
                
                if (window->title) {
                    uint32_t text_x = window->x + 8;
                    uint32_t text_y = window->y + (window->title_height - 16) / 2;
                    vesa_draw_text(text_x, text_y, window->title, 
                                 0xFFFFFF, title_color);
                }
                
                uint32_t button_y = window->y + 5;
                
                if (window->maximizable) {
                    int32_t max_x;
                    if (window->maximized) {
                        max_x = gui_state.screen_width - (window->closable ? 65 : 45);
                    } else {
                        max_x = window->x + window->width - (window->closable ? 65 : 45);
                    }
                    
                    vesa_draw_rect(max_x, button_y, 15, 15, WINDOW_BUTTON_COLOR);
                    
                    vesa_draw_rect(max_x + 3, button_y + 3, 9, 1, 0x000000);
                    vesa_draw_rect(max_x + 3, button_y + 11, 9, 1, 0x000000);
                    vesa_draw_rect(max_x + 3, button_y + 3, 1, 9, 0x000000);
                    vesa_draw_rect(max_x + 11, button_y + 3, 1, 9, 0x000000);
                    
                    vesa_draw_rect(max_x, button_y, 15, 1, WINDOW_BORDER_COLOR);
                    vesa_draw_rect(max_x, button_y + 14, 15, 1, WINDOW_BORDER_COLOR);
                    vesa_draw_rect(max_x, button_y, 1, 15, WINDOW_BORDER_COLOR);
                    vesa_draw_rect(max_x + 14, button_y, 1, 15, WINDOW_BORDER_COLOR);
                }
                
                if (window->minimizable) {
                    int32_t min_x;
                    if (window->maximized) {
                        min_x = gui_state.screen_width - (window->closable ? 45 : 25);
                    } else {
                        min_x = window->x + window->width - (window->closable ? 45 : 25);
                    }
                    vesa_draw_rect(min_x, button_y, 15, 15, WINDOW_BUTTON_COLOR);
                    vesa_draw_rect(min_x + 4, button_y + 7, 7, 1, 0x000000);
                    vesa_draw_rect(min_x, button_y, 15, 1, WINDOW_BORDER_COLOR);
                    vesa_draw_rect(min_x, button_y + 14, 15, 1, WINDOW_BORDER_COLOR);
                    vesa_draw_rect(min_x, button_y, 1, 15, WINDOW_BORDER_COLOR);
                    vesa_draw_rect(min_x + 14, button_y, 1, 15, WINDOW_BORDER_COLOR);
                }
                
                if (window->closable) {
                    int32_t close_x;
                    if (window->maximized) {
                        close_x = gui_state.screen_width - 25;
                    } else {
                        close_x = window->x + window->width - 25;
                    }
                    vesa_draw_rect(close_x, button_y, 15, 15, WINDOW_BUTTON_COLOR);
                    vesa_draw_line(close_x + 4, button_y + 4, close_x + 10, button_y + 10, 0x000000);
                    vesa_draw_line(close_x + 10, button_y + 4, close_x + 4, button_y + 10, 0x000000);
                    vesa_draw_rect(close_x, button_y, 15, 1, WINDOW_BORDER_COLOR);
                    vesa_draw_rect(close_x, button_y + 14, 15, 1, WINDOW_BORDER_COLOR);
                    vesa_draw_rect(close_x, button_y, 1, 15, WINDOW_BORDER_COLOR);
                    vesa_draw_rect(close_x + 14, button_y, 1, 15, WINDOW_BORDER_COLOR);
                }
            }
            
            uint32_t border_color = WINDOW_BORDER_COLOR;
            if (!window->maximized) {
                vesa_draw_rect(window->x, window->y, window->width, 1, border_color);
                vesa_draw_rect(window->x, window->y + window->height - 1, 
                             window->width, 1, border_color);
                vesa_draw_rect(window->x, window->y, 1, window->height, border_color);
                vesa_draw_rect(window->x + window->width - 1, window->y, 
                             1, window->height, border_color);
                
                if (window->resizable && !window->maximized) {
                    uint32_t corner_size = 8;
                    vesa_draw_line(window->x + window->width - corner_size, 
                                 window->y + window->height - 1,
                                 window->x + window->width - 1,
                                 window->y + window->height - corner_size,
                                 0x808080);
                    vesa_draw_line(window->x, window->y + window->height - corner_size,
                                 window->x + corner_size, window->y + window->height - 1,
                                 0x808080);
                }
            } else {
                vesa_draw_rect(window->x, window->y + window->height - 1, 
                             window->width, 1, border_color);
            }
            
            Widget* widget = window->first_widget;
            while (widget) {
                if (!widget->visible) {
                    widget = widget->next;
                    continue;
                }
                
                if (widget->draw) {
                    widget->draw(widget);
                } else {
                    switch (widget->type) {
                        case WIDGET_BUTTON: {
                            uint32_t btn_color;
                            switch (widget->state) {
                                case STATE_HOVER: btn_color = WINDOW_BUTTON_HOVER; break;
                                case STATE_PRESSED: btn_color = WINDOW_BUTTON_PRESSED; break;
                                case STATE_DISABLED: btn_color = 0xCCCCCC; break;
                                default: btn_color = WINDOW_BUTTON_COLOR; break;
                            }
                            
                            vesa_draw_rect(widget->x, widget->y, widget->width, widget->height, btn_color);
                            vesa_draw_rect(widget->x, widget->y, widget->width, 1, border_color);
                            vesa_draw_rect(widget->x, widget->y + widget->height - 1, 
                                         widget->width, 1, border_color);
                            vesa_draw_rect(widget->x, widget->y, 1, widget->height, border_color);
                            vesa_draw_rect(widget->x + widget->width - 1, widget->y, 
                                         1, widget->height, border_color);
                            
                            if (widget->text) {
                                uint32_t text_len = gui_strlen(widget->text);
                                uint32_t text_x = widget->x + (widget->width - text_len * 8) / 2;
                                uint32_t text_y = widget->y + (widget->height - 16) / 2;
                                
                                if (text_x < widget->x + 4) text_x = widget->x + 4;
                                if (text_y < widget->y + 2) text_y = widget->y + 2;
                                
                                uint32_t text_color = (widget->state == STATE_DISABLED) ? 0x888888 : 0x000000;
                                vesa_draw_text(text_x, text_y, widget->text, text_color, btn_color);
                            }
                            break;
                        }
                        case WIDGET_LABEL:
                            if (widget->text) {
                                vesa_draw_text(widget->x, widget->y, widget->text, 0x000000, WINDOW_BG_COLOR);
                            }
                            break;
                        case WIDGET_CHECKBOX: {
                            uint32_t box_x = widget->x;
                            uint32_t box_y = widget->y + 2;
                            uint32_t box_size = 14;
                            
                            vesa_draw_rect(box_x, box_y, box_size, box_size, CHECKBOX_COLOR);
                            vesa_draw_rect(box_x, box_y, box_size, 1, WINDOW_BORDER_COLOR);
                            vesa_draw_rect(box_x, box_y + box_size - 1, box_size, 1, WINDOW_BORDER_COLOR);
                            vesa_draw_rect(box_x, box_y, 1, box_size, WINDOW_BORDER_COLOR);
                            vesa_draw_rect(box_x + box_size - 1, box_y, 1, box_size, WINDOW_BORDER_COLOR);
                            
                            if (widget->data && *((uint8_t*)widget->data)) {
                                vesa_draw_rect(box_x + 3, box_y + 3, 8, 8, CHECKBOX_CHECKED_COLOR);
                            }
                            
                            if (widget->text) {
                                vesa_draw_text(widget->x + 20, widget->y, widget->text, 0x000000, WINDOW_BG_COLOR);
                            }
                            break;
                        }
                        case WIDGET_SLIDER: {
                            uint32_t* data = (uint32_t*)widget->data;
                            if (!data) break;
                            
                            uint32_t min = data[0];
                            uint32_t max = data[1];
                            uint32_t value = data[2];
                            
                            uint32_t track_height = 6;
                            uint32_t track_y = widget->y + (widget->height - track_height) / 2;
                            
                            vesa_draw_rect(widget->x, track_y, widget->width, track_height, SLIDER_TRACK_COLOR);
                            vesa_draw_rect(widget->x, track_y, widget->width, 1, 0x606060);
                            vesa_draw_rect(widget->x, track_y + track_height - 1, widget->width, 1, 0xA0A0A0);
                            
                            if (max > min) {
                                uint32_t fill_width = (value - min) * widget->width / (max - min);
                                if (fill_width > 0) {
                                    vesa_draw_rect(widget->x, track_y, fill_width, track_height, SLIDER_FILL_COLOR);
                                }
                            }
                            
                            uint32_t handle_size = 16;
                            uint32_t handle_x = widget->x;
                            if (max > min) {
                                handle_x = widget->x + (value - min) * (widget->width - handle_size) / (max - min);
                            }
                            uint32_t handle_y = widget->y + (widget->height - handle_size) / 2;
                            
                            vesa_draw_rect(handle_x, handle_y, handle_size, handle_size, SLIDER_HANDLE_COLOR);
                            vesa_draw_rect(handle_x, handle_y, handle_size, 1, 0x808080);
                            vesa_draw_rect(handle_x, handle_y + handle_size - 1, handle_size, 1, 0x404040);
                            vesa_draw_rect(handle_x, handle_y, 1, handle_size, 0x808080);
                            vesa_draw_rect(handle_x + handle_size - 1, handle_y, 1, handle_size, 0x404040);
                            break;
                        }
                        case WIDGET_PROGRESSBAR: {
                            uint32_t value = 0;
                            if (widget->data) {
                                value = *((uint32_t*)widget->data);
                            }
                            
                            vesa_draw_rect(widget->x, widget->y, widget->width, widget->height, PROGRESSBAR_BG_COLOR);
                            
                            vesa_draw_rect(widget->x, widget->y, widget->width, 1, 0x808080);
                            vesa_draw_rect(widget->x, widget->y + widget->height - 1, widget->width, 1, 0x808080);
                            vesa_draw_rect(widget->x, widget->y, 1, widget->height, 0x808080);
                            vesa_draw_rect(widget->x + widget->width - 1, widget->y, 1, widget->height, 0x808080);
                            
                            if (value > 0 && widget->width > 2) {
                                uint32_t fill_width = (value * (widget->width - 2)) / 100;
                                if (fill_width > 0) {
                                    vesa_draw_rect(widget->x + 1, widget->y + 1, fill_width, widget->height - 2, PROGRESSBAR_FILL_COLOR);
                                    
                                    if (fill_width > 4) {
                                        vesa_draw_rect(widget->x + 1, widget->y + 1, fill_width, 1, 0xA0D0FF);
                                        vesa_draw_rect(widget->x + 1, widget->y + widget->height - 2, fill_width, 1, 0x2050A0);
                                    }
                                }
                            }
                            break;
                        }
                        case WIDGET_INPUT: {
                            uint32_t bg_color = INPUT_BG_COLOR;
                            vesa_draw_rect(widget->x, widget->y, widget->width, widget->height, bg_color);
                            
                            uint32_t border = (widget->focused) ? 0x007ACC : 0x808080;
                            vesa_draw_rect(widget->x, widget->y, widget->width, 1, border);
                            vesa_draw_rect(widget->x, widget->y + widget->height - 1, widget->width, 1, border);
                            vesa_draw_rect(widget->x, widget->y, 1, widget->height, border);
                            vesa_draw_rect(widget->x + widget->width - 1, widget->y, 1, widget->height, border);
                            
                            InputData* input = (InputData*)widget->data;
                            if (input && input->buffer) {
                                uint32_t text_x = widget->x + 4;
                                uint32_t text_y = widget->y + (widget->height - 16) / 2;
                                
                                char* display_text = input->buffer;
                                
                                uint32_t max_chars = (widget->width - 8) / 8;
                                if (max_chars < 1) max_chars = 1;
                                
                                uint32_t len = gui_strlen(display_text);
                                if (input->cursor_pos > input->scroll_offset + max_chars - 1) {
                                    input->scroll_offset = input->cursor_pos - max_chars + 1;
                                }
                                if (input->cursor_pos < input->scroll_offset) {
                                    input->scroll_offset = input->cursor_pos;
                                }
                                
                                char visible_text[256];
                                uint32_t visible_len = 0;
                                for (uint32_t i = input->scroll_offset; 
                                     i < len && visible_len < max_chars; i++, visible_len++) {
                                    visible_text[visible_len] = display_text[i];
                                }
                                visible_text[visible_len] = '\0';
                                
                                // Рисуем выделение (СИНИМ!) — сначала фон
                                if (input->selection_start != input->selection_end && widget->focused) {
                                    uint32_t sel_start = input->selection_start;
                                    uint32_t sel_end = input->selection_end;
                                    if (sel_start > sel_end) {
                                        uint32_t temp = sel_start;
                                        sel_start = sel_end;
                                        sel_end = temp;
                                    }
                                    
                                    uint32_t vis_start = sel_start > input->scroll_offset ? 
                                                         sel_start - input->scroll_offset : 0;
                                    uint32_t vis_end = sel_end - input->scroll_offset;
                                    if (vis_end > max_chars) vis_end = max_chars;
                                    
                                    // Рисуем синий фон ДО текста
                                    for (uint32_t pos = vis_start; pos < vis_end && pos < max_chars; pos++) {
                                        if (pos < visible_len) { // Только если символ существует
                                            vesa_draw_rect(text_x + pos * 8, text_y, 8, 16, INPUT_SELECTION_COLOR);
                                        }
                                    }
                                }
                                
                                // Рисуем текст поверх (белым если в выделении)
                                for (uint32_t i = 0; i < visible_len; i++) {
                                    uint32_t global_pos = input->scroll_offset + i;
                                    uint32_t char_x = text_x + i * 8;
                                    uint32_t char_y = text_y;
                                    
                                    // Проверяем, попадает ли символ в выделение
                                    uint8_t is_selected = 0;
                                    if (widget->focused && input->selection_start != input->selection_end) {
                                        uint32_t sel_start = input->selection_start;
                                        uint32_t sel_end = input->selection_end;
                                        if (sel_start > sel_end) {
                                            uint32_t temp = sel_start;
                                            sel_start = sel_end;
                                            sel_end = temp;
                                        }
                                        is_selected = (global_pos >= sel_start && global_pos < sel_end);
                                    }
                                    
                                    if (input->password_mode) {
                                        char c = '*';
                                        uint32_t color = is_selected ? 0xFFFFFF : INPUT_TEXT_COLOR;
                                        vesa_draw_char(char_x, char_y, c, color, 
                                                      is_selected ? INPUT_SELECTION_COLOR : bg_color);
                                    } else {
                                        char c = visible_text[i];
                                        uint32_t color = is_selected ? 0xFFFFFF : INPUT_TEXT_COLOR;
                                        vesa_draw_char(char_x, char_y, c, color,
                                                      is_selected ? INPUT_SELECTION_COLOR : bg_color);
                                    }
                                }
                                
                                // Рисуем курсор
                                if (widget->focused) {
                                    static uint32_t last_blink = 0;
                                    uint32_t now = timer_get_ticks();
                                    if (now - last_blink > 50) {
                                        input->cursor_visible = !input->cursor_visible;
                                        last_blink = now;
                                    }
                                    
                                    if (input->cursor_visible) {
                                        uint32_t cursor_x = text_x + (input->cursor_pos - input->scroll_offset) * 8;
                                        if (cursor_x >= widget->x && cursor_x < widget->x + widget->width - 4) {
                                            vesa_draw_line(cursor_x, text_y, cursor_x, text_y + 16, INPUT_TEXT_COLOR);
                                        }
                                    }
                                }
                            }
                            break;
                        }
                        default:
                            break;
                    }
                }
                
                widget = widget->next;
            }
            
            vesa_mark_dirty(window->x - 5, window->y - 5,
                           window->width + 10, window->height + 10);
            
            // window->needs_redraw = 0;
        }
    }
    
    taskbar_render();
    
    if (start_menu_is_visible()) {
        Window* start_win = start_menu_get_window();
        if (start_win && IS_VALID_WINDOW_PTR(start_win)) {
            start_win->needs_redraw = 1;
        }
    }
}

void gui_force_redraw(void) {
    Window* window = gui_state.first_window;
    while (window) {
        if (IS_VALID_WINDOW_PTR(window)) {
            window->needs_redraw = 1;
        }
        window = window->next;
    }
}

void check_stack_overflow(void) {
    uint32_t current_esp;
    asm volatile("mov %%esp, %0" : "=r"(current_esp));
    
    uint32_t stack_bottom_addr = (uint32_t)&stack_guard - 16384;
    uint32_t stack_used = (uint32_t)&stack_guard - current_esp;
    
    if (stack_used > 15872) {
        serial_puts("\n[WARNING] Stack almost full! Used: ");
        serial_puts_num(stack_used);
        serial_puts(" bytes\n");
    }
    
    if (stack_guard != 0xDEADBEEF) {
        vesa_fill(0x000000);
        vesa_draw_text(200, 300, "KERNEL PANIC: Stack Overflow", 0xFF0000, 0x000000);
        vesa_draw_text(200, 330, "Check serial output", 0xFFFFFF, 0x000000);
        vesa_swap_buffers();
        
        serial_puts("\n*** KERNEL PANIC: Stack Overflow ***\n");
        serial_puts("Guard value: 0x");
        serial_puts_num_hex(stack_guard);
        serial_puts("\n");
        
        asm volatile("cli");
        while(1) asm volatile("hlt");
    }
}

uint32_t get_screen_width(void) { 
    return gui_state.screen_width; 
}

uint32_t get_screen_height(void) { 
    return gui_state.screen_height; 
}