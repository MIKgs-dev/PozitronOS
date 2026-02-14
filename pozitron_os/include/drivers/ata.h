#ifndef DRIVERS_ATA_H
#define DRIVERS_ATA_H

#include <stdint.h>

// ==================== ОСНОВНЫЕ КОНСТАНТЫ ====================

// Типы устройств
typedef enum {
    ATA_UNKNOWN = 0,
    ATA_PATA,
    ATA_SATA,
    ATA_ATAPI,
    ATA_SATAPI
} ata_device_type_t;

// Состояния
typedef enum {
    ATA_STATE_READY = 0,
    ATA_STATE_BUSY,
    ATA_STATE_ERROR,
    ATA_STATE_SLEEP,
    ATA_STATE_STANDBY,
    ATA_STATE_IDLE
} ata_state_t;

// Режимы
#define ATA_MODE_PIO          0
#define ATA_MODE_PIO_FLOW     1
#define ATA_MODE_DMA          2
#define ATA_MODE_UDMA         3

// ==================== ПОРТЫ ====================

// Primary Channel
#define ATA_PRIMARY_DATA         0x1F0
#define ATA_PRIMARY_ERROR        0x1F1
#define ATA_PRIMARY_FEATURES     0x1F1
#define ATA_PRIMARY_SECTOR_COUNT 0x1F2
#define ATA_PRIMARY_LBA_LOW      0x1F3
#define ATA_PRIMARY_LBA_MID      0x1F4
#define ATA_PRIMARY_LBA_HIGH     0x1F5
#define ATA_PRIMARY_DRIVE_HEAD   0x1F6
#define ATA_PRIMARY_STATUS       0x1F7
#define ATA_PRIMARY_COMMAND      0x1F7
#define ATA_PRIMARY_ALT_STATUS   0x3F6
#define ATA_PRIMARY_DEVICE_CTL   0x3F6

// Secondary Channel
#define ATA_SECONDARY_DATA        0x170
#define ATA_SECONDARY_ERROR       0x171
#define ATA_SECONDARY_FEATURES    0x171
#define ATA_SECONDARY_SECTOR_COUNT 0x172
#define ATA_SECONDARY_LBA_LOW     0x173
#define ATA_SECONDARY_LBA_MID     0x174
#define ATA_SECONDARY_LBA_HIGH    0x175
#define ATA_SECONDARY_DRIVE_HEAD  0x176
#define ATA_SECONDARY_STATUS      0x177
#define ATA_SECONDARY_COMMAND     0x177
#define ATA_SECONDARY_ALT_STATUS  0x376
#define ATA_SECONDARY_DEVICE_CTL  0x376

// ==================== КОМАНДЫ ====================

#define ATA_CMD_READ_SECTORS          0x20
#define ATA_CMD_READ_SECTORS_EXT      0x24
#define ATA_CMD_READ_DMA              0xC8
#define ATA_CMD_READ_DMA_EXT          0x25
#define ATA_CMD_WRITE_SECTORS         0x30
#define ATA_CMD_WRITE_SECTORS_EXT     0x34
#define ATA_CMD_WRITE_DMA             0xCA
#define ATA_CMD_WRITE_DMA_EXT         0x35
#define ATA_CMD_IDENTIFY              0xEC
#define ATA_CMD_FLUSH_CACHE           0xE7
#define ATA_CMD_FLUSH_CACHE_EXT       0xEA
#define ATA_CMD_PACKET                0xA0  // ATAPI
#define ATA_CMD_SMART                 0xB0
#define ATA_CMD_SLEEP                 0xE6
#define ATA_CMD_STANDBY               0xE2
#define ATA_CMD_IDLE                  0xE3
#define ATA_CMD_CHECK_POWER_MODE      0xE5

// SMART subcommands
#define ATA_SMART_READ_DATA           0xD0
#define ATA_SMART_READ_THRESH         0xD1
#define ATA_SMART_SAVE_ATTR           0xD3
#define ATA_SMART_EXEC_OFFLINE        0xD4
#define ATA_SMART_ENABLE              0xD8
#define ATA_SMART_DISABLE             0xD9
#define ATA_SMART_STATUS              0xDA

// ==================== СТАТУСНЫЕ БИТЫ ====================

#define ATA_STATUS_ERR   (1 << 0)  // Error
#define ATA_STATUS_IDX   (1 << 1)  // Index
#define ATA_STATUS_CORR  (1 << 2)  // Corrected Data
#define ATA_STATUS_DRQ   (1 << 3)  // Data Request Ready
#define ATA_STATUS_SRV   (1 << 4)  // Overlapped Mode Service Request
#define ATA_STATUS_DF    (1 << 5)  // Drive Fault
#define ATA_STATUS_RDY   (1 << 6)  // Ready
#define ATA_STATUS_BSY   (1 << 7)  // Busy

// ==================== БИТЫ ОШИБОК ====================

#define ATA_ERROR_AMNF   (1 << 0)  // Address Mark Not Found
#define ATA_ERROR_TK0NF  (1 << 1)  // Track 0 Not Found
#define ATA_ERROR_ABRT   (1 << 2)  // Command Aborted
#define ATA_ERROR_MCR    (1 << 3)  // Media Change Request
#define ATA_ERROR_IDNF   (1 << 4)  // ID Not Found
#define ATA_ERROR_MC     (1 << 5)  // Media Changed
#define ATA_ERROR_UNC    (1 << 6)  // Uncorrectable Data Error
#define ATA_ERROR_BBK    (1 << 7)  // Bad Block Detected

// ==================== СТРУКТУРЫ ДАННЫХ ====================

// SMART атрибут
typedef struct {
    uint8_t id;
    uint16_t flags;
    uint8_t current;
    uint8_t worst;
    uint8_t raw[6];
    uint8_t reserved;
} smart_attribute_t;

// SMART данные
typedef struct {
    uint16_t version;
    smart_attribute_t attributes[30];
    uint8_t offline_data_collection_status;
    uint8_t self_test_exec_status;
    uint32_t total_time;
    uint8_t error_log_entries;
    uint8_t reserved[116];
    uint16_t checksum;
} smart_data_t;

// Кэш-запись
typedef struct {
    uint32_t lba_low;
    uint32_t lba_high;
    uint8_t dirty;
    uint64_t timestamp;
    uint8_t* data;
} cache_entry_t;

// Структура запроса
typedef struct {
    uint32_t lba_low;
    uint32_t lba_high;
    uint32_t count;
    void* buffer;
    uint8_t is_write;
    uint8_t pending;
    uint8_t retry_count;
} ata_request_t;

// Простая структура (для обратной совместимости)
typedef struct {
    uint8_t present;
    ata_device_type_t type;
    uint8_t channel;
    uint8_t drive;
    uint16_t signature;
    uint16_t capabilities;
    uint32_t command_sets;
    uint32_t size;
    uint32_t sector_size;
    char model[41];
    char serial[21];
    char firmware[9];
} ata_device_t;

// Простая структура диска (для обратной совместимости)
typedef struct {
    ata_device_t device;
    uint8_t initialized;
    uint32_t total_sectors;
    uint32_t total_size_mb;
} disk_t;

// Полная структура диска (расширенная)
typedef struct {
    // Базовые поля
    uint8_t present;
    ata_device_type_t type;
    uint8_t channel;
    uint8_t drive;
    
    // Идентификация
    uint16_t signature;
    uint32_t capabilities;
    uint64_t command_sets;
    uint64_t total_sectors;
    uint32_t sector_size;
    
    // Поддержка LBA48
    uint8_t lba48_supported;
    
    // Строковая информация
    char model[41];
    char serial[21];
    char firmware[9];
    
    // Производительность
    uint8_t pio_mode;
    uint8_t dma_mode;
    uint8_t udma_mode;
    uint32_t max_sectors;
    
    // Кэш устройства
    uint32_t cache_size_bytes;
    uint8_t write_cache_enabled;
    
    // SMART
    uint8_t smart_supported;
    uint8_t smart_enabled;
    smart_data_t smart_data;
    uint16_t temperature;
    
    // Кэширование программное
    cache_entry_t* cache;
    uint32_t cache_entries;
    uint32_t cache_hits;
    uint32_t cache_misses;
    
    // Статистика
    uint64_t read_operations;
    uint64_t write_operations;
    uint64_t read_bytes;
    uint64_t write_bytes;
    uint64_t errors;
    
    // Очередь запросов
    ata_request_t requests[32];
    uint8_t queue_head;
    uint8_t queue_tail;
    uint8_t queue_size;
    
    // Состояние
    ata_state_t state;
    uint8_t locked;
    const char* lock_owner;
    
    // Разделы (MBR)
    struct {
        uint8_t bootable;
        uint8_t type;
        uint32_t start_lba;
        uint32_t sector_count;
    } partitions[4];
} ata_full_disk_t;

// ==================== ФУНКЦИИ ДРАЙВЕРА ====================

// Старые функции (для обратной совместимости)
void ata_init(void);
int ata_detect_devices(void);
disk_t* ata_get_disk(uint8_t disk_num);
int ata_read_sectors(disk_t* disk, uint32_t lba, uint8_t sector_count, void* buffer);
int ata_write_sectors(disk_t* disk, uint32_t lba, uint8_t sector_count, const void* buffer);
void ata_print_info(void);
uint8_t ata_get_disk_count(void);

// Новые функции
void ata_enhanced_init(void);
int ata_read_cached(uint8_t disk_num, uint64_t lba, uint32_t count, void* buffer);
int ata_write_cached(uint8_t disk_num, uint64_t lba, uint32_t count, const void* buffer);
void ata_flush_cache(uint8_t disk_num);
void ata_cache_stats(uint8_t disk_num);
ata_full_disk_t* ata_get_full_disk(uint8_t disk_num);
void ata_safe_test(uint8_t disk_num);

// Утилиты
uint64_t ata_get_disk_size(uint8_t disk_num);
uint32_t ata_get_sector_size(uint8_t disk_num);
const char* ata_get_model(uint8_t disk_num);
uint8_t ata_is_lba48_supported(uint8_t disk_num);

#endif // DRIVERS_ATA_H