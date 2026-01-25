#include "drivers/cmos.h"
#include "kernel/ports.h"
#include "drivers/serial.h"
#include <stddef.h>

// Чтение регистра CMOS
uint8_t cmos_read_register(uint8_t reg) {
    // Запрещаем прерывания на время чтения CMOS
    asm volatile ("cli");
    
    // Выбираем регистр
    outb(CMOS_ADDRESS, reg | 0x80);  // Бит NMI отключаем
    // Небольшая задержка для стабильности
    asm volatile ("nop; nop; nop; nop;");
    
    // Читаем данные
    uint8_t data = inb(CMOS_DATA);
    
    // Разрешаем прерывания
    asm volatile ("sti");
    
    return data;
}

// Проверка, обновляется ли время в данный момент
uint8_t cmos_is_updating(void) {
    // Проверяем флаг обновления (UIP)
    return cmos_read_register(CMOS_REG_STATUS_A) & CMOS_UIP;
}

// Ожидание завершения обновления времени
void cmos_wait_for_update(void) {
    // Ждём, пока флаг обновления сбросится
    while (cmos_is_updating());
}

// Конвертация BCD в бинарный формат
uint8_t cmos_bcd_to_binary(uint8_t bcd) {
    return (bcd & 0x0F) + ((bcd >> 4) * 10);
}

// Чтение текущего времени и даты из RTC
void cmos_read_datetime(rtc_datetime_t* datetime) {
    if (!datetime) return;
    
    // Ждём завершения обновления
    cmos_wait_for_update();
    
    // Считываем регистры дважды для гарантии стабильности
    uint8_t reg_b = cmos_read_register(CMOS_REG_STATUS_B);
    
    // Читаем компоненты времени
    uint8_t seconds = cmos_read_register(CMOS_REG_SECONDS);
    uint8_t minutes = cmos_read_register(CMOS_REG_MINUTES);
    uint8_t hours = cmos_read_register(CMOS_REG_HOURS);
    uint8_t weekday = cmos_read_register(CMOS_REG_WEEKDAY);
    uint8_t day = cmos_read_register(CMOS_REG_DAY);
    uint8_t month = cmos_read_register(CMOS_REG_MONTH);
    uint8_t year = cmos_read_register(CMOS_REG_YEAR);
    uint8_t century = 0;
    
    // Пытаемся прочитать век (может не поддерживаться)
    uint8_t century_reg = cmos_read_register(CMOS_REG_CENTURY);
    if (century_reg != 0xFF && century_reg != 0x00) {
        century = century_reg;
    } else {
        // Если век не поддерживается, предполагаем 21-й век для современных систем
        century = 20; // Для годов 00-79 будет 2000-2079
    }
    
    // Проверяем формат времени (12/24 часа)
    uint8_t is_24h = (reg_b & 0x02) ? 1 : 0;
    uint8_t is_pm = 0;
    
    // Если 12-часовой формат, проверяем PM/AM
    if (!is_24h && (hours & 0x80)) {
        is_pm = 1;
        hours &= 0x7F;  // Убираем PM бит
    }
    
    // Если данные в BCD формате, конвертируем
    if (!(reg_b & 0x04)) {
        seconds = cmos_bcd_to_binary(seconds);
        minutes = cmos_bcd_to_binary(minutes);
        hours = cmos_bcd_to_binary(hours);
        day = cmos_bcd_to_binary(day);
        month = cmos_bcd_to_binary(month);
        year = cmos_bcd_to_binary(year);
        if (century != 0 && century != 20) {
            century = cmos_bcd_to_binary(century);
        }
    }
    
    // Вычисляем полный год (4 цифры)
    uint16_t full_year;
    if (century > 0) {
        full_year = century * 100 + year;
    } else {
        // Если век не указан, используем эвристику
        if (year >= 80) {
            full_year = 1900 + year;  // 1980-1999
        } else {
            full_year = 2000 + year;  // 2000-2079
        }
    }
    
    // Заполняем структуру
    datetime->seconds = seconds;
    datetime->minutes = minutes;
    datetime->hours = hours;
    datetime->weekday = weekday;
    datetime->day = day;
    datetime->month = month;
    datetime->year = full_year;
    datetime->is_pm = is_pm;
    datetime->is_24h = is_24h;
    
    // Корректировка для 12-часового формата PM
    if (!is_24h && is_pm && hours < 12) {
        datetime->hours = hours + 12;
    } else if (!is_24h && !is_pm && hours == 12) {
        datetime->hours = 0;  // Полночь в 12-часовом формате AM
    }
}

// Получение количества секунд с полуночи
uint32_t cmos_get_seconds_since_midnight(void) {
    rtc_datetime_t datetime;
    cmos_read_datetime(&datetime);
    return datetime.hours * 3600 + datetime.minutes * 60 + datetime.seconds;
}

// Получение строки дня недели
char* cmos_get_weekday_string(uint8_t weekday) {
    static char* weekdays[] = {
        "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
    };
    
    if (weekday >= 1 && weekday <= 7) {
        return weekdays[weekday - 1];
    }
    return "???";
}

// Упрощённый timestamp (только для сравнения интервалов)
uint32_t cmos_get_timestamp(void) {
    rtc_datetime_t datetime;
    cmos_read_datetime(&datetime);
    
    // Упрощённый расчёт (не учитывает високосные годы и разное количество дней в месяцах)
    uint32_t days = datetime.day - 1;
    
    // Приблизительное количество дней в предыдущих месяцах
    static uint8_t days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    for (uint8_t m = 1; m < datetime.month; m++) {
        days += days_in_month[m - 1];
    }
    
    // Годы (с 2000 года)
    uint32_t years = datetime.year - 2000;
    
    // Добавляем дни за годы
    days += years * 365;
    
    // Конвертируем всё в секунды
    return days * 86400 + datetime.hours * 3600 + datetime.minutes * 60 + datetime.seconds;
}

// Инициализация (в основном для отладки)
void cmos_init(void) {
    serial_puts("[CMOS] RTC Driver initialized\n");
    
    // Проверяем доступность RTC
    rtc_datetime_t datetime;
    cmos_read_datetime(&datetime);
    
    serial_puts("[CMOS] Current RTC datetime: ");
    if (datetime.hours < 10) serial_puts("0");
    serial_puts_num(datetime.hours);
    serial_puts(":");
    if (datetime.minutes < 10) serial_puts("0");
    serial_puts_num(datetime.minutes);
    serial_puts(":");
    if (datetime.seconds < 10) serial_puts("0");
    serial_puts_num(datetime.seconds);
    serial_puts(" ");
    serial_puts(cmos_get_weekday_string(datetime.weekday));
    serial_puts(" ");
    if (datetime.day < 10) serial_puts("0");
    serial_puts_num(datetime.day);
    serial_puts(".");
    if (datetime.month < 10) serial_puts("0");
    serial_puts_num(datetime.month);
    serial_puts(".");
    serial_puts_num(datetime.year);
    
    if (!datetime.is_24h) {
        serial_puts(datetime.is_pm ? " PM" : " AM");
    }
    serial_puts("\n");
}