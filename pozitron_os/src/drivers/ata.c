#include "drivers/ata.h"
#include "drivers/ports.h"
#include "drivers/serial.h"
#include "kernel/memory.h"
#include "drivers/timer.h"
#include <stddef.h>
#include "lib/string.h"

// ==================== ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ ====================

#define MAX_DISKS 8
#define CACHE_ENTRIES 128
#define QUEUE_SIZE 32

static ata_full_disk_t disks[MAX_DISKS];
static uint8_t disk_count = 0;
static uint8_t ata_initialized = 0;
static uint8_t cache_enabled = 1;

// ==================== ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ ====================

// Получить базовые порты для канала
static uint16_t get_base_port(uint8_t channel) {
    return (channel == 0) ? ATA_PRIMARY_DATA : ATA_SECONDARY_DATA;
}

static uint16_t get_status_port(uint8_t channel) {
    return (channel == 0) ? ATA_PRIMARY_STATUS : ATA_SECONDARY_STATUS;
}

static uint16_t get_alt_status_port(uint8_t channel) {
    return (channel == 0) ? ATA_PRIMARY_ALT_STATUS : ATA_SECONDARY_ALT_STATUS;
}

// Задержка 400ns
static void ata_io_delay(uint16_t port) {
    inb(port);
    inb(port);
    inb(port);
    inb(port);
}

// Задержка в микросекундах (приблизительно)
static void micro_delay(uint32_t us) {
    for (volatile uint32_t i = 0; i < us * 10; i++);
}

// Ожидание с таймаутом
static int ata_wait(uint16_t status_port, uint8_t mask, uint8_t value, uint32_t timeout_ms) {
    uint32_t timeout = timeout_ms * 100;
    
    while (timeout--) {
        uint8_t status = inb(status_port);
        
        if ((status & mask) == value) {
            return 1;
        }
        
        if (status & ATA_STATUS_ERR) {
            return -1;
        }
        
        micro_delay(10);
    }
    
    return 0;
}

// Чтение ошибки с детальным выводом
static uint8_t ata_read_error_detail(uint8_t channel) {
    uint8_t error = inb(get_base_port(channel) + 1);
    
    if (error) {
        serial_puts("[ATA] Error: 0x");
        serial_puts_num_hex(error);
        serial_puts(" (");
        
        if (error & ATA_ERROR_BBK) serial_puts("BadBlock ");
        if (error & ATA_ERROR_UNC) serial_puts("Uncorrectable ");
        if (error & ATA_ERROR_MC) serial_puts("MediaChanged ");
        if (error & ATA_ERROR_IDNF) serial_puts("IDNotFound ");
        if (error & ATA_ERROR_MCR) serial_puts("MediaChangeReq ");
        if (error & ATA_ERROR_ABRT) serial_puts("Aborted ");
        if (error & ATA_ERROR_TK0NF) serial_puts("Track0NotFound ");
        if (error & ATA_ERROR_AMNF) serial_puts("AddrMarkNotFound ");
        
        serial_puts(")\n");
    }
    
    return error;
}

// Выбор устройства для LBA28
static void ata_select_device_lba28(uint8_t channel, uint8_t drive, uint32_t lba) {
    uint8_t head = 0xE0;  // LBA mode, master
    
    if (drive == 1) {
        head = 0xF0;  // LBA mode, slave
    }
    
    head |= ((lba >> 24) & 0x0F);
    
    outb(get_base_port(channel) + 6, head);
    ata_io_delay(get_status_port(channel));
}

// Выбор устройства для LBA48
static void ata_select_device_lba48(uint8_t channel, uint8_t drive, uint64_t lba) {
    uint8_t head = 0x40;  // LBA mode
    
    if (drive == 1) {
        head |= 0x10;  // slave
    }
    
    outb(get_base_port(channel) + 6, head);
    ata_io_delay(get_status_port(channel));
}

// ==================== ОСНОВНЫЕ ОПЕРАЦИИ ====================

// Базовое чтение секторов с поддержкой LBA48
static int ata_raw_read_sectors(ata_full_disk_t* disk, uint64_t lba, uint16_t count, void* buffer) {
    if (!disk->present || count == 0 || count > 65535) return 0;
    
    uint16_t base = get_base_port(disk->channel);
    uint16_t status_port = get_status_port(disk->channel);
    
    // Проверяем границы
    if (lba >= disk->total_sectors) {
        serial_puts("[ATA] Error: LBA out of range\n");
        return 0;
    }
    
    if (lba + count > disk->total_sectors) {
        serial_puts("[ATA] Error: Read beyond disk end\n");
        return 0;
    }
    
    uint8_t use_lba48 = 0;
    
    // Определяем нужно ли использовать LBA48
    if (disk->lba48_supported && (lba > 0x0FFFFFFF || count > 256)) {
        use_lba48 = 1;
    }
    
    if (use_lba48) {
        // LBA48 режим
        ata_select_device_lba48(disk->channel, disk->drive, lba);
        
        // Устанавливаем параметры LBA48
        outb(base + 2, (count >> 8) & 0xFF);      // Count high
        outb(base + 3, (lba >> 24) & 0xFF);       // LBA 24-31
        outb(base + 4, (lba >> 32) & 0xFF);       // LBA 32-39
        outb(base + 5, (lba >> 40) & 0xFF);       // LBA 40-47
        
        outb(base + 2, count & 0xFF);             // Count low
        outb(base + 3, lba & 0xFF);               // LBA 0-7
        outb(base + 4, (lba >> 8) & 0xFF);        // LBA 8-15
        outb(base + 5, (lba >> 16) & 0xFF);       // LBA 16-23
        
        outb(base + 7, ATA_CMD_READ_SECTORS_EXT);
    } else {
        // LBA28 режим
        ata_select_device_lba28(disk->channel, disk->drive, (uint32_t)lba);
        
        // Устанавливаем параметры LBA28
        outb(base + 2, count & 0xFF);             // Count
        outb(base + 3, lba & 0xFF);               // LBA low
        outb(base + 4, (lba >> 8) & 0xFF);        // LBA mid
        outb(base + 5, (lba >> 16) & 0xFF);       // LBA high
        
        outb(base + 7, ATA_CMD_READ_SECTORS);
    }
    
    uint16_t* buf = (uint16_t*)buffer;
    
    for (uint16_t s = 0; s < count; s++) {
        // Ждем готовности
        int status = ata_wait(status_port, 
                             ATA_STATUS_BSY | ATA_STATUS_DRQ, 
                             ATA_STATUS_DRQ, 5000);
        
        if (status <= 0) {
            if (status < 0) ata_read_error_detail(disk->channel);
            serial_puts("[ATA] Read timeout/error at sector ");
            serial_puts_num((uint32_t)lba + s);
            serial_puts("\n");
            return 0;
        }
        
        // Читаем сектор (256 слов = 512 байт)
        for (int i = 0; i < 256; i++) {
            buf[s * 256 + i] = inw(base);
        }
        
        // Короткая задержка между секторами
        micro_delay(1);
    }
    
    disk->read_operations++;
    disk->read_bytes += count * disk->sector_size;
    return 1;
}

// Базовая запись секторов с поддержкой LBA48
static int ata_raw_write_sectors(ata_full_disk_t* disk, uint64_t lba, uint16_t count, const void* buffer) {
    if (!disk->present || count == 0 || count > 65535) return 0;
    
    uint16_t base = get_base_port(disk->channel);
    uint16_t status_port = get_status_port(disk->channel);
    
    // Проверяем границы
    if (lba >= disk->total_sectors) {
        serial_puts("[ATA] Error: LBA out of range\n");
        return 0;
    }
    
    if (lba + count > disk->total_sectors) {
        serial_puts("[ATA] Error: Write beyond disk end\n");
        return 0;
    }
    
    uint8_t use_lba48 = 0;
    
    if (disk->lba48_supported && (lba > 0x0FFFFFFF || count > 256)) {
        use_lba48 = 1;
    }
    
    if (use_lba48) {
        // LBA48 режим
        ata_select_device_lba48(disk->channel, disk->drive, lba);
        
        outb(base + 2, (count >> 8) & 0xFF);
        outb(base + 3, (lba >> 24) & 0xFF);
        outb(base + 4, (lba >> 32) & 0xFF);
        outb(base + 5, (lba >> 40) & 0xFF);
        
        outb(base + 2, count & 0xFF);
        outb(base + 3, lba & 0xFF);
        outb(base + 4, (lba >> 8) & 0xFF);
        outb(base + 5, (lba >> 16) & 0xFF);
        
        outb(base + 7, ATA_CMD_WRITE_SECTORS_EXT);
    } else {
        // LBA28 режим
        ata_select_device_lba28(disk->channel, disk->drive, (uint32_t)lba);
        
        outb(base + 2, count & 0xFF);
        outb(base + 3, lba & 0xFF);
        outb(base + 4, (lba >> 8) & 0xFF);
        outb(base + 5, (lba >> 16) & 0xFF);
        
        outb(base + 7, ATA_CMD_WRITE_SECTORS);
    }
    
    const uint16_t* buf = (const uint16_t*)buffer;
    
    for (uint16_t s = 0; s < count; s++) {
        // Ждем готовности
        int status = ata_wait(status_port,
                             ATA_STATUS_BSY | ATA_STATUS_DRQ,
                             ATA_STATUS_DRQ, 5000);
        
        if (status <= 0) {
            if (status < 0) ata_read_error_detail(disk->channel);
            serial_puts("[ATA] Write timeout/error at sector ");
            serial_puts_num((uint32_t)lba + s);
            serial_puts("\n");
            return 0;
        }
        
        // Пишем сектор
        for (int i = 0; i < 256; i++) {
            outw(base, buf[s * 256 + i]);
        }
        
        // Ждем завершения записи
        if (!ata_wait(status_port, ATA_STATUS_BSY, 0, 10000)) {
            serial_puts("[ATA] Write completion timeout\n");
            return 0;
        }
        
        micro_delay(1);
    }
    
    // Сброс кэша
    if (use_lba48) {
        outb(base + 7, ATA_CMD_FLUSH_CACHE_EXT);
    } else {
        outb(base + 7, ATA_CMD_FLUSH_CACHE);
    }
    
    ata_wait(status_port, ATA_STATUS_BSY, 0, 5000);
    
    disk->write_operations++;
    disk->write_bytes += count * disk->sector_size;
    return 1;
}

// ==================== КЭШИРОВАНИЕ ====================

// Инициализация кэша для диска
static void init_cache_for_disk(ata_full_disk_t* disk) {
    if (!disk || !cache_enabled) return;
    
    disk->cache_entries = CACHE_ENTRIES;
    disk->cache = (cache_entry_t*)kmalloc(sizeof(cache_entry_t) * CACHE_ENTRIES);
    disk->cache_hits = 0;
    disk->cache_misses = 0;
    
    if (disk->cache) {
        for (uint32_t i = 0; i < CACHE_ENTRIES; i++) {
            disk->cache[i].lba_low = 0xFFFFFFFF;
            disk->cache[i].lba_high = 0xFFFFFFFF;
            disk->cache[i].dirty = 0;
            disk->cache[i].timestamp = 0;
            disk->cache[i].data = (uint8_t*)kmalloc(512);
        }
        
        serial_puts("[ATA] Cache initialized: ");
        serial_puts_num(disk->cache_entries);
        serial_puts(" entries\n");
    }
}

// Поиск в кэше
static cache_entry_t* find_in_cache(ata_full_disk_t* disk, uint64_t lba) {
    if (!disk->cache) return NULL;
    
    uint32_t lba_low = (uint32_t)lba;
    uint32_t lba_high = (uint32_t)(lba >> 32);
    
    for (uint32_t i = 0; i < disk->cache_entries; i++) {
        if (disk->cache[i].lba_low == lba_low && 
            disk->cache[i].lba_high == lba_high) {
            disk->cache[i].timestamp = timer_get_ticks();
            disk->cache_hits++;
            return &disk->cache[i];
        }
    }
    
    disk->cache_misses++;
    return NULL;
}

// Получение слота кэша (LRU)
static cache_entry_t* get_cache_slot(ata_full_disk_t* disk) {
    for (uint32_t i = 0; i < disk->cache_entries; i++) {
        if (disk->cache[i].lba_low == 0xFFFFFFFF && 
            disk->cache[i].lba_high == 0xFFFFFFFF) {
            return &disk->cache[i];
        }
    }
    
    uint64_t oldest = 0xFFFFFFFFFFFFFFFF;
    uint32_t lru_index = 0;
    
    for (uint32_t i = 0; i < disk->cache_entries; i++) {
        if (disk->cache[i].timestamp < oldest) {
            oldest = disk->cache[i].timestamp;
            lru_index = i;
        }
    }
    
    if (disk->cache[lru_index].dirty) {
        uint64_t old_lba = ((uint64_t)disk->cache[lru_index].lba_high << 32) | 
                          disk->cache[lru_index].lba_low;
        
        uint8_t old_cache = cache_enabled;
        cache_enabled = 0;
        
        if (!ata_raw_write_sectors(disk, old_lba, 1, disk->cache[lru_index].data)) {
            serial_puts("[ATA] Warning: Failed to flush dirty cache\n");
        }
        
        cache_enabled = old_cache;
        disk->cache[lru_index].dirty = 0;
    }
    
    return &disk->cache[lru_index];
}

// ==================== РАСШИРЕННЫЕ ОПЕРАЦИИ ====================

// Чтение с кэшированием
int ata_read_cached(uint8_t disk_num, uint64_t lba, uint32_t count, void* buffer) {
    if (disk_num >= disk_count) return 0;
    
    ata_full_disk_t* disk = &disks[disk_num];
    uint8_t* buf = (uint8_t*)buffer;
    
    for (uint32_t s = 0; s < count; s++) {
        uint64_t current_lba = lba + s;
        
        cache_entry_t* cached = find_in_cache(disk, current_lba);
        if (cached) {
            memcpy(buf + s * 512, cached->data, 512);
            continue;
        }
        
        if (!ata_raw_read_sectors(disk, current_lba, 1, buf + s * 512)) {
            return 0;
        }
        
        if (cache_enabled && disk->cache) {
            cache_entry_t* slot = get_cache_slot(disk);
            if (slot) {
                slot->lba_low = (uint32_t)current_lba;
                slot->lba_high = (uint32_t)(current_lba >> 32);
                slot->dirty = 0;
                slot->timestamp = timer_get_ticks();
                memcpy(slot->data, buf + s * 512, 512);
            }
        }
    }
    
    return 1;
}

// Запись с кэшированием
int ata_write_cached(uint8_t disk_num, uint64_t lba, uint32_t count, const void* buffer) {
    if (disk_num >= disk_count) return 0;
    
    ata_full_disk_t* disk = &disks[disk_num];
    const uint8_t* buf = (const uint8_t*)buffer;
    
    for (uint32_t s = 0; s < count; s++) {
        uint64_t current_lba = lba + s;
        
        cache_entry_t* cached = find_in_cache(disk, current_lba);
        if (cached) {
            memcpy(cached->data, buf + s * 512, 512);
            cached->dirty = 1;
            cached->timestamp = timer_get_ticks();
        } else if (cache_enabled && disk->cache) {
            cache_entry_t* slot = get_cache_slot(disk);
            if (slot) {
                slot->lba_low = (uint32_t)current_lba;
                slot->lba_high = (uint32_t)(current_lba >> 32);
                slot->dirty = 1;
                slot->timestamp = timer_get_ticks();
                memcpy(slot->data, buf + s * 512, 512);
            }
        }
        
        if (!ata_raw_write_sectors(disk, current_lba, 1, buf + s * 512)) {
            return 0;
        }
        
        if (cached) {
            cached->dirty = 0;
        }
    }
    
    return 1;
}

// ==================== ИНИЦИАЛИЗАЦИЯ ====================

void ata_enhanced_init(void) {
    if (ata_initialized) return;
    
    serial_puts("\n[ATA] Enhanced ATA Driver Initializing...\n");
    
    memset(disks, 0, sizeof(disks));
    disk_count = 0;
    
    for (int channel = 0; channel < 2; channel++) {
        for (int drive = 0; drive < 2; drive++) {
            uint16_t base = get_base_port(channel);
            uint16_t status_port = get_status_port(channel);
            
            // Выбираем устройство
            outb(base + 6, 0xA0 | (drive << 4));
            micro_delay(100);
            
            // Проверяем наличие
            outb(base + 2, 0x00);
            outb(base + 3, 0x00);
            outb(base + 4, 0x00);
            outb(base + 5, 0x00);
            
            outb(base + 7, ATA_CMD_IDENTIFY);
            micro_delay(100);
            
            uint8_t status = inb(status_port);
            
            if (status == 0) {
                continue;  // Нет устройства
            }
            
            // Ждем пока устройство не перестанет быть занятым
            int timeout = 10000;
            while (timeout-- > 0 && (inb(status_port) & ATA_STATUS_BSY)) {
                micro_delay(10);
            }
            
            if (timeout <= 0) {
                continue;  // Таймаут
            }
            
            // Проверяем наличие ошибки
            if (inb(status_port) & ATA_STATUS_ERR) {
                continue;  // Не поддерживает IDENTIFY
            }
            
            // Проверяем готовность данных
            if (!(inb(status_port) & ATA_STATUS_DRQ)) {
                continue;
            }
            
            // Устройство обнаружено
            if (disk_count >= MAX_DISKS) {
                serial_puts("[ATA] Warning: Maximum disks reached\n");
                break;
            }
            
            ata_full_disk_t* disk = &disks[disk_count];
            disk->present = 1;
            disk->channel = channel;
            disk->drive = drive;
            disk->state = ATA_STATE_READY;
            disk->type = ATA_PATA;
            
            // Читаем данные IDENTIFY
            uint16_t data[256];
            for (int i = 0; i < 256; i++) {
                data[i] = inw(base);
            }
            
            // Проверяем поддержку LBA48
            disk->lba48_supported = (data[83] & (1 << 10)) ? 1 : 0;
            
            // Размер диска
            if (disk->lba48_supported) {
                disk->total_sectors = ((uint64_t)data[103] << 48) |
                                     ((uint64_t)data[102] << 32) |
                                     ((uint64_t)data[101] << 16) |
                                     data[100];
            } else {
                disk->total_sectors = (data[61] << 16) | data[60];
            }
            
            disk->sector_size = 512;
            
            // Модель (40 символов)
            for (int i = 0; i < 20; i++) {
                disk->model[i*2] = (data[27+i] >> 8) & 0xFF;
                disk->model[i*2+1] = data[27+i] & 0xFF;
            }
            disk->model[40] = '\0';
            
            // Серийный номер (20 символов)
            for (int i = 0; i < 10; i++) {
                disk->serial[i*2] = (data[10+i] >> 8) & 0xFF;
                disk->serial[i*2+1] = data[10+i] & 0xFF;
            }
            disk->serial[20] = '\0';
            
            // Прошивка (8 символов)
            for (int i = 0; i < 4; i++) {
                disk->firmware[i*2] = (data[23+i] >> 8) & 0xFF;
                disk->firmware[i*2+1] = data[23+i] & 0xFF;
            }
            disk->firmware[8] = '\0';
            
            // Убираем пробелы
            int len = 40;
            while (len > 0 && disk->model[len-1] == ' ') {
                disk->model[--len] = '\0';
            }
            
            len = 20;
            while (len > 0 && disk->serial[len-1] == ' ') {
                disk->serial[--len] = '\0';
            }
            
            len = 8;
            while (len > 0 && disk->firmware[len-1] == ' ') {
                disk->firmware[--len] = '\0';
            }
            
            serial_puts("[ATA] Found ");
            serial_puts(channel == 0 ? "Primary" : "Secondary");
            serial_puts(drive == 0 ? " Master: " : " Slave: ");
            serial_puts(disk->model);
            serial_puts(" (");
            serial_puts_num((disk->total_sectors * disk->sector_size) / (1024 * 1024));
            serial_puts(" MB) ");
            if (disk->lba48_supported) {
                serial_puts("[LBA48]");
            } else {
                serial_puts("[LBA28]");
            }
            serial_puts("\n");
            
            // Инициализируем кэш
            init_cache_for_disk(disk);
            
            disk_count++;
        }
    }
    
    serial_puts("[ATA] Enhanced driver ready. Found ");
    serial_puts_num(disk_count);
    serial_puts(" disk(s)\n");
    
    ata_initialized = 1;
}

// ==================== УТИЛИТЫ ====================

void ata_flush_cache(uint8_t disk_num) {
    if (disk_num >= disk_count) return;
    
    ata_full_disk_t* disk = &disks[disk_num];
    
    if (!disk->cache) return;
    
    for (uint32_t i = 0; i < disk->cache_entries; i++) {
        if (disk->cache[i].dirty && disk->cache[i].lba_low != 0xFFFFFFFF) {
            uint64_t lba = ((uint64_t)disk->cache[i].lba_high << 32) | 
                          disk->cache[i].lba_low;
            ata_raw_write_sectors(disk, lba, 1, disk->cache[i].data);
            disk->cache[i].dirty = 0;
        }
    }
    
    serial_puts("[ATA] Cache flushed for disk ");
    serial_puts_num(disk_num);
    serial_puts("\n");
}

void ata_cache_stats(uint8_t disk_num) {
    if (disk_num >= disk_count) return;
    
    ata_full_disk_t* disk = &disks[disk_num];
    
    serial_puts("[ATA] Cache stats for disk ");
    serial_puts_num(disk_num);
    serial_puts(":\n");
    serial_puts("  Hits: ");
    serial_puts_num(disk->cache_hits);
    serial_puts("\n");
    serial_puts("  Misses: ");
    serial_puts_num(disk->cache_misses);
    serial_puts("\n");
    
    uint32_t total = disk->cache_hits + disk->cache_misses;
    if (total > 0) {
        uint32_t hit_rate = (disk->cache_hits * 100) / total;
        serial_puts("  Hit rate: ");
        serial_puts_num(hit_rate);
        serial_puts("%\n");
    }
}

// Безопасный тест записи (использует безопасные сектора)
void ata_safe_test(uint8_t disk_num) {
    if (disk_num >= disk_count) return;
    
    serial_puts("\n[ATA] Starting safe test for disk ");
    serial_puts_num(disk_num);
    serial_puts("\n");
    
    ata_full_disk_t* disk = &disks[disk_num];
    
    // Выбираем безопасный сектор (сектор 1000)
    uint64_t test_sector = 1000;
    
    if (test_sector >= disk->total_sectors) {
        serial_puts("[ATA] Error: Test sector out of range\n");
        return;
    }
    
    serial_puts("[ATA] Testing sector ");
    serial_puts_num((uint32_t)test_sector);
    serial_puts("\n");
    
    // Читаем оригинальный сектор
    uint8_t* original = (uint8_t*)kmalloc(512);
    uint8_t* test = (uint8_t*)kmalloc(512);
    
    if (!original || !test) {
        serial_puts("[ATA] Out of memory for test\n");
        if (original) kfree(original);
        if (test) kfree(test);
        return;
    }
    
    if (!ata_read_cached(disk_num, test_sector, 1, original)) {
        serial_puts("[ATA] Failed to read original\n");
        kfree(original);
        kfree(test);
        return;
    }
    
    // Создаем тестовые данные
    for (int i = 0; i < 512; i++) {
        test[i] = (i + disk_num) % 256;
    }
    test[510] = 0x55;
    test[511] = 0xAA;
    
    // Записываем
    serial_puts("[ATA] Writing test pattern...\n");
    if (!ata_write_cached(disk_num, test_sector, 1, test)) {
        serial_puts("[ATA] Write failed\n");
        kfree(original);
        kfree(test);
        return;
    }
    
    // Читаем обратно
    uint8_t* verify = (uint8_t*)kmalloc(512);
    if (!verify) {
        serial_puts("[ATA] Out of memory for verify\n");
        kfree(original);
        kfree(test);
        return;
    }
    
    if (!ata_read_cached(disk_num, test_sector, 1, verify)) {
        serial_puts("[ATA] Failed to verify\n");
    } else {
        if (memcmp(test, verify, 512) == 0) {
            serial_puts("[ATA] Verification PASSED\n");
        } else {
            serial_puts("[ATA] Verification FAILED\n");
            
            int errors = 0;
            for (int i = 0; i < 512 && errors < 5; i++) {
                if (test[i] != verify[i]) {
                    serial_puts("[ATA] Diff at byte ");
                    serial_puts_num(i);
                    serial_puts(": expected 0x");
                    serial_puts_num_hex(test[i]);
                    serial_puts(", got 0x");
                    serial_puts_num_hex(verify[i]);
                    serial_puts("\n");
                    errors++;
                }
            }
        }
    }
    
    // Восстанавливаем оригинальные данные
    serial_puts("[ATA] Restoring original data...\n");
    if (!ata_write_cached(disk_num, test_sector, 1, original)) {
        serial_puts("[ATA] Failed to restore\n");
    } else {
        serial_puts("[ATA] Original data restored\n");
    }
    
    // Сбрасываем кэш
    ata_flush_cache(disk_num);
    
    kfree(original);
    kfree(test);
    if (verify) kfree(verify);
    
    serial_puts("[ATA] Safe test complete\n");
}

ata_full_disk_t* ata_get_full_disk(uint8_t disk_num) {
    if (disk_num >= disk_count) return NULL;
    return &disks[disk_num];
}

uint8_t ata_get_disk_count(void) {
    return disk_count;
}

uint64_t ata_get_disk_size(uint8_t disk_num) {
    if (disk_num >= disk_count) return 0;
    return disks[disk_num].total_sectors * disks[disk_num].sector_size;
}

uint32_t ata_get_sector_size(uint8_t disk_num) {
    if (disk_num >= disk_count) return 0;
    return disks[disk_num].sector_size;
}

const char* ata_get_model(uint8_t disk_num) {
    if (disk_num >= disk_count) return "Unknown";
    return disks[disk_num].model;
}

uint8_t ata_is_lba48_supported(uint8_t disk_num) {
    if (disk_num >= disk_count) return 0;
    return disks[disk_num].lba48_supported;
}

// ==================== СОВМЕСТИМЫЕ ФУНКЦИИ ====================

void ata_init(void) {
    ata_enhanced_init();
}

int ata_detect_devices(void) {
    ata_enhanced_init();
    return ata_get_disk_count() > 0;
}

disk_t* ata_get_disk(uint8_t disk_num) {
    static disk_t old_disk;
    
    if (disk_num >= disk_count) return NULL;
    
    ata_full_disk_t* new_disk = &disks[disk_num];
    
    old_disk.device.present = new_disk->present;
    old_disk.device.type = new_disk->type;
    old_disk.device.channel = new_disk->channel;
    old_disk.device.drive = new_disk->drive;
    old_disk.device.signature = new_disk->signature;
    old_disk.device.capabilities = new_disk->capabilities & 0xFFFF;
    old_disk.device.command_sets = new_disk->command_sets & 0xFFFFFFFF;
    old_disk.device.size = new_disk->total_sectors & 0xFFFFFFFF;
    old_disk.device.sector_size = new_disk->sector_size;
    
    int i = 0;
    while (new_disk->model[i] && i < 40) {
        old_disk.device.model[i] = new_disk->model[i];
        i++;
    }
    old_disk.device.model[i] = '\0';
    
    i = 0;
    while (new_disk->serial[i] && i < 20) {
        old_disk.device.serial[i] = new_disk->serial[i];
        i++;
    }
    old_disk.device.serial[i] = '\0';
    
    i = 0;
    while (new_disk->firmware[i] && i < 8) {
        old_disk.device.firmware[i] = new_disk->firmware[i];
        i++;
    }
    old_disk.device.firmware[i] = '\0';
    
    old_disk.initialized = new_disk->present;
    old_disk.total_sectors = new_disk->total_sectors & 0xFFFFFFFF;
    old_disk.total_size_mb = (old_disk.total_sectors * new_disk->sector_size) / (1024 * 1024);
    
    return &old_disk;
}

int ata_read_sectors(disk_t* disk, uint32_t lba, uint8_t sector_count, void* buffer) {
    if (!disk || !disk->initialized) return 0;
    
    for (uint8_t i = 0; i < disk_count; i++) {
        ata_full_disk_t* full_disk = &disks[i];
        if (full_disk->present && 
            full_disk->channel == disk->device.channel &&
            full_disk->drive == disk->device.drive) {
            return ata_raw_read_sectors(full_disk, lba, sector_count, buffer);
        }
    }
    
    return 0;
}

int ata_write_sectors(disk_t* disk, uint32_t lba, uint8_t sector_count, const void* buffer) {
    if (!disk || !disk->initialized) return 0;
    
    for (uint8_t i = 0; i < disk_count; i++) {
        ata_full_disk_t* full_disk = &disks[i];
        if (full_disk->present && 
            full_disk->channel == disk->device.channel &&
            full_disk->drive == disk->device.drive) {
            return ata_raw_write_sectors(full_disk, lba, sector_count, buffer);
        }
    }
    
    return 0;
}

void ata_print_info(void) {
    serial_puts("\n=== ATA DISKS INFORMATION ===\n");
    
    if (disk_count == 0) {
        serial_puts("No ATA disks found\n");
        return;
    }
    
    for (int i = 0; i < disk_count; i++) {
        ata_full_disk_t* disk = &disks[i];
        
        serial_puts("Disk ");
        serial_puts_num(i);
        serial_puts(": ");
        serial_puts(disk->model);
        serial_puts("\n");
        
        serial_puts("  Type: ");
        switch (disk->type) {
            case ATA_PATA: serial_puts("PATA"); break;
            case ATA_SATA: serial_puts("SATA"); break;
            case ATA_ATAPI: serial_puts("ATAPI"); break;
            case ATA_SATAPI: serial_puts("SATAPI"); break;
            default: serial_puts("Unknown"); break;
        }
        serial_puts("\n");
        
        serial_puts("  Channel: ");
        serial_puts(disk->channel == 0 ? "Primary" : "Secondary");
        serial_puts(disk->drive == 0 ? " Master" : " Slave");
        serial_puts("\n");
        
        serial_puts("  LBA: ");
        serial_puts(disk->lba48_supported ? "48-bit" : "28-bit");
        serial_puts("\n");
        
        serial_puts("  Size: ");
        serial_puts_num(disk->total_sectors);
        serial_puts(" sectors (");
        serial_puts_num((disk->total_sectors * disk->sector_size) / (1024 * 1024));
        serial_puts(" MB)\n");
        
        serial_puts("  Sector size: ");
        serial_puts_num(disk->sector_size);
        serial_puts(" bytes\n");
        
        if (strlen(disk->serial) > 0) {
            serial_puts("  Serial: ");
            serial_puts(disk->serial);
            serial_puts("\n");
        }
        
        if (strlen(disk->firmware) > 0) {
            serial_puts("  Firmware: ");
            serial_puts(disk->firmware);
            serial_puts("\n");
        }
        
        serial_puts("\n");
    }
    
    serial_puts("==============================\n");
}