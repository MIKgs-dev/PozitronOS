#include "gui/gui.h"
#include "drivers/vesa.h"
#include "drivers/serial.h"
#include "drivers/mouse.h"
#include "kernel/memory.h"
#include "drivers/cmos.h"
#include "drivers/timer.h"
#include <stddef.h>
#include <string.h>

// ============ СТРУКТУРЫ ДАННЫХ ============
typedef struct {
    Window* window;
    uint32_t x, y, width, height;
    uint8_t pressed;
    uint8_t hover;
    uint8_t valid;
    char title[64];
} taskbar_button_t;

// Глобальные переменные таскбара
static taskbar_button_t taskbar_buttons[MAX_TASKBAR_BUTTONS];
static uint32_t taskbar_button_count = 0;
static uint32_t taskbar_scroll_offset = 0;
static uint8_t taskbar_initialized = 0;

// Меню Пуск
static Window* start_menu_window = NULL;
static uint8_t start_menu_visible = 0;
static uint32_t start_button_x = 0;
static uint32_t start_button_y = 0;
static uint8_t start_button_pressed = 0;
static uint8_t start_button_hover = 0;

// Кнопки прокрутки
static uint32_t scroll_left_button_x = 0;
static uint32_t scroll_right_button_x = 0;
static uint32_t scroll_button_y = 0;
static uint8_t scroll_left_pressed = 0;
static uint8_t scroll_right_pressed = 0;
static uint8_t scroll_left_hover = 0;
static uint8_t scroll_right_hover = 0;
static uint8_t scroll_buttons_visible = 0;

// Часы и меню даты
static uint32_t clock_button_x = 0;
static uint32_t clock_button_y = 0;
static uint32_t clock_button_width = 60;
static uint8_t clock_button_pressed = 0;
static uint8_t clock_button_hover = 0;
static char clock_text[16] = "00:00";
static char clock_text_full[16] = "00:00:00";
static char date_text[16] = "01.01.2000";


// Меню даты
static Window* date_menu_window = NULL;
static uint8_t date_menu_visible = 0;

// ============ ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ ============
static inline uint8_t is_valid_button_index(uint32_t index) {
    return (index < MAX_TASKBAR_BUTTONS && 
            taskbar_buttons[index].valid && 
            taskbar_buttons[index].window != NULL);
}

static inline void safe_strncpy(char* dest, const char* src, size_t n) {
    if (!dest || !src || n == 0) return;
    size_t i;
    for (i = 0; i < n - 1 && src[i]; i++) dest[i] = src[i];
    dest[i] = '\0';
}

// Функция для форматирования полной даты
static void format_date_string_full(uint8_t day, uint8_t month, uint16_t year, char* buffer) {
    // Формат: dd.mm.yyyy
    buffer[0] = '0' + (day / 10);
    buffer[1] = '0' + (day % 10);
    buffer[2] = '.';
    buffer[3] = '0' + (month / 10);
    buffer[4] = '0' + (month % 10);
    buffer[5] = '.';
    
    // Year (4 digits)
    buffer[6] = '0' + (year / 1000);
    buffer[7] = '0' + ((year % 1000) / 100);
    buffer[8] = '0' + ((year % 100) / 10);
    buffer[9] = '0' + (year % 10);
    buffer[10] = '\0';
}

// Функция для расчёта сколько кнопок помещается
static uint32_t calculate_visible_button_count(void) {
    if (!gui_state.initialized) return 0;
    
    uint32_t screen_width = gui_state.screen_width;
    if (screen_width == 0) return 0;
    
    uint32_t available_width = screen_width - START_BUTTON_WIDTH - clock_button_width - 10;
    
    if (scroll_buttons_visible) {
        available_width -= TASKBAR_SCROLL_BUTTON_WIDTH * 2 + TASKBAR_BUTTON_SPACING * 2;
    }
    
    uint32_t max_buttons = available_width / (TASKBAR_BUTTON_WIDTH + TASKBAR_BUTTON_SPACING);
    
    return max_buttons;
}

// Функция для обновления времени из RTC
static void update_time_from_rtc(void) {
    static uint32_t last_rtc_check = 0;
    static uint8_t initialized = 0;
    static uint8_t last_second = 255; // Значение, которое заведомо не совпадет
    
    uint32_t current_ticks = timer_get_ticks();
    
    // === ИСПРАВЛЕННАЯ ВЕРСИЯ ===
    // Обновляем каждые 50 тиков (0.5 секунды) или при изменении секунды
    if (!initialized || (current_ticks - last_rtc_check) >= 50) {
        rtc_datetime_t datetime;
        
        cmos_read_datetime(&datetime);
        
        // 1. Обновляем только если изменилась секунда
        if (datetime.seconds != last_second || !initialized) {
            last_second = datetime.seconds;
            
            // Для таскбара: HH:MM (5 символов)
            clock_text[0] = '0' + (datetime.hours / 10);
            clock_text[1] = '0' + (datetime.hours % 10);
            clock_text[2] = ':';
            clock_text[3] = '0' + (datetime.minutes / 10);
            clock_text[4] = '0' + (datetime.minutes % 10);
            clock_text[5] = '\0';
            
            // Для окна времени: HH:MM:SS (8 символов)
            clock_text_full[0] = '0' + (datetime.hours / 10);
            clock_text_full[1] = '0' + (datetime.hours % 10);
            clock_text_full[2] = ':';
            clock_text_full[3] = '0' + (datetime.minutes / 10);
            clock_text_full[4] = '0' + (datetime.minutes % 10);
            clock_text_full[5] = ':';
            clock_text_full[6] = '0' + (datetime.seconds / 10);
            clock_text_full[7] = '0' + (datetime.seconds % 10);
            clock_text_full[8] = '\0';
            
            format_date_string_full(datetime.day, datetime.month, datetime.year, date_text);
            
            // Помечаем область часов в таскбаре как dirty
            vesa_mark_dirty(clock_button_x, clock_button_y, 
                           clock_button_width, TASKBAR_BUTTON_HEIGHT);
        }
        
        last_rtc_check = current_ticks;
        initialized = 1;
    }
}

static void update_date_menu_time(void) {
    static uint32_t last_second_update = 0;
    static Widget* time_label_widget = NULL;
    
    // Обновляем только если окно открыто
    if (!date_menu_visible || !date_menu_window) return;
    
    // Находим лейбл времени один раз
    if (!time_label_widget) {
        Widget* widget = date_menu_window->first_widget;
        while (widget) {
            if (widget->type == WIDGET_LABEL) {
                // Ищем лейбл с временем по содержимому
                if (widget->text && widget->text[2] == ':') { // Проверяем формат HH:MM:SS
                    time_label_widget = widget;
                    break;
                }
            }
            widget = widget->next;
        }
    }
    
    if (!time_label_widget) return;
    
    uint32_t current_ticks = timer_get_ticks();
    
    // === ИСПРАВЛЕНИЕ: Обновляем каждые 10 тиков (100ms) ===
    if ((current_ticks - last_second_update) >= 10) {
        rtc_datetime_t datetime;
        cmos_read_datetime(&datetime);
        
        // Форматируем время с секундами
        char new_time[16];
        new_time[0] = '0' + (datetime.hours / 10);
        new_time[1] = '0' + (datetime.hours % 10);
        new_time[2] = ':';
        new_time[3] = '0' + (datetime.minutes / 10);
        new_time[4] = '0' + (datetime.minutes % 10);
        new_time[5] = ':';
        new_time[6] = '0' + (datetime.seconds / 10);
        new_time[7] = '0' + (datetime.seconds % 10);
        new_time[8] = '\0';
        
        // Сравниваем с текущим текстом, обновляем только если изменилось
        if (!time_label_widget->text || 
            strncmp(time_label_widget->text, new_time, 8) != 0) {
            wg_set_text(time_label_widget, new_time);
            time_label_widget->needs_redraw = 1;
            date_menu_window->needs_redraw = 1;
            
            // Помечаем область окна как dirty
            vesa_mark_dirty(date_menu_window->x, date_menu_window->y,
                          date_menu_window->width, date_menu_window->height);
        }
        
        last_second_update = current_ticks;
    }
}

// ============ ИНИЦИАЛИЗАЦИЯ ============
void taskbar_init(void) {
    if (taskbar_initialized) return;
    
    for (uint32_t i = 0; i < MAX_TASKBAR_BUTTONS; i++) {
        taskbar_buttons[i].window = NULL;
        taskbar_buttons[i].title[0] = '\0';
        taskbar_buttons[i].pressed = 0;
        taskbar_buttons[i].hover = 0;
        taskbar_buttons[i].valid = 0;
    }
    
    taskbar_button_count = 0;
    taskbar_scroll_offset = 0;
    taskbar_initialized = 1;
    
    rtc_datetime_t datetime;
    cmos_read_datetime(&datetime);
    
    clock_text[0] = '0' + (datetime.hours / 10);
    clock_text[1] = '0' + (datetime.hours % 10);
    clock_text[2] = ':';
    clock_text[3] = '0' + (datetime.minutes / 10);
    clock_text[4] = '0' + (datetime.minutes % 10);
    clock_text[5] = '\0';
    
    format_date_string_full(datetime.day, datetime.month, datetime.year, date_text);
    
    serial_puts("[TASKBAR] Initialized with RTC time: ");
    serial_puts(clock_text);
    serial_puts("\n");
}

// ============ ФУНКЦИИ ДЛЯ WM ============
void taskbar_add_window(Window* window) {
    if (!taskbar_initialized || !window) return;
    if (!IS_VALID_WINDOW_PTR(window)) return;
    if (!window->in_taskbar) return;
    
    for (uint32_t i = 0; i < MAX_TASKBAR_BUTTONS; i++) {
        if (is_valid_button_index(i) && 
            taskbar_buttons[i].window && 
            taskbar_buttons[i].window->id == window->id) {
            taskbar_update_window(window);
            return;
        }
    }
    
    uint32_t free_slot = MAX_TASKBAR_BUTTONS;
    for (uint32_t i = 0; i < MAX_TASKBAR_BUTTONS; i++) {
        if (!taskbar_buttons[i].valid) {
            free_slot = i;
            break;
        }
    }
    
    if (free_slot == MAX_TASKBAR_BUTTONS) return;
    
    taskbar_button_t* btn = &taskbar_buttons[free_slot];
    btn->window = window;
    btn->pressed = 0;
    btn->hover = 0;
    btn->valid = 1;
    btn->width = TASKBAR_BUTTON_WIDTH;
    btn->height = TASKBAR_BUTTON_HEIGHT;
    
    if (window->title && window->title[0]) {
        safe_strncpy(btn->title, window->title, sizeof(btn->title));
    } else {
        btn->title[0] = '\0';
    }
    
    taskbar_button_count++;
    
    if (taskbar_button_count > 0) {
        uint32_t visible_count = calculate_visible_button_count();
        if (visible_count > 0 && free_slot >= taskbar_scroll_offset + visible_count) {
            taskbar_scroll_offset = free_slot - visible_count + 1;
        }
    }
}

void taskbar_remove_window(Window* window) {
    if (!taskbar_initialized || !window) return;
    
    uint32_t found_index = MAX_TASKBAR_BUTTONS;
    uint32_t window_id = window->id;
    
    if (IS_VALID_WINDOW_PTR(window)) {
        for (uint32_t i = 0; i < MAX_TASKBAR_BUTTONS; i++) {
            if (taskbar_buttons[i].valid && 
                taskbar_buttons[i].window == window) {
                found_index = i;
                break;
            }
        }
    }
    
    if (found_index == MAX_TASKBAR_BUTTONS && window_id != 0) {
        for (uint32_t i = 0; i < MAX_TASKBAR_BUTTONS; i++) {
            if (taskbar_buttons[i].valid && 
                taskbar_buttons[i].window && 
                taskbar_buttons[i].window->id == window_id) {
                found_index = i;
                break;
            }
        }
    }
    
    if (found_index == MAX_TASKBAR_BUTTONS) return;
    
    taskbar_buttons[found_index].valid = 0;
    taskbar_buttons[found_index].window = NULL;
    taskbar_buttons[found_index].title[0] = '\0';
    taskbar_buttons[found_index].pressed = 0;
    taskbar_buttons[found_index].hover = 0;
    
    if (taskbar_button_count > 0) {
        taskbar_button_count--;
    }
    
    uint32_t visible_count = calculate_visible_button_count();
    if (taskbar_scroll_offset > 0 && taskbar_scroll_offset >= taskbar_button_count) {
        if (taskbar_button_count > visible_count) {
            taskbar_scroll_offset = taskbar_button_count - visible_count;
        } else {
            taskbar_scroll_offset = 0;
        }
    }
}

void taskbar_update_window(Window* window) {
    if (!taskbar_initialized || !window) return;
    if (!IS_VALID_WINDOW_PTR(window)) return;
    
    for (uint32_t i = 0; i < MAX_TASKBAR_BUTTONS; i++) {
        if (is_valid_button_index(i) && 
            taskbar_buttons[i].window && 
            taskbar_buttons[i].window->id == window->id) {
            
            if (window->title && window->title[0]) {
                safe_strncpy(taskbar_buttons[i].title, window->title, 
                           sizeof(taskbar_buttons[i].title));
            } else {
                taskbar_buttons[i].title[0] = '\0';
            }
            break;
        }
    }
}

// ============ ФУНКЦИИ ПРОКРУТКИ ============
void taskbar_scroll_left(void) {
    if (taskbar_scroll_offset > 0) {
        taskbar_scroll_offset--;
    }
}

void taskbar_scroll_right(void) {
    uint32_t max_offset = taskbar_button_count > 0 ? taskbar_button_count - 1 : 0;
    uint32_t visible_count = calculate_visible_button_count();
    
    if (visible_count > 0 && taskbar_scroll_offset < max_offset) {
        if (taskbar_scroll_offset + visible_count < taskbar_button_count) {
            taskbar_scroll_offset++;
        }
    }
}

uint32_t taskbar_get_scroll_offset(void) {
    return taskbar_scroll_offset;
}

uint32_t taskbar_get_visible_button_count(void) {
    return calculate_visible_button_count();
}

uint32_t taskbar_get_total_button_count(void) {
    return taskbar_button_count;
}

// ============ МЕНЮ ДАТЫ ============
static void close_date_menu(void) {
    if (date_menu_window) {
        // Освобождаем память для времени
        Widget* widget = date_menu_window->first_widget;
        while (widget) {
            if (widget->userdata) {
                kfree(widget->userdata);
                widget->userdata = NULL;
            }
            widget = widget->next;
        }
        
        if (IS_VALID_WINDOW_PTR(date_menu_window)) {
            wm_destroy_window(date_menu_window);
        }
        date_menu_window = NULL;
    }
    date_menu_visible = 0;
    clock_button_pressed = 0;
}

static void create_date_menu(void) {
    if (date_menu_visible && date_menu_window) {
        close_date_menu();
        return;
    }
    
    uint32_t screen_height = gui_state.screen_height;
    if (screen_height < TASKBAR_HEIGHT + 120) return;
    
    // Обновляем время перед созданием окна
    rtc_datetime_t datetime;
    cmos_read_datetime(&datetime);
    
    // Форматируем время с секундами для отображения
    char time_str[16];
    time_str[0] = '0' + (datetime.hours / 10);
    time_str[1] = '0' + (datetime.hours % 10);
    time_str[2] = ':';
    time_str[3] = '0' + (datetime.minutes / 10);
    time_str[4] = '0' + (datetime.minutes % 10);
    time_str[5] = ':';
    time_str[6] = '0' + (datetime.seconds / 10);
    time_str[7] = '0' + (datetime.seconds % 10);
    time_str[8] = '\0';
    
    // Форматируем дату
    char full_date[32];
    format_date_string_full(datetime.day, datetime.month, datetime.year, full_date);
    
    uint32_t menu_x = clock_button_x - 60;
    uint32_t menu_y = screen_height - TASKBAR_HEIGHT - 120;
    
    if (menu_x < 2) menu_x = 2;
    if (menu_x > gui_state.screen_width - 180) menu_x = gui_state.screen_width - 180;
    
    date_menu_window = wm_create_window("Date and Time",
                                       menu_x, menu_y,
                                       180, 120,
                                       WINDOW_MOVABLE | WINDOW_HAS_TITLE);
    
    if (!date_menu_window) return;
    
    date_menu_visible = 1;
    date_menu_window->closable = 0;
    date_menu_window->in_taskbar = 0;
    date_menu_window->minimizable = 0;
    
    taskbar_remove_window(date_menu_window);
    
    // Создаем элементы интерфейса с актуальным временем
    wg_create_label(date_menu_window, "Current time:", 10, 30);
    
    // Создаем лейбл для времени (будет обновляться динамически)
    Widget* time_label = wg_create_label(date_menu_window, time_str, 10, 50);
    
    wg_create_label(date_menu_window, "Date:", 10, 80);
    wg_create_label(date_menu_window, full_date, 10, 100);
    
    // Сохраняем указатель на лейбл времени для обновления
    if (time_label) {
        // Выделяем память для хранения времени и сохраняем в userdata
        char* stored_time = (char*)kmalloc(16);
        if (stored_time) {
            safe_strncpy(stored_time, time_str, 16);
            time_label->userdata = stored_time;
        }
    }
    
    wm_focus_window(date_menu_window);
    
    serial_puts("[TASKBAR] Date menu opened, time: ");
    serial_puts(time_str);
    serial_puts("\n");
}

void date_menu_toggle(void) {
    if (date_menu_visible) {
        close_date_menu();
    } else {
        create_date_menu();
    }
}

uint8_t date_menu_is_visible(void) {
    return date_menu_visible && (date_menu_window != NULL);
}

// ============ ОБНОВЛЕНИЕ ГЕОМЕТРИИ ============
static void update_taskbar_geometry(void) {
    if (!taskbar_initialized) return;
    
    uint32_t screen_width = gui_state.screen_width;
    uint32_t screen_height = gui_state.screen_height;
    if (screen_width == 0 || screen_height == 0) return;
    
    uint32_t taskbar_top = screen_height - TASKBAR_HEIGHT;
    
    start_button_x = 2;
    start_button_y = taskbar_top + 2;
    
    clock_button_x = screen_width - clock_button_width - 2;
    clock_button_y = taskbar_top + 2;
    
    uint32_t visible_button_count = calculate_visible_button_count();
    uint32_t max_visible_buttons = (screen_width - START_BUTTON_WIDTH - clock_button_width - 20) / 
                                  (TASKBAR_BUTTON_WIDTH + TASKBAR_BUTTON_SPACING);
    
    scroll_buttons_visible = (taskbar_button_count > max_visible_buttons);
    
    uint32_t first_button_x = START_BUTTON_WIDTH + 5;
    
    if (scroll_buttons_visible) {
        scroll_left_button_x = first_button_x;
        scroll_right_button_x = clock_button_x - TASKBAR_SCROLL_BUTTON_WIDTH - 5;
        scroll_button_y = start_button_y;
        
        first_button_x += TASKBAR_SCROLL_BUTTON_WIDTH + TASKBAR_BUTTON_SPACING;
        visible_button_count = (scroll_right_button_x - first_button_x) / 
                              (TASKBAR_BUTTON_WIDTH + TASKBAR_BUTTON_SPACING);
    }
    
    if (taskbar_scroll_offset + visible_button_count > taskbar_button_count) {
        if (taskbar_button_count > visible_button_count) {
            taskbar_scroll_offset = taskbar_button_count - visible_button_count;
        } else {
            taskbar_scroll_offset = 0;
        }
    }
    
    uint32_t current_x = first_button_x;
    uint32_t buttons_placed = 0;
    
    for (uint32_t i = 0; i < MAX_TASKBAR_BUTTONS; i++) {
        if (is_valid_button_index(i)) {
            taskbar_buttons[i].x = 0;
            taskbar_buttons[i].y = 0;
        }
    }
    
    for (uint32_t i = taskbar_scroll_offset; 
         i < MAX_TASKBAR_BUTTONS && buttons_placed < visible_button_count; 
         i++) {
        
        if (!is_valid_button_index(i)) continue;
        
        taskbar_buttons[i].x = current_x;
        taskbar_buttons[i].y = start_button_y;
        taskbar_buttons[i].width = TASKBAR_BUTTON_WIDTH;
        taskbar_buttons[i].height = TASKBAR_BUTTON_HEIGHT;
        
        current_x += TASKBAR_BUTTON_WIDTH + TASKBAR_BUTTON_SPACING;
        buttons_placed++;
    }
}

// ============ МЕНЮ ПУСК ============
static void create_window_from_start_menu(Widget* button, void* userdata) {
    if (!button || !userdata) return;
    
    const char* window_title = (const char*)userdata;
    char title_buffer[64];
    const char* src = window_title;
    char* dst = title_buffer;
    size_t i = 0;
    
    i = 0;
    while (src[i] && i < 62) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
    
    static uint8_t offset = 0;
    offset = (offset + 1) % 4;
    uint32_t x_pos[] = {200, 250, 300, 350};
    uint32_t y_pos[] = {150, 180, 120, 200};
    
    Window* win = wm_create_window(title_buffer,
                                  x_pos[offset], y_pos[offset],
                                  350, 250,
                                  WINDOW_CLOSABLE | WINDOW_MOVABLE | WINDOW_HAS_TITLE | WINDOW_MINIMIZABLE);
    
    if (win) {
        wg_create_label(win, "Application", 20, 50);
        wg_create_label(win, "Created from Start Menu", 20, 80);
        Widget* close_btn = wg_create_button(win, "Close", 20, 120, 100, 30);
        if (close_btn) {
            wg_set_callback_ex(close_btn, (void (*)(Widget*, void*))wm_close_window, win);
        }
    }
    
    start_menu_close();
}

void start_menu_create(void) {
    if (start_menu_visible && start_menu_window) {
        start_menu_close();
        return;
    }
    
    uint32_t screen_height = gui_state.screen_height;
    if (screen_height < TASKBAR_HEIGHT + 250) return;
    
    uint32_t menu_x = 2;
    uint32_t menu_y = screen_height - TASKBAR_HEIGHT - 250;
    
    start_menu_window = wm_create_window("Start Menu",
                                        menu_x, menu_y,
                                        250, 250,
                                        WINDOW_HAS_TITLE);
    
    if (!start_menu_window) return;
    
    start_menu_visible = 1;
    start_menu_window->closable = 0;
    start_menu_window->in_taskbar = 0;
    start_menu_window->minimizable = 0;
    
    taskbar_remove_window(start_menu_window);
    
    wg_create_label(start_menu_window, "PozitronOS Programs", 10, 30);
    vesa_draw_rect(start_menu_window->x + 5, start_menu_window->y + 55, 
                  240, 1, 0x808080);
    
    wg_create_button_rel(start_menu_window, "Calculator",
                    0.04f, 0.24f,   // x = 4%, y = 24% (примерно 10,60 при 250x250)
                    0.92f, 0.1f,    // width = 92%, height = 10% (230x25)
                    create_window_from_start_menu, "Calculator");

    Widget* shutdown_btn = wg_create_button_ex(start_menu_window, 
    "Shutdown Computer", 
    10, 180, 230, 25,
    shutdown_dialog_callback,
    NULL);
    
    vesa_draw_rect(start_menu_window->x + 5, start_menu_window->y + 210, 
                  240, 1, 0x808080);
    
    wm_focus_window(start_menu_window);
}

void start_menu_toggle(void) {
    if (start_menu_visible) {
        start_menu_close();
    } else {
        start_menu_create();
    }
}

void start_menu_close(void) {
    if (start_menu_window) {
        if (IS_VALID_WINDOW_PTR(start_menu_window)) {
            wm_destroy_window(start_menu_window);
        }
        start_menu_window = NULL;
    }
    start_menu_visible = 0;
    start_button_pressed = 0;
}

uint8_t start_menu_is_visible(void) {
    return start_menu_visible && (start_menu_window != NULL);
}

Window* start_menu_get_window(void) {
    return start_menu_window;
}

// ============ ПОИСК КНОПКИ ============
uint8_t taskbar_find_button_at(uint32_t x, uint32_t y, Window** window) {
    if (!taskbar_initialized || !window) return 0;
    
    uint32_t screen_height = gui_state.screen_height;
    if (screen_height == 0) return 0;
    
    uint32_t taskbar_top = screen_height - TASKBAR_HEIGHT;
    if (y < taskbar_top) return 0;
    
    update_taskbar_geometry();
    
    if (x >= start_button_x && x < start_button_x + START_BUTTON_WIDTH &&
        y >= start_button_y && y < start_button_y + TASKBAR_BUTTON_HEIGHT) {
        *window = NULL;
        return 1;
    }
    
    if (x >= clock_button_x && x < clock_button_x + clock_button_width &&
        y >= clock_button_y && y < clock_button_y + TASKBAR_BUTTON_HEIGHT) {
        *window = (Window*)3;
        return 1;
    }
    
    if (scroll_buttons_visible) {
        if (x >= scroll_left_button_x && x < scroll_left_button_x + TASKBAR_SCROLL_BUTTON_WIDTH &&
            y >= scroll_button_y && y < scroll_button_y + TASKBAR_BUTTON_HEIGHT) {
            *window = (Window*)1;
            return 1;
        }
        
        if (x >= scroll_right_button_x && x < scroll_right_button_x + TASKBAR_SCROLL_BUTTON_WIDTH &&
            y >= scroll_button_y && y < scroll_button_y + TASKBAR_BUTTON_HEIGHT) {
            *window = (Window*)2;
            return 1;
        }
    }
    
    for (uint32_t i = 0; i < MAX_TASKBAR_BUTTONS; i++) {
        if (!is_valid_button_index(i)) continue;
        
        if (taskbar_buttons[i].x == 0) continue;
        
        if (x >= taskbar_buttons[i].x && x < taskbar_buttons[i].x + taskbar_buttons[i].width &&
            y >= taskbar_buttons[i].y && y < taskbar_buttons[i].y + taskbar_buttons[i].height) {
            
            Window* win = taskbar_buttons[i].window;
            
            if (!IS_VALID_WINDOW_PTR(win)) {
                taskbar_buttons[i].valid = 0;
                taskbar_buttons[i].window = NULL;
                return 0;
            }
            
            *window = win;
            return 1;
        }
    }
    
    return 0;
}

// ============ ОБРАБОТКА СОБЫТИЙ ============
void taskbar_handle_event(event_t* event) {
    if (!taskbar_initialized || !event) return;
    
    update_taskbar_geometry();
    
    if (event->type == EVENT_MOUSE_CLICK) {
        uint32_t mx = event->data1;
        uint32_t my = event->data2 & 0xFFFF;
        uint32_t button = (event->data2 >> 16) & 0xFF;
        
        if (button == 0) {
            Window* clicked_window = NULL;
            
            if (taskbar_find_button_at(mx, my, &clicked_window)) {
                if (clicked_window == NULL) {
                    start_button_pressed = 1;
                    start_menu_toggle();
                } 
                else if (clicked_window == (Window*)1) {
                    scroll_left_pressed = 1;
                    taskbar_scroll_left();
                }
                else if (clicked_window == (Window*)2) {
                    scroll_right_pressed = 1;
                    taskbar_scroll_right();
                }
                else if (clicked_window == (Window*)3) {
                    clock_button_pressed = 1;
                    date_menu_toggle();
                }
                else {
                    if (!IS_VALID_WINDOW_PTR(clicked_window)) {
                        taskbar_remove_window(clicked_window);
                        return;
                    }
                    
                    for (uint32_t i = 0; i < MAX_TASKBAR_BUTTONS; i++) {
                        if (is_valid_button_index(i) && 
                            taskbar_buttons[i].window && 
                            taskbar_buttons[i].window->id == clicked_window->id) {
                            
                            taskbar_buttons[i].pressed = 1;
                            
                            if (clicked_window->minimized) {
                                wm_restore_window(clicked_window);
                            } else {
                                if (clicked_window->focused) {
                                    wm_minimize_window(clicked_window);
                                } else {
                                    wm_focus_window(clicked_window);
                                }
                            }
                            return;
                        }
                    }
                }
            }
        }
    }
    else if (event->type == EVENT_MOUSE_RELEASE) {
        uint32_t button = (event->data2 >> 16) & 0xFF;
        
        if (button == 0) {
            start_button_pressed = 0;
            scroll_left_pressed = 0;
            scroll_right_pressed = 0;
            clock_button_pressed = 0;
            
            for (uint32_t i = 0; i < MAX_TASKBAR_BUTTONS; i++) {
                if (taskbar_buttons[i].valid) {
                    taskbar_buttons[i].pressed = 0;
                }
            }
        }
    }
    else if (event->type == EVENT_MOUSE_MOVE) {
        uint32_t mx = event->data1;
        uint32_t my = event->data2 & 0xFFFF;
        
        uint32_t screen_height = gui_state.screen_height;
        uint32_t taskbar_top = screen_height - TASKBAR_HEIGHT;
        start_button_y = taskbar_top + 2;
        scroll_button_y = start_button_y;
        clock_button_y = start_button_y;
        
        update_taskbar_geometry();
        
        if (mx >= start_button_x && mx < start_button_x + START_BUTTON_WIDTH &&
            my >= start_button_y && my < start_button_y + TASKBAR_BUTTON_HEIGHT) {
            start_button_hover = 1;
        } else {
            start_button_hover = 0;
        }
        
        if (mx >= clock_button_x && mx < clock_button_x + clock_button_width &&
            my >= clock_button_y && my < clock_button_y + TASKBAR_BUTTON_HEIGHT) {
            clock_button_hover = 1;
        } else {
            clock_button_hover = 0;
        }
        
        if (scroll_buttons_visible) {
            if (mx >= scroll_left_button_x && mx < scroll_left_button_x + TASKBAR_SCROLL_BUTTON_WIDTH &&
                my >= scroll_button_y && my < scroll_button_y + TASKBAR_BUTTON_HEIGHT) {
                scroll_left_hover = 1;
            } else {
                scroll_left_hover = 0;
            }
            
            if (mx >= scroll_right_button_x && mx < scroll_right_button_x + TASKBAR_SCROLL_BUTTON_WIDTH &&
                my >= scroll_button_y && my < scroll_button_y + TASKBAR_BUTTON_HEIGHT) {
                scroll_right_hover = 1;
            } else {
                scroll_right_hover = 0;
            }
        }
        
        for (uint32_t i = 0; i < MAX_TASKBAR_BUTTONS; i++) {
            if (!is_valid_button_index(i)) continue;
            
            if (taskbar_buttons[i].x == 0) {
                taskbar_buttons[i].hover = 0;
                continue;
            }
            
            if (mx >= taskbar_buttons[i].x && mx < taskbar_buttons[i].x + taskbar_buttons[i].width &&
                my >= taskbar_buttons[i].y && my < taskbar_buttons[i].y + taskbar_buttons[i].height) {
                taskbar_buttons[i].hover = 1;
            } else {
                taskbar_buttons[i].hover = 0;
            }
        }
    }
}

// ============ РЕНДЕРИНГ ============
void taskbar_render(void) {
    if (!taskbar_initialized) return;
    
    uint32_t screen_width = gui_state.screen_width;
    uint32_t screen_height = gui_state.screen_height;
    if (screen_width == 0 || screen_height == 0) return;
    
    uint32_t taskbar_top = screen_height - TASKBAR_HEIGHT;
    update_taskbar_geometry();
    update_time_from_rtc();

    if (date_menu_visible) {
        update_date_menu_time();
    }
    
    vesa_draw_rect(0, taskbar_top, screen_width, TASKBAR_HEIGHT, TASKBAR_COLOR);
    vesa_draw_rect(0, taskbar_top, screen_width, 1, TASKBAR_SHADOW);
    vesa_draw_rect(0, taskbar_top + 1, screen_width, 1, TASKBAR_HIGHLIGHT);
    
    uint32_t start_color;
    if (start_button_pressed) {
        start_color = TASKBAR_BUTTON_ACTIVE;
    } else if (start_button_hover) {
        start_color = TASKBAR_BUTTON_HOVER;
    } else if (start_menu_is_visible()) {
        start_color = TASKBAR_BUTTON_ACTIVE;
    } else {
        start_color = TASKBAR_BUTTON_COLOR;
    }
    
    vesa_draw_rect(start_button_x, start_button_y, 
                  START_BUTTON_WIDTH, TASKBAR_BUTTON_HEIGHT, start_color);
    
    uint32_t start_border = start_button_pressed ? 0x1C97EA : TASKBAR_HIGHLIGHT;
    vesa_draw_rect(start_button_x, start_button_y, START_BUTTON_WIDTH, 1, start_border);
    vesa_draw_rect(start_button_x, start_button_y + TASKBAR_BUTTON_HEIGHT - 1, 
                  START_BUTTON_WIDTH, 1, start_border);
    vesa_draw_rect(start_button_x, start_button_y, 1, TASKBAR_BUTTON_HEIGHT, start_border);
    vesa_draw_rect(start_button_x + START_BUTTON_WIDTH - 1, start_button_y, 
                  1, TASKBAR_BUTTON_HEIGHT, start_border);
    
    vesa_draw_text(start_button_x + 8, start_button_y + 7, "Start", 
                  TASKBAR_TEXT_COLOR, start_color);
    
    if (scroll_buttons_visible) {
        uint32_t left_color = scroll_left_pressed ? TASKBAR_BUTTON_ACTIVE : 
                             (scroll_left_hover ? TASKBAR_BUTTON_HOVER : TASKBAR_BUTTON_COLOR);
        vesa_draw_rect(scroll_left_button_x, scroll_button_y, 
                      TASKBAR_SCROLL_BUTTON_WIDTH, TASKBAR_BUTTON_HEIGHT, left_color);
        
        uint32_t arrow_left_x = scroll_left_button_x + 7;
        uint32_t arrow_left_y = scroll_button_y + 10;
        for (uint8_t i = 0; i < 5; i++) {
            vesa_draw_rect(arrow_left_x + 4 - i, arrow_left_y + i, 1, 5 - i * 2, TASKBAR_TEXT_COLOR);
        }
        
        uint32_t right_color = scroll_right_pressed ? TASKBAR_BUTTON_ACTIVE : 
                              (scroll_right_hover ? TASKBAR_BUTTON_HOVER : TASKBAR_BUTTON_COLOR);
        vesa_draw_rect(scroll_right_button_x, scroll_button_y, 
                      TASKBAR_SCROLL_BUTTON_WIDTH, TASKBAR_BUTTON_HEIGHT, right_color);
        
        uint32_t arrow_right_x = scroll_right_button_x + 7;
        uint32_t arrow_right_y = scroll_button_y + 10;
        for (uint8_t i = 0; i < 5; i++) {
            vesa_draw_rect(arrow_right_x + i, arrow_right_y + i, 1, 5 - i * 2, TASKBAR_TEXT_COLOR);
        }
        
        vesa_draw_rect(scroll_left_button_x, scroll_button_y, 
                      TASKBAR_SCROLL_BUTTON_WIDTH, 1, TASKBAR_HIGHLIGHT);
        vesa_draw_rect(scroll_left_button_x, scroll_button_y + TASKBAR_BUTTON_HEIGHT - 1, 
                      TASKBAR_SCROLL_BUTTON_WIDTH, 1, TASKBAR_HIGHLIGHT);
        vesa_draw_rect(scroll_left_button_x, scroll_button_y, 
                      1, TASKBAR_BUTTON_HEIGHT, TASKBAR_HIGHLIGHT);
        vesa_draw_rect(scroll_left_button_x + TASKBAR_SCROLL_BUTTON_WIDTH - 1, scroll_button_y, 
                      1, TASKBAR_BUTTON_HEIGHT, TASKBAR_HIGHLIGHT);
        
        vesa_draw_rect(scroll_right_button_x, scroll_button_y, 
                      TASKBAR_SCROLL_BUTTON_WIDTH, 1, TASKBAR_HIGHLIGHT);
        vesa_draw_rect(scroll_right_button_x, scroll_button_y + TASKBAR_BUTTON_HEIGHT - 1, 
                      TASKBAR_SCROLL_BUTTON_WIDTH, 1, TASKBAR_HIGHLIGHT);
        vesa_draw_rect(scroll_right_button_x, scroll_button_y, 
                      1, TASKBAR_BUTTON_HEIGHT, TASKBAR_HIGHLIGHT);
        vesa_draw_rect(scroll_right_button_x + TASKBAR_SCROLL_BUTTON_WIDTH - 1, scroll_button_y, 
                      1, TASKBAR_BUTTON_HEIGHT, TASKBAR_HIGHLIGHT);
    }
    
    if (taskbar_button_count > 0) {
        for (uint32_t i = 0; i < MAX_TASKBAR_BUTTONS; i++) {
            if (!is_valid_button_index(i)) continue;
            
            if (taskbar_buttons[i].x == 0) continue;
            
            Window* win = taskbar_buttons[i].window;
            
            if (!IS_VALID_WINDOW_PTR(win)) {
                taskbar_buttons[i].valid = 0;
                taskbar_buttons[i].window = NULL;
                continue;
            }
            
            uint32_t btn_color;
            if (taskbar_buttons[i].pressed) {
                btn_color = TASKBAR_BUTTON_ACTIVE;
            } else if (taskbar_buttons[i].hover) {
                btn_color = TASKBAR_BUTTON_HOVER;
            } else if (win->focused && !win->minimized) {
                btn_color = TASKBAR_BUTTON_ACTIVE;
            } else if (win->minimized) {
                btn_color = 0x252526;
            } else {
                btn_color = TASKBAR_BUTTON_COLOR;
            }
            
            vesa_draw_rect(taskbar_buttons[i].x, taskbar_buttons[i].y, 
                          taskbar_buttons[i].width, taskbar_buttons[i].height, btn_color);
            
            uint32_t btn_border = taskbar_buttons[i].pressed ? 0x1C97EA : TASKBAR_HIGHLIGHT;
            vesa_draw_rect(taskbar_buttons[i].x, taskbar_buttons[i].y, 
                          taskbar_buttons[i].width, 1, btn_border);
            vesa_draw_rect(taskbar_buttons[i].x, taskbar_buttons[i].y + taskbar_buttons[i].height - 1, 
                          taskbar_buttons[i].width, 1, btn_border);
            vesa_draw_rect(taskbar_buttons[i].x, taskbar_buttons[i].y, 
                          1, taskbar_buttons[i].height, btn_border);
            vesa_draw_rect(taskbar_buttons[i].x + taskbar_buttons[i].width - 1, taskbar_buttons[i].y, 
                          1, taskbar_buttons[i].height, btn_border);
            
            if (taskbar_buttons[i].title[0]) {
                uint32_t max_chars = (TASKBAR_BUTTON_WIDTH - 10) / 8;
                if (max_chars < 3) max_chars = 3;
                
                char display_text[64];
                safe_strncpy(display_text, taskbar_buttons[i].title, max_chars + 1);
                
                uint32_t title_len = 0;
                while (taskbar_buttons[i].title[title_len]) title_len++;
                
                if (max_chars < title_len && max_chars >= 3) {
                    display_text[max_chars - 1] = '.';
                    display_text[max_chars - 2] = '.';
                    display_text[max_chars - 3] = '.';
                    display_text[max_chars] = '\0';
                }
                
                uint32_t text_color = TASKBAR_TEXT_COLOR;
                if (win->minimized) text_color = 0x888888;
                
                vesa_draw_text(taskbar_buttons[i].x + 5, taskbar_buttons[i].y + 7, 
                              display_text, text_color, btn_color);
            }
        }
    }
    
    uint32_t clock_color;
    if (clock_button_pressed) {
        clock_color = TASKBAR_BUTTON_ACTIVE;
    } else if (clock_button_hover) {
        clock_color = TASKBAR_BUTTON_HOVER;
    } else if (date_menu_is_visible()) {
        clock_color = TASKBAR_BUTTON_ACTIVE;
    } else {
        clock_color = TASKBAR_BUTTON_COLOR;
    }
    
    vesa_draw_rect(clock_button_x, clock_button_y, 
                  clock_button_width, TASKBAR_BUTTON_HEIGHT, clock_color);
    
    uint32_t clock_border = clock_button_pressed ? 0x1C97EA : TASKBAR_HIGHLIGHT;
    vesa_draw_rect(clock_button_x, clock_button_y, clock_button_width, 1, clock_border);
    vesa_draw_rect(clock_button_x, clock_button_y + TASKBAR_BUTTON_HEIGHT - 1, 
                  clock_button_width, 1, clock_border);
    vesa_draw_rect(clock_button_x, clock_button_y, 1, TASKBAR_BUTTON_HEIGHT, clock_border);
    vesa_draw_rect(clock_button_x + clock_button_width - 1, clock_button_y, 
                  1, TASKBAR_BUTTON_HEIGHT, clock_border);
    
    uint32_t time_text_width = 5 * 8;
    uint32_t time_x = clock_button_x + (clock_button_width - time_text_width) / 2;
    vesa_draw_text(time_x, clock_button_y + 7, clock_text, 
                  TASKBAR_TEXT_COLOR, clock_color);
    
    if (scroll_buttons_visible && taskbar_button_count > 0) {
        uint32_t visible_count = calculate_visible_button_count();
        if (visible_count > 0 && taskbar_button_count > visible_count) {
            uint32_t indicator_width = 200;
            uint32_t indicator_height = 4;
            uint32_t indicator_x = (screen_width - indicator_width) / 2;
            uint32_t indicator_y = screen_height - 4;
            
            vesa_draw_rect(indicator_x, indicator_y, 
                          indicator_width, indicator_height, 0x505054);
            
            uint32_t slider_width = (indicator_width * visible_count) / taskbar_button_count;
            if (slider_width < 20) slider_width = 20;
            
            uint32_t max_offset = taskbar_button_count - visible_count;
            if (max_offset > 0) {
                uint32_t slider_pos = (indicator_width - slider_width) * taskbar_scroll_offset / max_offset;
                
                vesa_draw_rect(indicator_x + slider_pos, indicator_y, 
                              slider_width, indicator_height, 0x007ACC);
                
                vesa_draw_rect(indicator_x, indicator_y + indicator_height, 
                              indicator_width, 1, 0x252526);
            }
        }
    }
}