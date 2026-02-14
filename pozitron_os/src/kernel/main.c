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
#include "drivers/ata.h"
#include "kernel/memory.h"
#include "gui/gui.h"
#include "hw/scanner.h"
#include "drivers/power.h"
#include "gui/shutdown.h"
#include "kernel/multiboot_util.h"
#include "kernel/logo.h"
#include <stddef.h>
#include "lib/string.h"
#include "fs/fat32.h"

static uint8_t system_running = 1;

// ============ CALLBACK-ФУНКЦИИ ============
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

// ============ ТЕСТ МАКСИМИЗАЦИИ ============
static void test_maximize_callback(Widget* button, void* userdata) {
    if (!userdata) return;
    
    Window* window = (Window*)userdata;
    
    if (window->maximized) {
        wm_restore_window(window);
        serial_puts("[TEST] Window restored\n");
    } else {
        wm_maximize_window(window);
        serial_puts("[TEST] Window maximized\n");
    }
}

// ============ ТЕСТОВОЕ ОКНО ============
static Window* create_test_window(const char* title, uint32_t x, uint32_t y, 
                                  uint32_t width, uint32_t height, uint8_t use_relative) {
    Window* win = wm_create_window(title, x, y, width, height,
                                  WINDOW_CLOSABLE | WINDOW_MOVABLE | 
                                  WINDOW_HAS_TITLE | WINDOW_MINIMIZABLE |
                                  WINDOW_MAXIMIZABLE);
    
    if (!win) {
        serial_puts("[TEST] ERROR: Failed to create test window\n");
        return NULL;
    }
    
    if (use_relative) {
        // Относительные координаты
        
        // Метка с названием окна
        wg_create_label_rel(win, title, 0.1f, 0.1f);
        
        // Информация о типе окна
        wg_create_label_rel(win, "This window uses RELATIVE coordinates", 0.1f, 0.2f);
        wg_create_label_rel(win, "Widgets will scale on maximize!", 0.1f, 0.25f);
        
        // Чекбоксы
        Widget* cb1 = wg_create_checkbox_rel(win, "Feature A (relative)", 0.1f, 0.35f, 1);
        if (cb1) {
            wg_set_callback_ex(cb1, checkbox_toggled_callback, NULL);
        }
        
        Widget* cb2 = wg_create_checkbox_rel(win, "Feature B (relative)", 0.1f, 0.42f, 0);
        if (cb2) {
            wg_set_callback_ex(cb2, checkbox_toggled_callback, NULL);
        }
        
        // Слайдер
        wg_create_label_rel(win, "Volume:", 0.1f, 0.5f);
        Widget* slider = wg_create_slider_rel(win, 0.1f, 0.55f, 0.5f, 0.05f, 0, 100, 50);
        if (slider) {
            wg_set_callback_ex(slider, slider_changed_callback, NULL);
        }
        
        // Прогресс-бар
        wg_create_label_rel(win, "Progress:", 0.1f, 0.65f);
        Widget* progress = wg_create_progressbar_rel(win, 0.1f, 0.7f, 0.5f, 0.05f, 30);
        
        // Кнопки
        Widget* max_btn = wg_create_button_rel(win, "Maximize/Restore",
                                              0.65f, 0.35f, 0.25f, 0.1f,
                                              test_maximize_callback, win);
        
        Widget* update_btn = wg_create_button_rel(win, "Update Progress",
                                                0.65f, 0.5f, 0.25f, 0.1f,
                                                update_progress_callback, progress);
    } else {
        // Абсолютные координаты
        
        // Метка с названием окна
        wg_create_label(win, title, 20, 40);
        
        // Информация о типе окна
        wg_create_label(win, "This window uses ABSOLUTE coordinates", 20, 70);
        wg_create_label(win, "Widgets WON'T scale on maximize!", 20, 90);
        
        // Чекбоксы
        Widget* cb1 = wg_create_checkbox(win, "Feature A (absolute)", 40, 120, 1);
        if (cb1) {
            wg_set_callback_ex(cb1, checkbox_toggled_callback, NULL);
        }
        
        Widget* cb2 = wg_create_checkbox(win, "Feature B (absolute)", 40, 145, 0);
        if (cb2) {
            wg_set_callback_ex(cb2, checkbox_toggled_callback, NULL);
        }
        
        // Слайдер
        wg_create_label(win, "Volume:", 20, 175);
        Widget* slider = wg_create_slider(win, 40, 195, 200, 0, 100, 50);
        if (slider) {
            wg_set_callback_ex(slider, slider_changed_callback, NULL);
        }
        
        // Прогресс-бар
        wg_create_label(win, "Progress:", 20, 225);
        Widget* progress = wg_create_progressbar(win, 40, 245, 200, 20, 30);
        
        // Кнопки
        Widget* max_btn = wg_create_button_ex(win, "Maximize/Restore",
                                            250, 120, 120, 30,
                                            test_maximize_callback, win);
        
        Widget* update_btn = wg_create_button_ex(win, "Update Progress",
                                               250, 165, 120, 30,
                                               update_progress_callback, progress);
    }
    
    return win;
}

// ============ ДЕМО-ИНТЕРФЕЙС ============
static void create_demo_ui(void) {
    serial_puts("\n=== CREATING DEMO UI ===\n");
    
    // Главное демо-окно
    Window* main_win = wm_create_window("PozitronOS GUI Demo", 
                                       200, 100, 500, 400, 
                                       WINDOW_CLOSABLE | WINDOW_MOVABLE | 
                                       WINDOW_HAS_TITLE | WINDOW_MINIMIZABLE |
                                       WINDOW_MAXIMIZABLE);
    
    if (!main_win) {
        serial_puts("[DEMO] ERROR: Failed to create main window\n");
        return;
    }
    
    // Используем относительные координаты
    wg_create_label_rel(main_win, "PozitronOS GUI Demo", 
                       0.05f, 0.05f);
    
    wg_create_label_rel(main_win, "New coordinate system:", 0.05f, 0.12f);
    wg_create_label_rel(main_win, "1. Relative coordinates (0.0 - 1.0)", 0.1f, 0.17f);
    wg_create_label_rel(main_win, "2. Auto-scaling on maximize/resize", 0.1f, 0.22f);
    wg_create_label_rel(main_win, "3. Check serial output for events!", 0.05f, 0.32f);
    
    // Горизонтальная линия
    wg_create_label_rel(main_win, "--------------------------------------------", 
                       0.05f, 0.37f);
    
    // Чекбоксы
    Widget* cb1 = wg_create_checkbox_rel(main_win, "Use new coordinate system", 
                                        0.1f, 0.42f, 1);
    if (cb1) {
        wg_set_callback_ex(cb1, checkbox_toggled_callback, NULL);
    }
    
    Widget* cb2 = wg_create_checkbox_rel(main_win, "Auto-scale widgets", 
                                        0.1f, 0.48f, 1);
    if (cb2) {
        wg_set_callback_ex(cb2, checkbox_toggled_callback, NULL);
    }
    
    Widget* cb3 = wg_create_checkbox_rel(main_win, "Enable smart layout", 
                                        0.1f, 0.54f, 0);
    if (cb3) {
        wg_set_callback_ex(cb3, checkbox_toggled_callback, NULL);
    }
    
    // Слайдеры
    wg_create_label_rel(main_win, "Brightness:", 0.1f, 0.62f);
    Widget* slider1 = wg_create_slider_rel(main_win, 0.1f, 0.67f, 0.4f, 0.04f, 0, 100, 75);
    if (slider1) {
        wg_set_callback_ex(slider1, slider_changed_callback, NULL);
    }
    
    wg_create_label_rel(main_win, "Contrast:", 0.1f, 0.74f);
    Widget* slider2 = wg_create_slider_rel(main_win, 0.1f, 0.79f, 0.4f, 0.04f, 0, 100, 50);
    if (slider2) {
        wg_set_callback_ex(slider2, slider_changed_callback, NULL);
    }
    
    // Прогресс-бар
    wg_create_label_rel(main_win, "System load:", 0.6f, 0.42f);
    Widget* progress = wg_create_progressbar_rel(main_win, 0.6f, 0.47f, 0.3f, 0.06f, 45);
    
    // Кнопки
    Widget* update_btn = wg_create_button_rel(main_win, "Update Load",
                                            0.6f, 0.56f, 0.3f, 0.08f,
                                            update_progress_callback, progress);
    
    Widget* max_btn = wg_create_button_rel(main_win, "Maximize Window",
                                         0.6f, 0.67f, 0.3f, 0.08f,
                                         test_maximize_callback, main_win);
    
    // Создаём тестовые окна для сравнения
    serial_puts("[DEMO] Creating test windows for comparison...\n");
    
    // Окно с относительными координатами
    Window* rel_win = create_test_window("Relative Coords Window", 
                                        100, 150, 400, 350, 1);
    
    // Окно с абсолютными координатами
    Window* abs_win = create_test_window("Absolute Coords Window", 
                                        550, 150, 400, 350, 0);
    
    if (rel_win && abs_win) {
        serial_puts("[DEMO] Test windows created successfully\n");
        serial_puts("[DEMO] Try maximizing both windows to see the difference!\n");
    }
}

// ============ ОБРАБОТЧИК КЛАВИАТУРЫ ============
static void handle_keyboard_events(event_t* event) {
    if (!event) return;
    
    if (event->type == EVENT_KEY_PRESS) {
        switch (event->data1) {
            case 0x3B: // F1 - создать тестовое окно
                {
                    static uint32_t counter = 0;
                    counter++;
                    
                    char title[64];
                    char* ptr = title;
                    
                    // Формируем название окна
                    const char* prefix = "Test Window ";
                    while (*prefix) *ptr++ = *prefix++;
                    
                    // Добавляем номер
                    uint32_t n = counter;
                    if (n == 0) *ptr++ = '0';
                    while (n > 0) {
                        *ptr++ = '0' + (n % 10);
                        n /= 10;
                    }
                    *ptr = '\0';
                    
                    // Переворачиваем цифры
                    char* start = title + 12; // после "Test Window "
                    char* end = ptr - 1;
                    while (start < end) {
                        char temp = *start;
                        *start = *end;
                        *end = temp;
                        start++;
                        end--;
                    }
                    
                    // Чередуем типы окон
                    uint8_t use_relative = (counter % 2 == 0);
                    
                    // Создаем окно
                    Window* win = create_test_window(title,
                                                  100 + (counter * 30) % 500,
                                                  80 + (counter * 20) % 300,
                                                  350 + (counter * 10) % 150,
                                                  250 + (counter * 10) % 100,
                                                  use_relative);
                    
                    if (win) {
                        serial_puts("[KEY] F1: Created ");
                        serial_puts(use_relative ? "relative" : "absolute");
                        serial_puts(" coordinate window: ");
                        serial_puts(title);
                        serial_puts("\n");
                    }
                }
                break;
                
            case 0x01: // ESC - закрыть фокусное окно
                if (gui_state.focused_window) {
                    wm_close_window(gui_state.focused_window);
                }
                break;

            case 0x2A: // LSHIFT
            case 0x36: // RSHIFT
                break;
                
            case 0x5B: // Виндовс клавиша левая
            case 0x5C: // Виндовс клавиша правая
                start_menu_toggle();
                break;
                
            default:
                // Игнорируем другие клавиши
                break;
        }
    }
}

// ============ ГЛАВНАЯ ФУНКЦИЯ ============
void kernel_main(uint32_t magic, multiboot_info_t* mb_info) {
    // Инициализация системы
    multiboot_dump_info(mb_info);
    memory_init_multiboot(mb_info);

    serial_init();
    vga_init();
    vga_puts("\n");

    gdt_init();
    vga_puts("[ OK ] GDT OK\n");
    idt_init();
    vga_puts("[ OK ] IDT OK\n");
    pic_init();
    vga_puts("[ OK ] PIC OK\n");
    isr_init();
    vga_puts("[ OK ] ISR OK\n");
    asm volatile("sti");

    timer_init(100);
    vga_puts("[ OK ] TIMER OK\n");
    keyboard_init();
    memory_init();
    print_memory_map();
    memory_dump();
    vga_puts("[ OK ] MEMORY ALLOCATION SYSTEM OK\n");

    if(!vesa_init(mb_info)) {
        vga_puts("[ERROR] VBE/VESA INITIALISATION FAILED\n");
    } else {
        vga_puts("[ OK ] VBE/VESA OK\n");
    }
    vesa_enable_double_buffer();

    // ===== ПОКАЗЫВАЕМ ЛОГОТИП (ТОЛЬКО FADE-IN) =====
    show_boot_logo();

    // ===== ТЕПЕРЬ ИНИЦИАЛИЗИРУЕМ ВСЁ ОСТАЛЬНОЕ, ОБНОВЛЯЯ ПРОГРЕСС-БАР =====
    boot_progress = 5;
    update_boot_progress();

    serial_puts("[INFO] INITIALIZING ATA DRIVER\n");
    ata_enhanced_init();
    boot_progress = 15;
    update_boot_progress();

    cmos_init();
    vga_puts("[ OK ] CMOS RTC OK\n");
    boot_progress = 25;
    update_boot_progress();

    scanner_init();
    vga_puts("[INFO] SCANNING HARDWARE START\n");
    scanner_scan_all();
    scanner_dump_all();
    vga_puts("[ OK ] SCANNING HARDWARE FINISH\n");

    boot_progress = 60;
    update_boot_progress();

    // Графика
    uint32_t screen_width = vesa_get_width();
    uint32_t screen_height = vesa_get_height();

    vesa_cache_background();
    vesa_init_dirty();
    vesa_mark_dirty_all();
    vesa_cursor_init();
    vesa_cursor_set_visible(1);
    boot_progress = 70;
    update_boot_progress();

    mouse_init();
    boot_progress = 90;
    update_boot_progress();

    event_init();
    vga_puts("[ OK ] EVENT SYSTEM OK\n");

    boot_progress = 100;
    update_boot_progress();
    fade_out_boot_logo();

    if (vesa_is_background_cached()) {
        vesa_restore_background();
    }

    vga_puts("[INFO] STARTUP GUI ENVIRONMENT\n");
    gui_init(screen_width, screen_height);
    taskbar_init();
    //create_demo_ui();
    vga_puts("[ OK ] GUI ENVIRONMENT OK\n");

    // Информация в serial
    serial_puts("\n=== SYSTEM READY ===\n");
    vga_puts("[INFO] SYSTEMS READY\n");

    // Первый рендер
    gui_render();
    vesa_cursor_update();
    if (vesa_is_double_buffer_enabled()) {
        vesa_swap_buffers();
    }

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

        // 2. Обновляем анимацию затемнения
        if (is_shutdown_mode_active()) {
            update_shutdown_animation();
        }

        // 3. Восстанавливаем фон
        if (vesa_is_background_cached()) {
            vesa_restore_background_dirty();
        }

        // 4. Рендерим GUI
        gui_render();

        // 5. ПОКАЗЫВАЕМ КУРСОР ВСЕГДА
        vesa_show_cursor();
        vesa_cursor_update();

        // 6. Обновляем экран
        if (vesa_is_double_buffer_enabled()) {
            vesa_swap_buffers();
        }
    }
}