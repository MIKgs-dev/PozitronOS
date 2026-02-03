#ifndef ATA_H
#define ATA_H

#include <stdint.h>

// Типы дисков
#define ATA_MASTER   0
#define ATA_SLAVE    1
#define ATA_SEC_MASTER 2
#define ATA_SEC_SLAVE  3

// Инициализация ATA
void ata_init(void);

// Автоматическое определение дисков
void ata_scan(void);

// Проверка наличия диска
int ata_disk_present(uint8_t drive);

// Чтение секторов
int ata_read_sectors(uint8_t drive, uint32_t lba, uint8_t count, void* buffer);

// Запись секторов
int ata_write_sectors(uint8_t drive, uint32_t lba, uint8_t count, void* buffer);

// Получить информацию о диске
const char* ata_get_model(uint8_t drive);
uint32_t ata_get_sectors(uint8_t drive);

#endif