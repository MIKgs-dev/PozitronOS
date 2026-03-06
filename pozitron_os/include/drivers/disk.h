#ifndef DISK_H
#define DISK_H

#include <stdint.h>

#define MAX_DISKS 8
#define DISK_NAME_LEN 64

typedef enum {
    DISK_TYPE_AHCI,
    DISK_TYPE_ATA_PIO,
    DISK_TYPE_UNKNOWN
} disk_type_t;

typedef struct {
    uint8_t present;
    disk_type_t type;
    uint8_t bus;          // для AHCI: порт, для ATA PIO: 0/1 (primary/secondary)
    uint8_t device;       // для AHCI: 0, для ATA PIO: 0/1 (master/slave)
    uint64_t total_sectors;
    uint32_t sector_size; // обычно 512
    uint32_t size_mb;     // total_sectors * sector_size / 1048576
    char model[41];
    uint8_t removable;
} disk_info_t;

// Инициализация диск-менеджера
void disk_init(void);

// Регистрация нового диска (вызывается из драйвера)
int disk_register(disk_type_t type, uint8_t bus, uint8_t device,
                  uint64_t total_sectors, const char* model, uint8_t removable);

// Получить общее количество дисков
int disk_get_count(void);

// Получить информацию о диске по индексу
const disk_info_t* disk_get_info(int index);

// Чтение секторов с диска (индекс диска, LBA, количество, буфер)
int disk_read(int disk_num, uint64_t lba, uint32_t count, void* buffer);

// Запись секторов на диск
int disk_write(int disk_num, uint64_t lba, uint32_t count, const void* buffer);

#endif