#include "drivers/serial.h"
#include "drivers/vga.h"
#include "drivers/pic.h"
#include "drivers/timer.h"
#include "drivers/keyboard.h"
#include "drivers/mouse.h"
#include "drivers/cmos.h"
#include "core/gdt.h"
#include "core/idt.h"
#include "core/isr.h"
#include "core/event.h"
#include "drivers/vesa.h"
#include "kernel/memory.h"
#include "gui/gui.h"
#include <stddef.h>

static uint8_t system_running = 1;

// ============ CALLBACK-ФУНКЦИИ ДЛЯ НОВЫХ ЭЛЕМЕНТОВ ============
static void update_progress_callback(Widget* button, void* userdata) {
    if (!button || !userdata) return;
    
    Widget* progressbar = (Widget*)userdata;
    static uint32_t progress_value = 0;
    
    progress_value += 10;
    if (progress_value > 100) progress_value = 0;
    
    wg_set_progressbar_value(progressbar, progress_value);
    
    serial_puts("[GUI] Progress updated: ");
    serial_puts_num(progress_value);
    serial_puts("%\n");
}

static void slider_changed_callback(Widget* slider, void* userdata) {
    if (!slider) return;
    
    uint32_t value = wg_get_slider_value(slider);
    
    serial_puts("[GUI] Slider changed: ");
    serial_puts_num(value);
    serial_puts("\n");
}

static void checkbox_toggled_callback(Widget* checkbox, void* userdata) {
    if (!checkbox) return;
    
    uint8_t checked = wg_get_checkbox_state(checkbox);
    
    serial_puts("[GUI] Checkbox ");
    serial_puts(checked ? "checked" : "unchecked");
    if (checkbox->text) {
        serial_puts(": ");
        serial_puts(checkbox->text);
    }
    serial_puts("\n");
}

// ============ ДЕМО-ИНТЕРФЕЙС С НОВЫМИ ЭЛЕМЕНТАМИ ============
static void create_demo_ui(void) {
    serial_puts("\n=== CREATING DEMO UI ===\n");
    
    // Главное демо-окно
    Window* main_win = wm_create_window("PozitronOS GUI Demo", 200, 100, 450, 320, 
                                       WINDOW_CLOSABLE | WINDOW_MOVABLE | 
                                       WINDOW_HAS_TITLE | WINDOW_MINIMIZABLE |
                                       WINDOW_MAXIMIZABLE);
    
    if (!main_win) {
        serial_puts("[DEMO] ERROR: Failed to create main window\n");
        return;
    }
    
    // Информация о новых функциях
    wg_create_label(main_win, "PozitronOS GUI - New Controls Demo", 20, 40);
    wg_create_label(main_win, "New GUI Controls Added:", 20, 70);
    wg_create_label(main_win, "1. Checkboxes (with callbacks)", 30, 95);
    wg_create_label(main_win, "2. Sliders (draggable)", 30, 115);
    wg_create_label(main_win, "3. Progress bars (with % display)", 30, 135);
    wg_create_label(main_win, "4. Date/Time menu (click clock)", 30, 155);
    wg_create_label(main_win, "Check serial output for events!", 20, 180);
    
    // Окно с новыми элементами управления
    Window* controls_win = wm_create_window("GUI Controls Demo", 100, 150, 400, 300,
                                           WINDOW_CLOSABLE | WINDOW_MOVABLE | 
                                           WINDOW_HAS_TITLE | WINDOW_MINIMIZABLE);
    
    if (controls_win) {
        wg_create_label(controls_win, "New GUI Controls:", 20, 40);
        wg_create_label(controls_win, "Checkboxes:", 20, 70);
        
        // Чекбоксы с callback
        Widget* cb1 = wg_create_checkbox(controls_win, "Enable feature A", 40, 90, 1);
        if (cb1) {
            wg_set_callback_ex(cb1, checkbox_toggled_callback, NULL);
        }
        
        Widget* cb2 = wg_create_checkbox(controls_win, "Enable feature B", 40, 115, 0);
        if (cb2) {
            wg_set_callback_ex(cb2, checkbox_toggled_callback, NULL);
        }
        
        Widget* cb3 = wg_create_checkbox(controls_win, "Auto-save", 40, 140, 1);
        if (cb3) {
            wg_set_callback_ex(cb3, checkbox_toggled_callback, NULL);
        }
        
        // Слайдеры с callback
        wg_create_label(controls_win, "Volume:", 20, 170);
        Widget* slider1 = wg_create_slider(controls_win, 40, 190, 200, 0, 100, 75);
        if (slider1) {
            wg_set_callback_ex(slider1, slider_changed_callback, NULL);
        }
        
        wg_create_label(controls_win, "Brightness:", 20, 215);
        Widget* slider2 = wg_create_slider(controls_win, 40, 235, 200, 0, 100, 50);
        if (slider2) {
            wg_set_callback_ex(slider2, slider_changed_callback, NULL);
        }
        
        // Прогресс-бар
        wg_create_label(controls_win, "System load:", 20, 260);
        Widget* progress = wg_create_progressbar(controls_win, 40, 280, 200, 20, 45);
        
        // Кнопка для обновления прогресса
        Widget* update_btn = wg_create_button(controls_win, "Update Progress", 250, 280, 120, 20);
        if (update_btn) {
            wg_set_callback_ex(update_btn, update_progress_callback, progress);
        }
    }
    
    // Дополнительное демо-окно
    Window* demo2 = wm_create_window("Demo Window 2", 300, 200, 350, 250,
                                    WINDOW_CLOSABLE | WINDOW_MOVABLE | 
                                    WINDOW_HAS_TITLE | WINDOW_MINIMIZABLE |
                                    WINDOW_MAXIMIZABLE);
    
    if (demo2) {
        wg_create_label(demo2, "More controls testing:", 20, 50);
        
        // Группа чекбоксов
        wg_create_label(demo2, "Options:", 20, 80);
        wg_create_checkbox(demo2, "Show toolbar", 40, 100, 1);
        wg_create_checkbox(demo2, "Enable sounds", 40, 120, 1);
        wg_create_checkbox(demo2, "Auto-update", 40, 140, 0);
        
        // Слайдер
        wg_create_label(demo2, "Transparency:", 20, 170);
        wg_create_slider(demo2, 40, 190, 150, 0, 100, 30);
        
        // Прогресс-бар
        wg_create_label(demo2, "Download:", 20, 220);
        Widget* progress2 = wg_create_progressbar(demo2, 40, 240, 150, 16, 75);
        
        // Кнопка для прогресса
        Widget* btn = wg_create_button(demo2, "Simulate", 200, 240, 80, 16);
        if (btn) {
            wg_set_callback_ex(btn, update_progress_callback, progress2);
        }
    }
}

// ============ ОБРАБОТЧИК КЛАВИАТУРЫ (ОБНОВЛЕН ДЛЯ МАКСИМИЗАЦИИ) ============
static void handle_keyboard_events(event_t* event) {
    if (!event) return;
    
    if (event->type == EVENT_KEY_PRESS) {
        switch (event->data1) {
            case 0x3B: // F1 - создать тестовое окно с максимизацией
                {
                    static uint32_t counter = 0;
                    char title[32];
                    char* ptr = title;
                    
                    *ptr++ = 'T'; *ptr++ = 'e'; *ptr++ = 's'; *ptr++ = 't'; *ptr++ = ' ';
                    
                    uint32_t n = ++counter;
                    if (n == 0) *ptr++ = '0';
                    while (n > 0 && ptr < title + 30) {
                        *ptr++ = '0' + (n % 10);
                        n /= 10;
                    }
                    *ptr = '\0';
                    
                    // Переворачиваем цифры
                    char* start = title + 5;
                    char* end = ptr - 1;
                    while (start < end) {
                        char temp = *start;
                        *start = *end;
                        *end = temp;
                        start++;
                        end--;
                    }
                    
                    // Создаем окно с поддержкой максимизации
                    Window* win = wm_create_window(title,
                                                 100 + (counter * 30) % 500,
                                                 80 + (counter * 20) % 300,
                                                 250, 180,
                                                 WINDOW_CLOSABLE | WINDOW_MOVABLE | 
                                                 WINDOW_HAS_TITLE | WINDOW_MINIMIZABLE |
                                                 WINDOW_MAXIMIZABLE); // Добавляем максимизацию
                    if (win) {
                        wg_create_label(win, "Created by F1", 30, 50);
                        wg_create_label(win, title, 30, 80);
                        wg_create_label(win, "Supports maximization!", 30, 110);
                        wg_create_button_ex(win, "Maximize", 30, 140, 80, 25,
                                          (void (*)(Widget*, void*))wm_maximize_window, win);
                    }
                }
                break;
                
            case 0x3C: // F2 - информация
                wm_dump_info();
                break;
                
            case 0x01: // ESC - закрыть фокусное окно
                if (gui_state.focused_window) {
                    wm_close_window(gui_state.focused_window);
                }
                break;
                
            case 0x57: // F11 - переключение максимизации (стандартная клавиша в Windows)
                if (gui_state.focused_window && 
                    IS_VALID_WINDOW_PTR(gui_state.focused_window) &&
                    gui_state.focused_window->maximizable) {
                    if (gui_state.focused_window->maximized) {
                        wm_restore_window(gui_state.focused_window);
                        serial_puts("[KEY] F11: Window restored\n");
                    } else {
                        wm_maximize_window(gui_state.focused_window);
                        serial_puts("[KEY] F11: Window maximized\n");
                    }
                } else if (gui_state.focused_window) {
                    serial_puts("[KEY] F11: Window not maximizable\n");
                }
                break;
                
            case 0x2A: // LSHIFT
            case 0x36: // RSHIFT
                break;
                
            case 0x5B: // Виндовс клавиша левая - альтернативный способ открыть меню
                start_menu_toggle();
                break;
            case 0x5C: // Виндовс клавиша правая - альтернативный способ открыть меню
                start_menu_toggle();
                break;
                
            default:
                // Игнорируем другие клавиши
                break;
        }
    }
}

// ============ ГЛАВНАЯ ФУНКЦИЯ ============
void kernel_main() {
    // Инициализация системы
    serial_init();
    vga_puts("PozitronOS - Maximization Demo\n");
    vga_puts("===============================\n");
    
    gdt_init();
    idt_init();
    pic_init();
    isr_init();
    asm volatile("sti");
    
    timer_init(100);
    keyboard_init();
    memory_init();
    cmos_init();
    
    // Графика
    if(!vesa_init()) {
        vga_puts("ERROR: Failed to initialize graphics\n");
        while(1);
    }
    
    uint32_t screen_width = vesa_get_width();
    uint32_t screen_height = vesa_get_height();
    
    vesa_enable_double_buffer();
    
    // Создаём кэшированный фон (градиентный)
    vesa_cache_background();
    vesa_init_dirty();
    
    // КРИТИЧЕСКОЕ: Помечаем ВЕСЬ экран как dirty
    vesa_mark_dirty_all();
    
    vesa_cursor_init();
    vesa_cursor_set_visible(1);
    
    mouse_init();
    event_init();
    
    // ВАЖНО: Сначала восстанавливаем фон
    if (vesa_is_background_cached()) {
        vesa_restore_background();
    }
    
    gui_init(screen_width, screen_height);
    taskbar_init();
    create_demo_ui();
    
    // Первый рендер
    gui_render();
    vesa_cursor_update();
    
    if (vesa_is_double_buffer_enabled()) {
        vesa_swap_buffers();
    }
    
    serial_puts("\n=== SYSTEM READY ===\n");
    serial_puts("NEW: Window Maximization Feature!\n");
    serial_puts("==============================\n");
    serial_puts("To Maximize a window:\n");
    serial_puts("  1. Click [□] button in titlebar\n");
    serial_puts("  2. Double-click window titlebar\n");
    serial_puts("  3. Press F11 when window focused\n");
    serial_puts("  4. Click Maximize button in window\n");
    serial_puts("\nTo Restore from maximized:\n");
    serial_puts("  1. Click [□] button again\n");
    serial_puts("  2. Press F11\n");
    serial_puts("  3. Click Restore button\n");
    serial_puts("\nOther Controls:\n");
    serial_puts("  - F1: Create maximizable test window\n");
    serial_puts("  - F2: Window manager info\n");
    serial_puts("  - ESC: Close focused window\n");
    serial_puts("  - A: Toggle start menu\n");
    serial_puts("=========================\n\n");
    
    // Главный цикл
    while(system_running) {
        asm volatile("hlt");
    
        event_t event;
        while (event_poll(&event)) {
            gui_handle_event(&event);
            handle_keyboard_events(&event);
        }
    
        // 1. Сначала скрываем курсор
        vesa_hide_cursor();
    
        // 2. Восстанавливаем фон для грязных областей
        if (vesa_is_background_cached()) {
            vesa_restore_background_dirty();
        }
    
        // 3. Рендерим GUI (окна, таскбар)
        gui_render();
    
        // 4. Показываем курсор ПОВЕРХ всего
        vesa_show_cursor();
        vesa_cursor_update();
    
        // 5. Копируем back buffer в front buffer
        if (vesa_is_double_buffer_enabled()) {
            vesa_swap_buffers();
        }
    }
    
    // Завершение
    serial_puts("\n=== SHUTDOWN ===\n");
    gui_shutdown();
    vesa_hide_cursor();
    vesa_fill(0x000000);
    serial_puts("[SYSTEM] Goodbye!\n");
    
    while(1) asm volatile("cli; hlt");
}