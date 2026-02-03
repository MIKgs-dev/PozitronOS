#include "gui/gui.h"
#include "drivers/serial.h"
#include "drivers/vesa.h"
#include "kernel/memory.h"
#include "gui/shutdown.h"

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

// ============ ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ ============
static uint8_t point_in_rect_static(uint32_t px, uint32_t py, uint32_t x, uint32_t y, 
                                   uint32_t w, uint32_t h) {
    return (px >= x && px < x + w && py >= y && py < y + h);
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

// ============ ОБРАБОТКА СОБЫТИЙ ============
void gui_handle_event(event_t* event) {
    if (!gui_state.initialized || !event) return;

    // Получаем координаты мыши сразу
    uint32_t mx = event->data1;
    uint32_t my = event->data2 & 0xFFFF;
    uint32_t button = (event->data2 >> 16) & 0xFF;
    
    // Проверяем режим выключения
    if (is_shutdown_mode_active()) {
        Window* shutdown_dialog = get_shutdown_dialog();
        
        if (shutdown_dialog && IS_VALID_WINDOW_PTR(shutdown_dialog)) {
            uint8_t is_inside_dialog = point_in_rect_static(mx, my, 
                                                           shutdown_dialog->x, shutdown_dialog->y,
                                                           shutdown_dialog->width, shutdown_dialog->height);
            
            if (is_inside_dialog) {
                uint32_t win_x = mx - shutdown_dialog->x;
                uint32_t win_y = my - shutdown_dialog->y;
                
                Widget* widget = shutdown_dialog->first_widget;
                while (widget) {
                    uint32_t widget_rel_x = widget->x - shutdown_dialog->x;
                    uint32_t widget_rel_y = widget->y - shutdown_dialog->y;
                    
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
    
    // Обработка клавиатуры
    if (event->type == EVENT_KEY_PRESS) {
        switch (event->data1) {
            case 0x3B: // F1
                gui_state.debug_mode = !gui_state.debug_mode;
                vesa_mark_dirty(0, 0, gui_state.screen_width, 20);
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
            case 0x57: // F11 для максимизации
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
        }
        return;
    }

    // Обработка перетаскивания окон
    if (gui_state.dragging_window) {
        Window* window = gui_state.dragging_window;
        
        if (!IS_VALID_WINDOW_PTR(window)) {
            gui_state.dragging_window = NULL;
            return;
        }
        
        if (event->type == EVENT_MOUSE_MOVE) {
            // Нельзя перетаскивать максимизированные окна
            if (window->maximized) {
                gui_state.dragging_window = NULL;
                return;
            }
            
            uint32_t new_x = mx - window->drag_offset_x;
            uint32_t new_y = my - window->drag_offset_y;
            
            // Ограничиваем перемещение в пределах экрана
            if (new_x > gui_state.screen_width - window->width) {
                new_x = gui_state.screen_width - window->width;
            }
            if (new_y > gui_state.screen_height - TASKBAR_HEIGHT - window->height) {
                new_y = gui_state.screen_height - TASKBAR_HEIGHT - window->height;
            }
            if (new_x < 0) new_x = 0;
            if (new_y < 0) new_y = 0;
            
            // Перемещаем окно
            wm_move_window(window, new_x, new_y);
            window->needs_redraw = 1;
        }
        else if (event->type == EVENT_MOUSE_RELEASE && button == 0) {
            gui_state.dragging_window = NULL;
        }
        
        return;
    }

    // Обработка событий панели задач (только если клик в области панели)
    if (my >= gui_state.screen_height - TASKBAR_HEIGHT) {
        taskbar_handle_event(event);
        return;
    }
    
    // Проверяем, открыто ли меню Пуск
    Window* start_win = start_menu_get_window();
    if (start_menu_is_visible() && start_win && IS_VALID_WINDOW_PTR(start_win)) {
        // Проверяем, попал ли курсор в меню Пуск
        uint8_t in_start_menu = point_in_rect_static(mx, my, start_win->x, start_win->y, 
                                                   start_win->width, start_win->height);
        
        if (in_start_menu) {
            if (event->type == EVENT_MOUSE_CLICK && button == 0) {
                // Фокусируем окно меню Пуск
                wm_focus_window(start_win);
                
                // Обрабатываем клики по виджетам меню Пуск
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
                        return; // Событие обработано
                    }
                    widget = widget->next;
                }
            }
            else if (event->type == EVENT_MOUSE_MOVE) {
                // Фокусируем окно меню Пуск при наведении
                wm_focus_window(start_win);
                
                // Обработка hover для виджетов меню Пуск
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
                // Сбрасываем состояние нажатых кнопок в меню Пуск
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
        // Если курсор вне меню Пуск - обрабатываем как обычные события
    }

    // Обработка кликов мыши по обычным окнам
    if (event->type == EVENT_MOUSE_CLICK && button == 0) {
        Window* window = wm_find_window_at(mx, my);
        
        if (window && IS_VALID_WINDOW_PTR(window)) {
            wm_focus_window(window);
            
            // Проверяем клики по виджетам
            uint8_t widget_was_clicked = 0;
            Widget* widget = window->first_widget;
            
            while (widget) {
                if (widget->visible && widget->enabled &&
                    point_in_rect_static(mx, my, widget->x, widget->y, 
                                       widget->width, widget->height)) {
                    
                    widget_was_clicked = 1;
                    widget->state = STATE_PRESSED;
                    widget->needs_redraw = 1;
                    window->needs_redraw = 1;
                    
                    // Специальная обработка виджетов
                    if (widget->type == WIDGET_CHECKBOX && widget->data) {
                        uint8_t* checked = (uint8_t*)widget->data;
                        *checked = !(*checked);
                    }
                    else if (widget->type == WIDGET_SLIDER && widget->data) {
                        // Начинаем перетаскивание слайдера
                        widget->dragging = 1;
                        
                        uint32_t* data = (uint32_t*)widget->data;
                        uint32_t min = data[0];
                        uint32_t max = data[1];
                        
                        if (max > min) {
                            uint32_t relative_x = mx - widget->x;
                            uint32_t value = min + (relative_x * (max - min)) / widget->width;
                            if (value < min) value = min;
                            if (value > max) value = max;
                            data[2] = value;
                        }
                    }
                    
                    if (widget->on_click) {
                        widget->on_click(widget, widget->userdata);
                    }
                    
                    break;
                }
                widget = widget->next;
            }
            
            // Если не кликнули по виджету, проверяем заголовок окна
            if (!widget_was_clicked && window->has_titlebar) {
                uint32_t title_y = window->y;
                uint32_t title_h = window->title_height;
                
                // Проверяем, попал ли клик в заголовок
                if (my >= title_y && my < title_y + title_h) {
                    // Кнопка закрытия
                    if (window->closable) {
                        uint32_t close_x = window->maximized ? 
                                          gui_state.screen_width - 25 : 
                                          window->x + window->width - 25;
                        uint32_t close_y = window->y + 5;
                        
                        if (mx >= close_x && mx < close_x + 15 &&
                            my >= close_y && my < close_y + 15) {
                            wm_close_window(window);
                            return;
                        }
                    }
                    
                    // Кнопка сворачивания
                    if (window->minimizable) {
                        uint32_t min_x = window->maximized ? 
                                        gui_state.screen_width - (window->closable ? 45 : 25) : 
                                        window->x + window->width - (window->closable ? 45 : 25);
                        uint32_t min_y = window->y + 5;
                        
                        if (mx >= min_x && mx < min_x + 15 &&
                            my >= min_y && my < min_y + 15) {
                            wm_minimize_window(window);
                            return;
                        }
                    }
                    
                    // Кнопка максимизации
                    if (window->maximizable) {
                        uint32_t max_x = window->maximized ? 
                                        gui_state.screen_width - (window->closable ? 65 : 45) : 
                                        window->x + window->width - (window->closable ? 65 : 45);
                        uint32_t max_y = window->y + 5;
                        
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
                    
                    // Начало перетаскивания окна
                    if (window->movable && !window->minimized && !window->maximized) {
                        window->drag_offset_x = mx - window->x;
                        window->drag_offset_y = my - window->y;
                        gui_state.dragging_window = window;
                        window->needs_redraw = 1;
                    }
                }
            }
        } else {
            // Клик вне окон - снимаем фокус
            if (gui_state.focused_window && 
                IS_VALID_WINDOW_PTR(gui_state.focused_window)) {
                gui_state.focused_window->focused = 0;
                gui_state.focused_window->needs_redraw = 1;
            }
            gui_state.focused_window = NULL;
        }
    }
    // Обработка движения мыши
    else if (event->type == EVENT_MOUSE_MOVE) {
        // Проверяем перетаскивание слайдера
        Window* window = gui_state.first_window;
        while (window) {
            if (IS_VALID_WINDOW_PTR(window)) {
                Widget* widget = window->first_widget;
                while (widget) {
                    if (widget->dragging && widget->type == WIDGET_SLIDER && widget->data) {
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
                        
                        if (widget->on_click) {
                            widget->on_click(widget, widget->userdata);
                        }
                        return;
                    }
                    widget = widget->next;
                }
            }
            window = window->next;
        }
        
        // Обработка hover для обычных окон
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
                            if (widget->state != STATE_HOVER) {
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
    // Обработка отпускания кнопки мыши
    else if (event->type == EVENT_MOUSE_RELEASE && button == 0) {
        // Сбрасываем dragging у всех виджетов
        Window* window = gui_state.first_window;
        while (window) {
            if (IS_VALID_WINDOW_PTR(window)) {
                Widget* widget = window->first_widget;
                while (widget) {
                    if (widget->dragging) {
                        widget->dragging = 0;
                        widget->state = STATE_NORMAL;
                        widget->needs_redraw = 1;
                        window->needs_redraw = 1;
                    }
                    widget = widget->next;
                }
            }
            window = window->next;
        }
        
        // Сбрасываем состояние нажатых виджетов во всех окнах
        window = gui_state.first_window;
        while (window) {
            if (IS_VALID_WINDOW_PTR(window)) {
                Widget* widget = window->first_widget;
                while (widget) {
                    if (widget->state == STATE_PRESSED) {
                        if (point_in_rect_static(mx, my, widget->x, widget->y,
                                                widget->width, widget->height)) {
                            widget->state = STATE_HOVER;
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
}

// ============ РЕНДЕРИНГ ============
void gui_render(void) {
    if (!gui_state.initialized) return;

    // Если режим выключения активен
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
            
            // Рамка
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
                        
                        if (text_x < widget->x + 4) text_x = widget->x + 4;
                        if (text_y < widget->y + 2) text_y = widget->y + 2;
                        
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
    
    // Проверяем, есть ли видимые окна
    uint8_t has_visible_windows = 0;
    Window* window = gui_state.first_window;
    while (window) {
        if (IS_VALID_WINDOW_PTR(window) && window->visible && !window->minimized) {
            has_visible_windows = 1;
            break;
        }
        window = window->next;
    }
    
    // Рендерим окна, если есть видимые
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
        
        // Сортировка по z-index (рендерим от нижних к верхним)
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
            
            // Рисуем тень (только для не-максимизированных окон)
            if (window->has_titlebar && !window->maximized) {
                vesa_draw_rect(window->x + 2, window->y + 2, 
                             window->width, window->height, 0x888888);
            }
            
            // Фон окна
            vesa_draw_rect(window->x, window->y, window->width, window->height, WINDOW_BG_COLOR);
            
            // Заголовок окна
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
                
                // Кнопка максимизации/восстановления
                if (window->maximizable) {
                    uint32_t max_x;
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
                
                // Кнопка сворачивания
                if (window->minimizable) {
                    uint32_t min_x;
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
                
                // Кнопка закрытия
                if (window->closable) {
                    uint32_t close_x;
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
            
            // Рамка окна
            uint32_t border_color = WINDOW_BORDER_COLOR;
            if (!window->maximized) {
                vesa_draw_rect(window->x, window->y, window->width, 1, border_color);
                vesa_draw_rect(window->x, window->y + window->height - 1, 
                             window->width, 1, border_color);
                vesa_draw_rect(window->x, window->y, 1, window->height, border_color);
                vesa_draw_rect(window->x + window->width - 1, window->y, 
                             1, window->height, border_color);
            } else {
                vesa_draw_rect(window->x, window->y + window->height - 1, 
                             window->width, 1, border_color);
            }
            
            // Рендеринг виджетов окна
            Widget* widget = window->first_widget;
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
                }
                else if (widget->type == WIDGET_LABEL) {
                    if (widget->text) {
                        vesa_draw_text(widget->x, widget->y, widget->text, 0x000000, WINDOW_BG_COLOR);
                    }
                }
                else if (widget->type == WIDGET_CHECKBOX) {
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
                }
                else if (widget->type == WIDGET_SLIDER) {
                    uint32_t* data = (uint32_t*)widget->data;
                    if (!data) {
                        widget = widget->next;
                        continue;
                    }
                    
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
                }
                else if (widget->type == WIDGET_PROGRESSBAR) {
                    uint32_t value = 0;
                    if (widget->data) {
                        value = *((uint32_t*)widget->data);
                    }
                    
                    vesa_draw_rect(widget->x, widget->y, widget->width, widget->height, PROGRESSBAR_BG_COLOR);
                    vesa_draw_rect(widget->x, widget->y, widget->width, 1, 0x808080);
                    vesa_draw_rect(widget->x, widget->y + widget->height - 1, widget->width, 1, 0x808080);
                    vesa_draw_rect(widget->x, widget->y, 1, widget->height, 0x808080);
                    vesa_draw_rect(widget->x + widget->width - 1, widget->y, 1, widget->height, 0x808080);
                    
                    uint32_t fill_width = (value * (widget->width - 2)) / 100;
                    if (fill_width > 0) {
                        vesa_draw_rect(widget->x + 1, widget->y + 1, fill_width, widget->height - 2, PROGRESSBAR_FILL_COLOR);
                    }
                    
                    if (widget->height >= 16) {
                        char percent[8];
                        percent[0] = '0' + (value / 100);
                        percent[1] = '0' + ((value % 100) / 10);
                        percent[2] = '0' + (value % 10);
                        percent[3] = '%';
                        percent[4] = '\0';
                        
                        uint32_t text_x = widget->x + (widget->width - 4*8) / 2;
                        uint32_t text_y = widget->y + (widget->height - 16) / 2;
                        vesa_draw_text(text_x, text_y, percent, 0x000000, PROGRESSBAR_BG_COLOR);
                    }
                }
                
                widget = widget->next;
            }
            
            vesa_mark_dirty(window->x - 5, window->y - 5,
                           window->width + 10, window->height + 10);
        }
    }
    
    // Всегда рендерим панель задач
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

// ============ УТИЛИТЫ ============
uint8_t point_in_rect(uint32_t px, uint32_t py, uint32_t x, uint32_t y, 
                     uint32_t w, uint32_t h) {
    return point_in_rect_static(px, py, x, y, w, h);
}

uint32_t get_screen_width(void) { 
    return gui_state.screen_width; 
}

uint32_t get_screen_height(void) { 
    return gui_state.screen_height; 
}