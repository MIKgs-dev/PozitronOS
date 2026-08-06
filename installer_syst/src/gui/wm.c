#include "gui/gui.h"
#include "drivers/serial.h"
#include "kernel/memory.h"

// ============ ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ ============
static inline void safe_remove_window_from_list(Window* window) {
    if (!window) return;
    
    if (window->prev) {
        window->prev->next = window->next;
    } else {
        gui_state.first_window = window->next;
    }
    
    if (window->next) {
        window->next->prev = window->prev;
    } else {
        gui_state.last_window = window->prev;
    }
    
    window->prev = NULL;
    window->next = NULL;
}

static inline void safe_add_window_to_list(Window* window) {
    if (!window) return;
    
    window->prev = gui_state.last_window;
    window->next = NULL;
    
    if (gui_state.last_window) {
        gui_state.last_window->next = window;
    } else {
        gui_state.first_window = window;
    }
    
    gui_state.last_window = window;
}

// ============ ФУНКЦИИ РЕСАЙЗА ============
void wm_start_resize(Window* window, uint32_t edge, int32_t mouse_x, int32_t mouse_y) {
    if (!IS_VALID_WINDOW_PTR(window) || !window->resizable || window->maximized) return;
    
    window->resizing = 1;
    window->resize_corner = edge;
    window->resize_start_x = mouse_x;
    window->resize_start_y = mouse_y;
    window->resize_start_width = window->width;
    window->resize_start_height = window->height;
    window->drag_offset_x = window->x;
    window->drag_offset_y = window->y;
    
    serial_puts("[WM] Start resize edge ");
    serial_puts_num(edge);
    serial_puts("\n");
}

void wm_do_resize(Window* window, int32_t mouse_x, int32_t mouse_y) {
    if (!IS_VALID_WINDOW_PTR(window) || !window->resizing) return;
    
    int32_t delta_x = mouse_x - window->resize_start_x;
    int32_t delta_y = mouse_y - window->resize_start_y;
    
    int32_t new_x = window->drag_offset_x;
    int32_t new_y = window->drag_offset_y;
    int32_t new_width = window->resize_start_width;
    int32_t new_height = window->resize_start_height;
    
    // Определяем, какой край/угол используется
    switch (window->resize_corner) {
        // УГЛЫ (для совместимости с cursor.c)
        case 1: // Верхний левый угол
            new_width = window->resize_start_width - delta_x;
            new_height = window->resize_start_height - delta_y;
            new_x = window->drag_offset_x + delta_x;
            new_y = window->drag_offset_y + delta_y;
            break;
            
        case 2: // Верхний правый угол
            new_width = window->resize_start_width + delta_x;
            new_height = window->resize_start_height - delta_y;
            new_y = window->drag_offset_y + delta_y;
            break;
            
        case 3: // Нижний левый угол
            new_width = window->resize_start_width - delta_x;
            new_height = window->resize_start_height + delta_y;
            new_x = window->drag_offset_x + delta_x;
            break;
            
        case 4: // Нижний правый угол
            new_width = window->resize_start_width + delta_x;
            new_height = window->resize_start_height + delta_y;
            break;
            
        // КРАЯ (новые значения)
        case 5: // Левый край
            new_width = window->resize_start_width - delta_x;
            new_x = window->drag_offset_x + delta_x;
            break;
            
        case 6: // Правый край
            new_width = window->resize_start_width + delta_x;
            break;
            
        case 7: // Верхний край
            new_height = window->resize_start_height - delta_y;
            new_y = window->drag_offset_y + delta_y;
            break;
            
        case 8: // Нижний край
            new_height = window->resize_start_height + delta_y;
            break;
    }
    
    // Ограничения минимального размера
    if (new_width < 100) {
        if (window->resize_corner == 1 || window->resize_corner == 3 || window->resize_corner == 5) 
            new_x = window->drag_offset_x + (window->resize_start_width - 100);
        new_width = 100;
    }
    if (new_height < 100) {
        if (window->resize_corner == 1 || window->resize_corner == 2 || window->resize_corner == 7) 
            new_y = window->drag_offset_y + (window->resize_start_height - 100);
        new_height = 100;
    }
    
    int32_t screen_width = gui_state.screen_width;
    int32_t screen_height = gui_state.screen_height - TASKBAR_HEIGHT;
    
    // Ограничения максимального размера
    if (new_width > screen_width - 50) {
        if (window->resize_corner == 2 || window->resize_corner == 4 || window->resize_corner == 6) {
            new_width = screen_width - 50;
        } else {
            new_width = screen_width - 50;
            new_x = screen_width - new_width;
        }
    }
    if (new_height > screen_height - 50) {
        if (window->resize_corner == 3 || window->resize_corner == 4 || window->resize_corner == 8) {
            new_height = screen_height - 50;
        } else {
            new_height = screen_height - 50;
            new_y = screen_height - new_height;
        }
    }
    
    // Железобетонные ограничения по краям экрана
    if (new_x < 0) new_x = 0;
    if (new_y < 0) new_y = 0;
    if (new_x + new_width > screen_width) new_x = screen_width - new_width;
    if (new_y + new_height > screen_height) new_y = screen_height - new_height;
    
    window->x = new_x;
    window->y = new_y;
    window->width = new_width;
    window->height = new_height;
    
    wg_update_all_widgets(window);
    window->needs_redraw = 1;
}

void wm_end_resize(Window* window) {
    if (!IS_VALID_WINDOW_PTR(window)) return;
    
    window->resizing = 0;
    
    if (!window->maximized) {
        window->normal_x = window->x;
        window->normal_y = window->y;
        window->normal_width = window->width;
        window->normal_height = window->height;
        window->orig_x = window->x;
        window->orig_y = window->y;
        window->orig_width = window->width;
        window->orig_height = window->height;
    }
    
    if (window->on_resize) {
        window->on_resize(window);
    }
    
    serial_puts("[WM] End resize\n");
}

void wm_resize_window(Window* window, uint32_t width, uint32_t height) {
    if (!IS_VALID_WINDOW_PTR(window) || !window->resizable) return;
    if (window->minimized || window->maximized) return;
    
    int32_t screen_width = gui_state.screen_width;
    int32_t screen_height = gui_state.screen_height - TASKBAR_HEIGHT;
    
    int32_t new_width = width;
    int32_t new_height = height;
    
    if (new_width < 100) new_width = 100;
    if (new_height < 100) new_height = 100;
    
    if (new_width > screen_width - 50) new_width = screen_width - 50;
    if (new_height > screen_height - 50) new_height = screen_height - 50;
    
    int32_t new_x = window->x;
    int32_t new_y = window->y;
    
    if (new_x + new_width > screen_width) {
        new_x = screen_width - new_width;
    }
    if (new_y + new_height > screen_height) {
        new_y = screen_height - new_height;
    }
    if (new_x < 0) new_x = 0;
    if (new_y < 0) new_y = 0;
    
    window->x = new_x;
    window->y = new_y;
    window->width = new_width;
    window->height = new_height;
    
    wg_update_all_widgets(window);
    
    if (window->on_resize) {
        window->on_resize(window);
    }
    
    window->needs_redraw = 1;
    
    serial_puts("[WM] Window resized: ");
    if (window->title) serial_puts(window->title);
    serial_puts(" (");
    serial_puts_num(width);
    serial_puts("x");
    serial_puts_num(height);
    serial_puts(")\n");
}

// ============ API WINDOW MANAGER ============
Window* wm_create_window(const char* title, uint32_t x, uint32_t y, 
                        uint32_t width, uint32_t height, uint8_t flags) {
    if (!gui_state.initialized) return NULL;
    if (gui_state.window_count >= 64) return NULL;
    if (width == 0 || height == 0) return NULL;
    
    int32_t screen_width = gui_state.screen_width;
    int32_t screen_height = gui_state.screen_height;
    
    int32_t new_x = x;
    int32_t new_y = y;
    
    if (new_x > screen_width - 100) new_x = screen_width - 100;
    if (new_y > screen_height - TASKBAR_HEIGHT - 100) new_y = screen_height - TASKBAR_HEIGHT - 100;
    if (new_x < 0) new_x = 0;
    if (new_y < 0) new_y = 0;
    
    int32_t new_width = width;
    int32_t new_height = height;
    
    if (new_width > screen_width - 50) new_width = screen_width - 50;
    if (new_height > screen_height - TASKBAR_HEIGHT - 50) new_height = screen_height - TASKBAR_HEIGHT - 50;
    
    Window* window = (Window*)kmalloc(sizeof(Window));
    if (!window) return NULL;
    
    window->id = gui_state.next_window_id++;
    window->x = new_x;
    window->y = new_y;
    window->width = new_width;
    window->height = new_height;
    window->title_height = 25;
    window->visible = 1;
    window->has_titlebar = (flags & WINDOW_HAS_TITLE) ? 1 : 0;
    window->z_index = gui_state.window_count;
    window->focused = 0;
    window->dragging = 0;
    window->resizing = 0;
    window->drag_offset_x = 0;
    window->drag_offset_y = 0;
    window->resize_corner = 0;
    window->resize_start_x = 0;
    window->resize_start_y = 0;
    window->resize_start_width = 0;
    window->resize_start_height = 0;
    window->minimized = 0;
    window->maximized = 0;
    window->first_widget = NULL;
    window->last_widget = NULL;
    window->next = NULL;
    window->prev = NULL;
    window->on_close = NULL;
    window->on_focus = NULL;
    window->on_minimize = NULL;
    window->on_maximize = NULL;
    window->on_restore = NULL;
    window->on_resize = NULL;
    window->closable = (flags & WINDOW_CLOSABLE) ? 1 : 0;
    window->movable = (flags & WINDOW_MOVABLE) ? 1 : 0;
    window->resizable = (flags & WINDOW_RESIZABLE) ? 1 : 0;
    window->minimizable = (flags & WINDOW_MINIMIZABLE) ? 1 : 0;
    window->maximizable = (flags & WINDOW_MAXIMIZABLE) ? 1 : 0;
    window->needs_redraw = 1;
    window->in_taskbar = 1;
    window->is_resizing = 0;
    window->focused_widget = NULL;
    
    window->orig_x = new_x;
    window->orig_y = new_y;
    window->orig_width = new_width;
    window->orig_height = new_height;
    
    window->normal_x = new_x;
    window->normal_y = new_y;
    window->normal_width = new_width;
    window->normal_height = new_height;
    
    window->orig_movable = window->movable;
    window->orig_resizable = window->resizable;
    
    if (title && title[0]) {
        uint32_t len = 0;
        while (title[len] && len < 63) len++;
        window->title = (char*)kmalloc(len + 1);
        if (window->title) {
            for (uint32_t i = 0; i < len; i++) window->title[i] = title[i];
            window->title[len] = '\0';
        } else {
            window->title = NULL;
        }
    } else {
        window->title = NULL;
    }
    
    gui_register_window(window);
    safe_add_window_to_list(window);
    gui_state.window_count++;
    
    if (window->in_taskbar) {
        taskbar_add_window(window);
    }
    
    wm_focus_window(window);
    return window;
}

void wm_destroy_window(Window* window) {
    if (!window || window->id == 0) return;
    
    uint32_t window_id = window->id;
    
    if (window->in_taskbar) {
        taskbar_remove_window(window);
    }
    
    if (gui_state.focused_window == window) {
        gui_state.focused_window = NULL;
    }
    
    if (gui_state.dragging_window == window) {
        gui_state.dragging_window = NULL;
    }
    
    if (gui_state.focus_widget && gui_state.focus_widget->parent_window == window) {
        gui_clear_focus();
    }
    
    window->dragging = 0;
    window->resizing = 0;
    
    if (window->on_close) {
        window->on_close(window);
    }
    
    Widget* widget = window->first_widget;
    while (widget) {
        Widget* next = widget->next;
        wg_destroy_widget(widget);
        widget = next;
    }
    
    if (window->title) {
        kfree(window->title);
        window->title = NULL;
    }
    
    safe_remove_window_from_list(window);
    gui_unregister_window(window_id);
    
    if (gui_state.window_count > 0) {
        gui_state.window_count--;
    }
    
    window->id = 0;
    kfree(window);
    
    int32_t z = 0;
    Window* w = gui_state.first_window;
    while (w) {
        if (IS_VALID_WINDOW_PTR(w)) {
            w->z_index = z++;
        }
        w = w->next;
    }
}

void wm_bring_to_front(Window* window) {
    if (!IS_VALID_WINDOW_PTR(window) || !gui_state.first_window) return;
    if (window == gui_state.last_window) return;
    
    safe_remove_window_from_list(window);
    safe_add_window_to_list(window);
    
    int32_t z = 0;
    Window* w = gui_state.first_window;
    while (w) {
        if (IS_VALID_WINDOW_PTR(w)) {
            w->z_index = z++;
            w = w->next;
        } else {
            w = w->next;
        }
    }
    
    wm_focus_window(window);
}

void wm_focus_window(Window* window) {
    if (!IS_VALID_WINDOW_PTR(window)) return;
    
    if (window->minimized) {
        wm_restore_window(window);
    }
    
    if (gui_state.focused_window && 
        IS_VALID_WINDOW_PTR(gui_state.focused_window) &&
        gui_state.focused_window != window) {
        gui_state.focused_window->focused = 0;
        gui_state.focused_window->needs_redraw = 1;
    }
    
    gui_state.focused_window = window;
    window->focused = 1;
    window->needs_redraw = 1;
    
    wm_bring_to_front(window);
    
    if (window->on_focus) {
        window->on_focus(window);
    }
    
    if (window->in_taskbar) {
        taskbar_update_window(window);
    }
}

Window* wm_get_focused_window(void) {
    if (gui_state.focused_window && IS_VALID_WINDOW_PTR(gui_state.focused_window)) {
        return gui_state.focused_window;
    }
    return NULL;
}

Window* wm_find_window_at(uint32_t x, uint32_t y) {
    Window* window = gui_state.last_window;
    while (window) {
        if (IS_VALID_WINDOW_PTR(window) && 
            window->visible && !window->minimized &&
            point_in_rect(x, y, window->x, window->y, 
                         window->width, window->height)) {
            return window;
        }
        window = window->prev;
    }
    return NULL;
}

void wm_move_window(Window* window, int32_t x, int32_t y) {
    if (!IS_VALID_WINDOW_PTR(window) || !window->movable || window->minimized || window->maximized) return;
    
    int32_t screen_width = gui_state.screen_width;
    int32_t screen_height = gui_state.screen_height - TASKBAR_HEIGHT;
    int32_t win_width = window->width;
    int32_t win_height = window->height;
    
    // Железобетонные ограничения
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x > screen_width - win_width) x = screen_width - win_width;
    if (y > screen_height - win_height) y = screen_height - win_height;
    
    // Финальная проверка
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    
    int32_t delta_x = x - window->x;
    int32_t delta_y = y - window->y;
    
    Widget* widget = window->first_widget;
    while (widget) {
        if (!widget->use_relative) {
            widget->x += delta_x;
            widget->y += delta_y;
        } else {
            wg_update_position(widget);
        }
        widget->needs_redraw = 1;
        widget = widget->next;
    }
    
    window->x = x;
    window->y = y;
    
    if (!window->maximized) {
        window->normal_x = x;
        window->normal_y = y;
        window->orig_x = x;
        window->orig_y = y;
    }
    
    window->needs_redraw = 1;
}

void wm_close_window(Window* window) {
    if (!IS_VALID_WINDOW_PTR(window) || !window->closable) return;
    wm_destroy_window(window);
}

void wm_maximize_window(Window* window) {
    if (!IS_VALID_WINDOW_PTR(window) || window->maximized || !window->maximizable) return;
    
    window->normal_x = window->x;
    window->normal_y = window->y;
    window->normal_width = window->width;
    window->normal_height = window->height;
    
    window->orig_movable = window->movable;
    window->orig_resizable = window->resizable;
    
    window->x = 0;
    window->y = 0;
    window->width = gui_state.screen_width;
    window->height = gui_state.screen_height - TASKBAR_HEIGHT;
    
    window->maximized = 1;
    window->movable = 0;
    window->resizable = 0;
    
    wg_update_all_widgets(window);
    window->needs_redraw = 1;
    
    if (window->on_maximize) {
        window->on_maximize(window);
    }
    
    serial_puts("[WM] Window maximized\n");
}

void wm_minimize_window(Window* window) {
    if (!IS_VALID_WINDOW_PTR(window) || !window->minimizable || window->minimized) return;
    
    uint8_t was_maximized = window->maximized;
    
    if (was_maximized) {
        window->orig_x = window->normal_x;
        window->orig_y = window->normal_y;
        window->orig_width = window->normal_width;
        window->orig_height = window->normal_height;
    } else {
        window->orig_x = window->x;
        window->orig_y = window->y;
        window->orig_width = window->width;
        window->orig_height = window->height;
    }
    
    window->visible = 0;
    window->minimized = 1;
    window->needs_redraw = 1;
    
    if (gui_state.focused_window == window) {
        window->focused = 0;
        gui_state.focused_window = NULL;
        
        Window* new_focus = NULL;
        Window* w = gui_state.first_window;
        while (w) {
            if (IS_VALID_WINDOW_PTR(w) && w != window && !w->minimized) {
                new_focus = w;
                break;
            }
            w = w->next;
        }
        
        if (new_focus) {
            wm_focus_window(new_focus);
        }
    }
    
    if (gui_state.focus_widget && gui_state.focus_widget->parent_window == window) {
        gui_clear_focus();
    }
    
    if (window->on_minimize) {
        window->on_minimize(window);
    }
    
    if (window->in_taskbar) {
        taskbar_update_window(window);
    }
    
    vesa_mark_dirty(window->x - 5, window->y - 5, 
                   window->width + 10, window->height + 10);
}

void wm_restore_window(Window* window) {
    if (!IS_VALID_WINDOW_PTR(window)) return;
    
    if (window->minimized) {
        window->visible = 1;
        window->minimized = 0;
        
        window->x = window->orig_x;
        window->y = window->orig_y;
        window->width = window->orig_width;
        window->height = window->orig_height;
        
        if (window->normal_width == gui_state.screen_width && 
            window->normal_height == gui_state.screen_height - TASKBAR_HEIGHT) {
            window->maximized = 1;
            window->movable = 0;
            window->resizable = 0;
        } else {
            window->maximized = 0;
            window->movable = window->orig_movable;
            window->resizable = window->orig_resizable;
        }
        
        wg_update_all_widgets(window);
        
        if (window->on_restore) {
            window->on_restore(window);
        }
        
        serial_puts("[WM] Window restored from minimized\n");
    }
    else if (window->maximized) {
        window->x = window->normal_x;
        window->y = window->normal_y;
        window->width = window->normal_width;
        window->height = window->normal_height;
        
        window->maximized = 0;
        window->movable = window->orig_movable;
        window->resizable = window->orig_resizable;
        
        wg_update_all_widgets(window);
        
        if (window->on_restore) {
            window->on_restore(window);
        }
        
        serial_puts("[WM] Window restored from maximized\n");
    }
    else {
        return;
    }
    
    if (window->in_taskbar) {
        taskbar_update_window(window);
    }
    
    window->needs_redraw = 1;
    wm_focus_window(window);
}

void wm_dump_info(void) {
    serial_puts("\n=== WINDOW MANAGER INFO ===\n");
    serial_puts("Total windows: ");
    serial_puts_num(gui_state.window_count);
    serial_puts("\n");
    
    serial_puts("Focused window: ");
    if (gui_state.focused_window && IS_VALID_WINDOW_PTR(gui_state.focused_window) && 
        gui_state.focused_window->title) {
        serial_puts(gui_state.focused_window->title);
        serial_puts(" (ID: ");
        serial_puts_num(gui_state.focused_window->id);
        serial_puts(")");
        if (gui_state.focused_window->minimized) serial_puts(" [MINIMIZED]");
        if (gui_state.focused_window->maximized) serial_puts(" [MAXIMIZED]");
    } else {
        serial_puts("None");
    }
    serial_puts("\n");
    
    serial_puts("Focused widget: ");
    if (gui_state.focus_widget && gui_state.focus_widget->text) {
        serial_puts(gui_state.focus_widget->text);
    } else {
        serial_puts("None");
    }
    serial_puts("\n");
    
    Window* window = gui_state.first_window;
    while (window) {
        if (IS_VALID_WINDOW_PTR(window)) {
            serial_puts("  [");
            serial_puts_num(window->z_index);
            serial_puts("] ");
            if (window->title) serial_puts(window->title);
            serial_puts(" (ID:");
            serial_puts_num(window->id);
            serial_puts(") ");
            serial_puts_num(window->width);
            serial_puts("x");
            serial_puts_num(window->height);
            serial_puts(" ");
            if (window->minimized) serial_puts("[MIN] ");
            if (window->maximized) serial_puts("[MAX] ");
            if (window->focused) serial_puts("[F] ");
            if (window->resizing) serial_puts("[RESIZE] ");
            serial_puts("\n");
        }
        window = window->next;
    }
    serial_puts("============================\n");
}