#include "drivers/ata.h"
#include "drivers/ports.h"
#include "drivers/serial.h"
#include "kernel/memory.h"
#include <stddef.h>

// ============ КОНСТАНТЫ ATA ============
// Primary IDE controller
#define ATA_PRIMARY_DATA     0x1F0
#define ATA_PRIMARY_ERROR    0x1F1
#define ATA_PRIMARY_SECT_CNT 0x1F2
#define ATA_PRIMARY_LBA_LOW  0x1F3
#define ATA_PRIMARY_LBA_MID  0x1F4
#define ATA_PRIMARY_LBA_HIGH 0x1F5
#define ATA_PRIMARY_DRIVE    0x1F6
#define ATA_PRIMARY_STATUS   0x1F7
#define ATA_PRIMARY_COMMAND  0x1F7
#define ATA_PRIMARY_ALT      0x3F6

// Secondary IDE controller
#define ATA_SECONDARY_DATA   0x170
#define ATA_SECONDARY_ERROR  0x171
#define ATA_SECONDARY_SECT_CNT 0x172
#define ATA_SECONDARY_LBA_LOW  0x173
#define ATA_SECONDARY_LBA_MID  0x174
#define ATA_SECONDARY_LBA_HIGH 0x175
#define ATA_SECONDARY_DRIVE    0x176
#define ATA_SECONDARY_STATUS   0x177
#define ATA_SECONDARY_COMMAND  0x177
#define ATA_SECONDARY_ALT      0x376

// Бит статуса
#define ATA_SR_BSY      0x80    // Бит занятости
#define ATA_SR_DRDY     0x40    // Готовность устройства
#define ATA_SR_DF       0x20    // Ошибка устройства
#define ATA_SR_DSC      0x10    // Поиск завершен
#define ATA_SR_DRQ      0x08    // Запрос данных
#define ATA_SR_CORR     0x04    // Исправленные данные
#define ATA_SR_IDX      0x02    // Индекс
#define ATA_SR_ERR      0x01    // Ошибка

// Команды
#define ATA_CMD_READ_SECTORS     0x20
#define ATA_CMD_WRITE_SECTORS    0x30
#define ATA_CMD_IDENTIFY         0xEC

// Структура для хранения информации о диске
typedef struct {
    uint16_t base;
    uint8_t  type;          // 0xA0 = master, 0xB0 = slave
    uint8_t  present;       // 1 = диск присутствует
    char     model[41];     // Модель диска
    uint32_t sectors;       // Количество секторов
} ata_disk_t;

// Массив дисков (4 возможных: Primary Master/Slave, Secondary Master/Slave)
static ata_disk_t disks[4] = {
    {ATA_PRIMARY_DATA,   0xA0, 0, "", 0},   // Primary Master
    {ATA_PRIMARY_DATA,   0xB0, 0, "", 0},   // Primary Slave
    {ATA_SECONDARY_DATA, 0xA0, 0, "", 0},   // Secondary Master
    {ATA_SECONDARY_DATA, 0xB0, 0, "", 0},   // Secondary Slave
};

// Имена дисков для логов
static const char* disk_names[4] = {
    "Primary Master",
    "Primary Slave",
    "Secondary Master",
    "Secondary Slave"
};

// ============ ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ ============

// Простая задержка
static void ata_delay(uint32_t ms) {
    // Простой цикл ожидания
    for (volatile uint32_t i = 0; i < ms * 1000; i++) {
        asm volatile("nop");
    }
}

// Ожидание готовности диска
static int ata_wait_ready(uint16_t base) {
    uint8_t status;
    int timeout = 100000;  // 100,000 попыток
    
    while (timeout--) {
        status = inb(base + 7);  // STATUS регистр
        
        // Если BSY=0 и DRDY=1, диск готов
        if (!(status & ATA_SR_BSY) && (status & ATA_SR_DRDY)) {
            return 0;
        }
    }
    
    return -1;  // Таймаут
}

// Ожидание запроса данных
static int ata_wait_drq(uint16_t base) {
    uint8_t status;
    int timeout = 100000;
    
    while (timeout--) {
        status = inb(base + 7);
        
        // Если BSY=0 и DRQ=1, можно читать/писать данные
        if (!(status & ATA_SR_BSY) && (status & ATA_SR_DRQ)) {
            return 0;
        }
        
        // Если ошибка
        if (status & ATA_SR_ERR) {
            return -1;
        }
    }
    
    return -1;
}

// Выбор диска
static void ata_select_disk(uint16_t base, uint8_t type) {
    outb(base + 6, type);  // DRIVE/HEAD регистр
    // Читаем статус 4 раза для задержки
    for (int i = 0; i < 4; i++) {
        inb(base + 7);
    }
}

// Определение одного диска
static int ata_probe_disk(int index) {
    uint16_t base = disks[index].base;
    uint8_t type = disks[index].type;
    
    serial_puts("[ATA] Probing ");
    serial_puts(disk_names[index]);
    serial_puts(" (0x");
    serial_puts_num_hex(base);
    serial_puts(":");
    serial_puts((type == 0xA0) ? "master" : "slave");
    serial_puts(")... ");
    
    // Выбираем диск
    ata_select_disk(base, type);
    
    // Ждём готовности
    if (ata_wait_ready(base) < 0) {
        serial_puts("no response\n");
        return 0;
    }
    
    // Посылаем команду IDENTIFY
    outb(base + 7, ATA_CMD_IDENTIFY);
    ata_delay(1);
    
    // Проверяем ответ
    uint8_t status = inb(base + 7);
    if (status == 0) {
        serial_puts("no device\n");
        return 0;
    }
    
    // Ждём DRQ
    if (ata_wait_drq(base) < 0) {
        serial_puts("not ATA\n");
        return 0;
    }
    
    // Читаем данные IDENTIFY (256 слов = 512 байт)
    uint16_t identify[256];
    for (int i = 0; i < 256; i++) {
        identify[i] = inw(base);
    }
    
    // Извлекаем информацию о модели
    char model[41];
    for (int i = 0; i < 20; i++) {
        model[i*2] = identify[27+i] >> 8;
        model[i*2+1] = identify[27+i] & 0xFF;
    }
    model[40] = '\0';
    
    // Убираем лишние пробелы в конце
    for (int i = 39; i >= 0 && model[i] == ' '; i--) {
        model[i] = '\0';
    }
    
    // Сохраняем информацию
    disks[index].present = 1;
    disks[index].sectors = *(uint32_t*)&identify[60];
    
    // Копируем модель
    int j = 0;
    while (model[j] && j < 40) {
        disks[index].model[j] = model[j];
        j++;
    }
    disks[index].model[j] = '\0';
    
    serial_puts("OK: ");
    serial_puts(disks[index].model);
    serial_puts(" (");
    serial_puts_num(disks[index].sectors);
    serial_puts(" sectors, ");
    serial_puts_num(disks[index].sectors / 2 / 1024); // Примерно в MB
    serial_puts(" MB)\n");
    
    return 1;
}

// ============ ПУБЛИЧНЫЕ ФУНКЦИИ ============

// Инициализация ATA
void ata_init(void) {
    serial_puts("[ATA] Initializing ATA controllers...\n");
    
    // Сбрасываем оба контроллера
    outb(ATA_PRIMARY_ALT, 0x04); // SRST на primary
    ata_delay(5);
    outb(ATA_PRIMARY_ALT, 0x00);
    ata_delay(5);
    
    outb(ATA_SECONDARY_ALT, 0x04); // SRST на secondary
    ata_delay(5);
    outb(ATA_SECONDARY_ALT, 0x00);
    ata_delay(5);
    
    serial_puts("[ATA] Controllers reset\n");
}

// Автоматическое определение дисков
void ata_scan(void) {
    serial_puts("[ATA] Scanning for disks...\n");
    
    int found = 0;
    for (int i = 0; i < 4; i++) {
        if (ata_probe_disk(i)) {
            found++;
        }
    }
    
    serial_puts("[ATA] Found ");
    serial_puts_num(found);
    serial_puts(" disk(s)\n");
}

// Проверка наличия диска
int ata_disk_present(uint8_t drive) {
    if (drive >= 4) return 0;
    return disks[drive].present;
}

// Чтение секторов
int ata_read_sectors(uint8_t drive, uint32_t lba, uint8_t count, void* buffer) {
    if (drive >= 4 || !disks[drive].present || count == 0) {
        serial_puts("[ATA] Invalid disk or count\n");
        return -1;
    }
    
    uint16_t base = disks[drive].base;
    uint8_t type = disks[drive].type;
    
    // Выбираем диск
    ata_select_disk(base, type);
    
    // Ждём готовности
    if (ata_wait_ready(base) < 0) {
        return -1;
    }
    
    // Настраиваем LBA
    outb(base + 2, count);                 // Sector count
    outb(base + 3, lba & 0xFF);            // LBA low
    outb(base + 4, (lba >> 8) & 0xFF);     // LBA mid
    outb(base + 5, (lba >> 16) & 0xFF);    // LBA high
    outb(base + 6, 0xE0 | type | ((lba >> 24) & 0x0F)); // Drive/head
    
    // Отправляем команду чтения
    outb(base + 7, ATA_CMD_READ_SECTORS);
    
    uint16_t* buf = (uint16_t*)buffer;
    
    // Читаем секторы
    for (int s = 0; s < count; s++) {
        // Ждём готовности данных
        if (ata_wait_drq(base) < 0) {
            serial_puts("[ATA] Read error at sector ");
            serial_puts_num(lba + s);
            serial_puts("\n");
            return -1;
        }
        
        // Читаем 256 слов (512 байт)
        for (int i = 0; i < 256; i++) {
            buf[s * 256 + i] = inw(base);
        }
    }
    
    return 0;
}

// Запись секторов
int ata_write_sectors(uint8_t drive, uint32_t lba, uint8_t count, void* buffer) {
    if (drive >= 4 || !disks[drive].present || count == 0) {
        return -1;
    }
    
    uint16_t base = disks[drive].base;
    uint8_t type = disks[drive].type;
    
    ata_select_disk(base, type);
    
    // Ждём готовности
    if (ata_wait_ready(base) < 0) {
        return -1;
    }
    
    // Настраиваем LBA
    outb(base + 2, count);                 // Sector count
    outb(base + 3, lba & 0xFF);            // LBA low
    outb(base + 4, (lba >> 8) & 0xFF);     // LBA mid
    outb(base + 5, (lba >> 16) & 0xFF);    // LBA high
    outb(base + 6, 0xE0 | type | ((lba >> 24) & 0x0F)); // Drive/head
    
    // Команда записи
    outb(base + 7, ATA_CMD_WRITE_SECTORS);
    
    uint16_t* buf = (uint16_t*)buffer;
    
    for (int s = 0; s < count; s++) {
        // Ждём готовности данных
        if (ata_wait_drq(base) < 0) {
            serial_puts("[ATA] Write error at sector ");
            serial_puts_num(lba + s);
            serial_puts("\n");
            return -1;
        }
        
        // Пишем 256 слов
        for (int i = 0; i < 256; i++) {
            outw(base, buf[s * 256 + i]);
        }
        
        // Ждём завершения
        ata_wait_ready(base);
    }
    
    return 0;
}

// Получить информацию о диске
const char* ata_get_model(uint8_t drive) {
    if (drive >= 4 || !disks[drive].present) return "Unknown";
    return disks[drive].model;
}

uint32_t ata_get_sectors(uint8_t drive) {
    if (drive >= 4 || !disks[drive].present) return 0;
    return disks[drive].sectors;
}