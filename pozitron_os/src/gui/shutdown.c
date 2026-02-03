#include "gui/gui.h"
#include "drivers/vesa.h"
#include "drivers/serial.h"
#include "drivers/power.h"
#include "kernel/memory.h"
#include "drivers/timer.h"
#include <stddef.h>

// Объявляем функции из taskbar.c
extern uint8_t start_menu_is_visible(void);
extern void start_menu_close(void);
extern void taskbar_remove_window(Window* window);

// Состояния системы выключения
#define SHUTDOWN_STATE_IDLE          0  // Ничего не происходит
#define SHUTDOWN_STATE_DIALOG        1  // Диалог открыт
#define SHUTDOWN_STATE_CANCELING     2  // Идет отмена
#define SHUTDOWN_STATE_CONFIRMING    3  // Подтверждено, идет выключение

// Глобальные переменные
static Window* original_windows[64];
static uint32_t original_window_count = 0;
static uint8_t shutdown_state = SHUTDOWN_STATE_IDLE;
static Window* shutdown_dialog = NULL;
static uint8_t darken_level = 0; // Уровень затемнения: 0-100
static uint8_t shutdown_immediate = 0; // Флаг для немедленного выключения

// ============ БЛОКИРОВКА УПРАВЛЕНИЯ ============

// Функция для скрытия всех окон
static void hide_all_windows(void) {
    original_window_count = 0;
    
    Window* window = gui_state.first_window;
    while (window && original_window_count < 64) {
        if (IS_VALID_WINDOW_PTR(window) && window->visible) {
            original_windows[original_window_count] = window;
            original_window_count++;
            window->visible = 0;
            window->needs_redraw = 1;
        }
        window = window->next;
    }
    
    if (start_menu_is_visible()) {
        start_menu_close();
    }
}

// Функция для восстановления всех окон
static void restore_all_windows(void) {
    for (uint32_t i = 0; i < original_window_count; i++) {
        Window* window = original_windows[i];
        if (IS_VALID_WINDOW_PTR(window)) {
            window->visible = 1;
            window->needs_redraw = 1;
        }
    }
    original_window_count = 0;
}

// ============ ЭФФЕКТ ЗАТЕМНЕНИЯ ============

void render_darken_effect(void) {
    if (shutdown_state == SHUTDOWN_STATE_IDLE || darken_level == 0) return;
    
    uint32_t screen_width = vesa_get_width();
    uint32_t screen_height = vesa_get_height();
    uint32_t* back_buffer = vesa_get_back_buffer();
    
    if (!back_buffer) return;
    
    // Для состояния CONFIRMING - плавный переход к черному
    if (shutdown_state == SHUTDOWN_STATE_CONFIRMING && darken_level > 100) {
        // Переход от темно-серого к черному (darken_level: 100 → 200)
        uint8_t brightness = 64 - ((darken_level - 100) * 64 / 100);
        if (brightness > 64) brightness = 64;
        
        uint32_t black_color = (brightness << 16) | (brightness << 8) | brightness;
        
        for (uint32_t i = 0; i < screen_width * screen_height; i++) {
            back_buffer[i] = black_color;
        }
    } else {
        // Для DIALOG, CANCELING и CONFIRMING (≤100) - серая шкала
        uint32_t light_gray = 0xC0C0C0;
        uint32_t dark_gray = 0x404040;
        
        uint8_t light_r = (light_gray >> 16) & 0xFF;
        uint8_t light_g = (light_gray >> 8) & 0xFF;
        uint8_t light_b = light_gray & 0xFF;
        
        uint8_t dark_r = (dark_gray >> 16) & 0xFF;
        uint8_t dark_g = (dark_gray >> 8) & 0xFF;
        uint8_t dark_b = dark_gray & 0xFF;
        
        // Ограничиваем darken_level 100 для серой шкалы
        uint8_t effective_level = darken_level;
        if (effective_level > 100) effective_level = 100;
        
        uint8_t final_r = light_r + ((dark_r - light_r) * effective_level) / 100;
        uint8_t final_g = light_g + ((dark_g - light_g) * effective_level) / 100;
        uint8_t final_b = light_b + ((dark_b - light_b) * effective_level) / 100;
        
        uint32_t darken_color = (final_r << 16) | (final_g << 8) | final_b;
        
        for (uint32_t i = 0; i < screen_width * screen_height; i++) {
            back_buffer[i] = darken_color;
        }
    }
}

void update_shutdown_animation(void) {
    if (shutdown_state == SHUTDOWN_STATE_IDLE) return;
    
    // Таймер для плавной анимации - обновляем раз в 50мс
    static uint32_t last_update = 0;
    uint32_t current_time = timer_get_ticks();
    
    if (current_time - last_update < 5) {
        return;
    }
    
    last_update = current_time;
    
    // ОБРАБОТКА РАЗНЫХ СОСТОЯНИЙ
    switch (shutdown_state) {
        case SHUTDOWN_STATE_DIALOG:
            // МЕДЛЕННОЕ увеличение затемнения до темно-серого
            if (darken_level < 100) {
                darken_level += 2;
                if (darken_level > 100) darken_level = 100;
            }
            break;
            
        case SHUTDOWN_STATE_CANCELING:
            // Быстрое осветление с проверкой на переполнение!
            if (darken_level > 0) {
                // ВАЖНО: Проверяем чтобы не было переполнения!
                if (darken_level >= 8) {
                    darken_level -= 8;
                } else {
                    // Если меньше 8, просто устанавливаем в 0
                    darken_level = 0;
                }
            }
            
            // КРИТИЧНО: Проверяем, достигли ли мы 0
            if (darken_level == 0) {
                serial_puts("[SHUTDOWN] Cancel complete, restoring system...\n");
                
                // Восстанавливаем окна
                restore_all_windows();
                
                // Сбрасываем состояние
                shutdown_state = SHUTDOWN_STATE_IDLE;
                shutdown_dialog = NULL;
                
                // Перерисовываем всё
                gui_force_redraw();
                vesa_mark_dirty_all();
                
                serial_puts("[SHUTDOWN] System restored to IDLE state\n");
            }
            break;
            
        case SHUTDOWN_STATE_CONFIRMING:
            // Плавное затемнение до черного (максимум 200)
            if (darken_level < 200) {
                darken_level += 4;
                if (darken_level > 200) darken_level = 200;
                
                // Когда достигли полного черного - выключаем
                if (darken_level >= 200 && !shutdown_immediate) {
                    shutdown_immediate = 1;
                    
                    serial_puts("[SHUTDOWN] Complete black - calling shutdown\n");
                    
                    // Маленькая задержка чтобы увидеть черный экран
                    for(int i = 0; i < 1000000; i++) asm volatile("nop");
                    
                    // Вызываем функцию выключения
                    shutdown_computer();
                }
            }
            break;
    }
}

// ============ CALLBACK-ФУНКЦИИ ============

static void shutdown_cancel_callback(Widget* button, void* userdata) {
    (void)button;
    
    serial_puts("[SHUTDOWN] Cancelling shutdown\n");
    
    // Проверяем текущее состояние
    if (shutdown_state != SHUTDOWN_STATE_DIALOG) {
        serial_puts("[SHUTDOWN] WARNING: Wrong state for cancel: ");
        serial_puts_num(shutdown_state);
        serial_puts("\n");
        return;
    }
    
    // Уничтожаем диалоговое окно
    Window* dialog = (Window*)userdata;
    if (dialog && IS_VALID_WINDOW_PTR(dialog)) {
        wm_destroy_window(dialog);
        shutdown_dialog = NULL;
    }
    
    // Переходим в состояние отмены
    shutdown_state = SHUTDOWN_STATE_CANCELING;
    
    serial_puts("[SHUTDOWN] Transition to CANCELING state (darken_level=");
    serial_puts_num(darken_level);
    serial_puts(")\n");
}

static void shutdown_confirm_callback(Widget* button, void* userdata) {
    (void)button;
    (void)userdata;
    
    serial_puts("[SHUTDOWN] Confirming shutdown\n");
    
    // Проверяем текущее состояние
    if (shutdown_state != SHUTDOWN_STATE_DIALOG) {
        serial_puts("[SHUTDOWN] WARNING: Wrong state for confirm: ");
        serial_puts_num(shutdown_state);
        serial_puts("\n");
        return;
    }
    
    // Уничтожаем диалоговое окно
    if (shutdown_dialog && IS_VALID_WINDOW_PTR(shutdown_dialog)) {
        wm_destroy_window(shutdown_dialog);
        shutdown_dialog = NULL;
    }
    
    // СКРЫВАЕМ КУРСОР ПЕРЕД ПЕРЕХОДОМ В РЕЖИМ ВЫКЛЮЧЕНИЯ
    vesa_hide_cursor();
    
    // Переходим в состояние подтверждения
    shutdown_state = SHUTDOWN_STATE_CONFIRMING;
    shutdown_immediate = 0;
    
    serial_puts("[SHUTDOWN] Transition to CONFIRMING state\n");
}

void shutdown_dialog_callback(Widget* button, void* userdata) {
    (void)button;
    (void)userdata;
    
    // ЖЕЛЕЗОБЕТОННАЯ ЗАЩИТА: проверяем можно ли создавать диалог
    if (shutdown_state != SHUTDOWN_STATE_IDLE) {
        serial_puts("[SHUTDOWN] WARNING: Cannot create dialog in state: ");
        serial_puts_num(shutdown_state);
        serial_puts(" (must be IDLE)\n");
        return;
    }
    
    serial_puts("[SHUTDOWN] Creating shutdown dialog\n");
    
    // Сбрасываем все переменные
    darken_level = 0;
    shutdown_immediate = 0;
    original_window_count = 0;
    
    // Скрываем все окна
    hide_all_windows();
    
    // Создаём диалоговое окно
    uint32_t screen_width = vesa_get_width();
    uint32_t screen_height = vesa_get_height();
    
    uint32_t dialog_width = 400;
    uint32_t dialog_height = 150;
    uint32_t dialog_x = (screen_width - dialog_width) / 2;
    uint32_t dialog_y = (screen_height - dialog_height) / 2 - 50;
    
    shutdown_dialog = wm_create_window("Shutdown Computer",
                                      dialog_x, dialog_y,
                                      dialog_width, dialog_height,
                                      WINDOW_MOVABLE | WINDOW_HAS_TITLE);
    
    if (!shutdown_dialog) {
        serial_puts("[SHUTDOWN] ERROR: Failed to create dialog\n");
        restore_all_windows();
        shutdown_state = SHUTDOWN_STATE_IDLE;
        return;
    }
    
    // Настройки окна
    shutdown_dialog->closable = 0;
    shutdown_dialog->minimizable = 0;
    shutdown_dialog->maximizable = 0;
    shutdown_dialog->in_taskbar = 0;
    shutdown_dialog->has_titlebar = 1;
    shutdown_dialog->title_height = 25;
    shutdown_dialog->visible = 1;
    shutdown_dialog->focused = 1;
    shutdown_dialog->needs_redraw = 1;
    
    // Текст вопроса в ДВЕ строки
    wg_create_label(shutdown_dialog, "Are you sure you want to", 80, 40);
    wg_create_label(shutdown_dialog, "shutdown the computer?", 90, 60);
    
    // Кнопки
    Widget* btn_yes = wg_create_button_ex(shutdown_dialog, "Yes", 100, 95, 80, 30,
                                         shutdown_confirm_callback, NULL);
    
    Widget* btn_no = wg_create_button_ex(shutdown_dialog, "No", 220, 95, 80, 30,
                                        shutdown_cancel_callback, shutdown_dialog);
    
    if (!btn_yes || !btn_no) {
        serial_puts("[SHUTDOWN] ERROR: Failed to create buttons\n");
        wm_destroy_window(shutdown_dialog);
        shutdown_dialog = NULL;
        restore_all_windows();
        shutdown_state = SHUTDOWN_STATE_IDLE;
        return;
    }
    
    // Фокусируем окно и убираем из таскбара
    wm_focus_window(shutdown_dialog);
    taskbar_remove_window(shutdown_dialog);
    
    // Переходим в состояние DIALOG
    shutdown_state = SHUTDOWN_STATE_DIALOG;
    
    serial_puts("[SHUTDOWN] Dialog created, state=DIALOG\n");
}

// ============ ФУНКЦИИ ДЛЯ ВНЕШНЕГО ДОСТУПА ============

uint8_t is_shutdown_mode_active(void) {
    return shutdown_state != SHUTDOWN_STATE_IDLE;
}

Window* get_shutdown_dialog(void) {
    if (shutdown_state == SHUTDOWN_STATE_DIALOG) {
        return shutdown_dialog;
    }
    return NULL;
}

// Функция для принудительного сброса состояния (на всякий случай)
void force_reset_shutdown_state(void) {
    if (shutdown_state != SHUTDOWN_STATE_IDLE) {
        serial_puts("[SHUTDOWN] FORCE resetting state from ");
        serial_puts_num(shutdown_state);
        serial_puts(" to IDLE\n");
        
        if (shutdown_dialog && IS_VALID_WINDOW_PTR(shutdown_dialog)) {
            wm_destroy_window(shutdown_dialog);
        }
        
        restore_all_windows();
        shutdown_state = SHUTDOWN_STATE_IDLE;
        shutdown_dialog = NULL;
        darken_level = 0;
        shutdown_immediate = 0;
        
        gui_force_redraw();
        vesa_mark_dirty_all();
    }
}