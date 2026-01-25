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

// ============ API WINDOW MANAGER ============
Window* wm_create_window(const char* title, uint32_t x, uint32_t y, 
                        uint32_t width, uint32_t height, uint8_t flags) {
    if (!gui_state.initialized) return NULL;
    if (gui_state.window_count >= 64) return NULL;
    if (width == 0 || height == 0) return NULL;
    
    uint32_t screen_width = gui_state.screen_width;
    uint32_t screen_height = gui_state.screen_height;
    
    if (x > screen_width - 100) x = screen_width - 100;
    if (y > screen_height - TASKBAR_HEIGHT - 100) y = screen_height - TASKBAR_HEIGHT - 100;
    if (width > screen_width - 50) width = screen_width - 50;
    if (height > screen_height - TASKBAR_HEIGHT - 50) height = screen_height - TASKBAR_HEIGHT - 50;
    
    Window* window = (Window*)kmalloc(sizeof(Window));
    if (!window) return NULL;
    
    window->id = gui_state.next_window_id++;
    window->x = x;
    window->y = y;
    window->width = width;
    window->height = height;
    window->title_height = 25;
    window->visible = 1;
    window->has_titlebar = (flags & WINDOW_HAS_TITLE) ? 1 : 0;
    window->z_index = gui_state.window_count;
    window->focused = 0;
    window->dragging = 0;
    window->drag_offset_x = 0;
    window->drag_offset_y = 0;
    window->minimized = 0;
    window->maximized = 0;                 // НОВОЕ: не максимизировано
    window->first_widget = NULL;
    window->last_widget = NULL;
    window->next = NULL;
    window->prev = NULL;
    window->on_close = NULL;
    window->on_focus = NULL;
    window->on_minimize = NULL;
    window->on_maximize = NULL;            // НОВОЕ: callback максимизации
    window->on_restore = NULL;
    window->closable = (flags & WINDOW_CLOSABLE) ? 1 : 0;
    window->movable = (flags & WINDOW_MOVABLE) ? 1 : 0;
    window->resizable = (flags & WINDOW_RESIZABLE) ? 1 : 0;
    window->minimizable = (flags & WINDOW_MINIMIZABLE) ? 1 : 0;
    window->maximizable = (flags & WINDOW_MAXIMIZABLE) ? 1 : 0; // НОВОЕ: поддержка максимизации
    window->needs_redraw = 1;
    window->in_taskbar = 1;
    
    // Сохраняем оригинальные размеры
    window->orig_x = x;
    window->orig_y = y;
    window->orig_width = width;
    window->orig_height = height;
    
    // Инициализируем нормальные размеры (для максимизации)
    window->normal_x = x;
    window->normal_y = y;
    window->normal_width = width;
    window->normal_height = height;
    
    // Сохраняем оригинальные свойства
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
    
    window->dragging = 0;
    
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
    
    // Если окно минимизировано, сначала восстанавливаем
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

void wm_move_window(Window* window, uint32_t x, uint32_t y) {
    // Нельзя перемещать максимизированные окна
    if (!IS_VALID_WINDOW_PTR(window) || !window->movable || window->minimized || window->maximized) return;
    
    uint32_t screen_width = gui_state.screen_width;
    uint32_t screen_height = gui_state.screen_height;
    
    // Для нормального окна - стандартные ограничения
    if (!window->maximized) {
        if (x > screen_width - window->width) {
            x = screen_width - window->width;
        }
        if (y > screen_height - TASKBAR_HEIGHT - window->height) {
            y = screen_height - TASKBAR_HEIGHT - window->height;
        }
    }
    
    // Обновляем позиции виджетов
    Widget* widget = window->first_widget;
    while (widget) {
        widget->x = x + (widget->x - window->x);
        widget->y = y + (widget->y - window->y);
        widget = widget->next;
    }
    
    window->x = x;
    window->y = y;
    
    // Если окно не максимизировано, обновляем нормальные координаты
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

// ============ ФУНКЦИЯ МАКСИМИЗАЦИИ ============
void wm_maximize_window(Window* window) {
    if (!IS_VALID_WINDOW_PTR(window) || window->maximized || !window->maximizable) return;
    
    // Сохраняем текущие (нормальные) размеры окна
    window->normal_x = window->x;
    window->normal_y = window->y;
    window->normal_width = window->width;
    window->normal_height = window->height;
    
    // Сохраняем оригинальные свойства
    window->orig_movable = window->movable;
    window->orig_resizable = window->resizable;
    
    // Разворачиваем окно на весь экран (минус панель задач)
    window->x = 0;
    window->y = 0;
    window->width = gui_state.screen_width;
    window->height = gui_state.screen_height - TASKBAR_HEIGHT;
    
    // Обновляем состояние
    window->maximized = 1;
    window->movable = 0;      // Максимизированное окно нельзя перемещать
    window->resizable = 0;    // Максимизированное окно нельзя изменять размер
    
    // Обновляем виджеты
    Widget* widget = window->first_widget;
    while (widget) {
        // Сохраняем относительные позиции (в процентах от размеров окна)
        // Для простоты - оставляем абсолютные координаты, но можно добавить логику масштабирования
        widget->needs_redraw = 1;
        widget = widget->next;
    }
    
    window->needs_redraw = 1;
    
    // Вызываем callback, если он установлен
    if (window->on_maximize) {
        window->on_maximize(window);
    }
    
    serial_puts("[WM] Window maximized: ");
    if (window->title) serial_puts(window->title);
    serial_puts(" (");
    serial_puts_num(window->width);
    serial_puts("x");
    serial_puts_num(window->height);
    serial_puts(")\n");
}

// ============ ФУНКЦИЯ СВОРАЧИВАНИЯ ============
void wm_minimize_window(Window* window) {
    if (!IS_VALID_WINDOW_PTR(window) || !window->minimizable || window->minimized) return;
    
    uint8_t was_maximized = window->maximized;

    // Если окно максимизировано, сначала сохраняем максимизированные размеры
    if (was_maximized) {
        window->orig_x = window->normal_x;
        window->orig_y = window->normal_y;
        window->orig_width = window->normal_width;
        window->orig_height = window->normal_height;
        // Не сбрасываем maximized флаг!
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
        
        // Находим другое окно для фокуса
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
    
    if (window->on_minimize) {
        window->on_minimize(window);
    }
    
    if (window->in_taskbar) {
        taskbar_update_window(window);
    }
    
    vesa_mark_dirty(window->x - 5, window->y - 5, 
                   window->width + 10, window->height + 10);
}

// ============ УНИВЕРСАЛЬНАЯ ФУНКЦИЯ ВОССТАНОВЛЕНИЯ ============
void wm_restore_window(Window* window) {
    if (!IS_VALID_WINDOW_PTR(window)) return;
    
    // Если окно минимизировано
    if (window->minimized) {
        window->visible = 1;
        window->minimized = 0;
        
        // Восстанавливаем позицию и размер из orig_* полей
        window->x = window->orig_x;
        window->y = window->orig_y;
        window->width = window->orig_width;
        window->height = window->orig_height;
        
        // Если окно было максимизировано до минимизации, восстанавливаем максимизацию
        // Для этого нужно сохранять was_maximized отдельно или проверять нормальные размеры
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
        
        // Обновляем виджеты
        Widget* widget = window->first_widget;
        while (widget) {
            widget->x = window->x + (widget->x - window->orig_x);
            widget->y = window->y + (widget->y - window->orig_y);
            widget->needs_redraw = 1;
            widget = widget->next;
        }
        
        if (window->on_restore) {
            window->on_restore(window);
        }
        
        serial_puts("[WM] Window restored from minimized: ");
        if (window->title) serial_puts(window->title);
        serial_puts("\n");
    }
    // Если окно максимизировано
    else if (window->maximized) {
        // Восстанавливаем нормальные размеры
        window->x = window->normal_x;
        window->y = window->normal_y;
        window->width = window->normal_width;
        window->height = window->normal_height;
        
        // Восстанавливаем свойства
        window->maximized = 0;
        window->movable = window->orig_movable;
        window->resizable = window->orig_resizable;
        
        // Обновляем виджеты - возвращаем их на прежние позиции
        Widget* widget = window->first_widget;
        while (widget) {
            // Виджеты остаются на своих относительных позициях
            widget->needs_redraw = 1;
            widget = widget->next;
        }
        
        if (window->on_restore) {
            window->on_restore(window);
        }
        
        serial_puts("[WM] Window restored from maximized: ");
        if (window->title) serial_puts(window->title);
        serial_puts(" (");
        serial_puts_num(window->width);
        serial_puts("x");
        serial_puts_num(window->height);
        serial_puts(")\n");
    }
    // Если окно и не минимизировано, и не максимизировано - ничего не делаем
    else {
        return;
    }
    
    // Обновляем таскбар
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
            serial_puts("\n");
        }
        window = window->next;
    }
    serial_puts("============================\n");
}