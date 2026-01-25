#ifndef CMOS_H
#define CMOS_H

#include "../kernel/types.h"

// Регистры CMOS
#define CMOS_ADDRESS 0x70
#define CMOS_DATA    0x71

// RTC регистры
#define CMOS_REG_SECONDS     0x00
#define CMOS_REG_MINUTES     0x02
#define CMOS_REG_HOURS       0x04
#define CMOS_REG_WEEKDAY     0x06
#define CMOS_REG_DAY         0x07
#define CMOS_REG_MONTH       0x08
#define CMOS_REG_YEAR        0x09
#define CMOS_REG_CENTURY     0x32
#define CMOS_REG_STATUS_A    0x0A
#define CMOS_REG_STATUS_B    0x0B
#define CMOS_REG_STATUS_C    0x0C

// Флаги статуса
#define CMOS_UIP 0x80

// Структура времени и даты
typedef struct {
    uint8_t seconds;
    uint8_t minutes;
    uint8_t hours;
    uint8_t weekday;
    uint8_t day;
    uint8_t month;
    uint16_t year;
    uint8_t is_pm;
    uint8_t is_24h;
} rtc_datetime_t;

// Функции
void cmos_init(void);
uint8_t cmos_read_register(uint8_t reg);
void cmos_wait_for_update(void);
uint8_t cmos_bcd_to_binary(uint8_t bcd);
void cmos_read_datetime(rtc_datetime_t* datetime);
uint8_t cmos_is_updating(void);
uint32_t cmos_get_seconds_since_midnight(void);
char* cmos_get_weekday_string(uint8_t weekday);
uint32_t cmos_get_timestamp(void);

#endif