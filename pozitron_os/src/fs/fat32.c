#include "fs/fat32.h"
#include "drivers/ata.h"
#include "drivers/serial.h"
#include "kernel/memory.h"
#include "lib/string.h"
#include <stddef.h>

// ==================== ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ ====================

static fat32_fs_t fs;
static uint8_t current_disk = 0;
static uint8_t* sector_buffer = NULL;
static uint32_t* fat_buffer = NULL;
static uint32_t fat_buffer_sector = 0xFFFFFFFF;

// ==================== ВСПОГАТЕЛЬНЫЕ ФУНКЦИИ ====================

// Чтение сектора в буфер
static int read_sector(uint32_t lba) {
    if (!sector_buffer) {
        sector_buffer = (uint8_t*)kmalloc(512);
        if (!sector_buffer) return 0;
    }
    
    return ata_read_cached(current_disk, lba, 1, sector_buffer);
}

// Запись буфера в сектор
static int write_sector(uint32_t lba) {
    if (!sector_buffer) return 0;
    return ata_write_cached(current_disk, lba, 1, sector_buffer);
}

// Чтение FAT таблицы
static uint32_t read_fat_entry(uint32_t cluster) {
    if (cluster < 2 || cluster >= fs.total_clusters + 2) {
        return FAT32_CLUSTER_BAD;
    }
    
    // Вычисляем сектор FAT
    uint32_t fat_sector = fs.fat_start + (cluster * 4) / fs.bytes_per_sector;
    uint32_t fat_offset = (cluster * 4) % fs.bytes_per_sector;
    
    // Если не в кэше - читаем
    if (fat_sector != fat_buffer_sector) {
        if (!read_sector(fat_sector)) return FAT32_CLUSTER_BAD;
        
        if (!fat_buffer) {
            fat_buffer = (uint32_t*)kmalloc(512);
            if (!fat_buffer) return FAT32_CLUSTER_BAD;
        }
        
        memcpy(fat_buffer, sector_buffer, 512);
        fat_buffer_sector = fat_sector;
    }
    
    // Маска для FAT32 (28 бит)
    return fat_buffer[fat_offset / 4] & 0x0FFFFFFF;
}

// Запись в FAT таблицу
static int write_fat_entry(uint32_t cluster, uint32_t value) {
    if (cluster < 2 || cluster >= fs.total_clusters + 2) {
        return 0;
    }
    
    value &= 0x0FFFFFFF; // Оставляем только 28 бит
    
    uint32_t fat_sector = fs.fat_start + (cluster * 4) / fs.bytes_per_sector;
    uint32_t fat_offset = (cluster * 4) % fs.bytes_per_sector;
    
    // Если не в кэше - читаем
    if (fat_sector != fat_buffer_sector) {
        if (!read_sector(fat_sector)) return 0;
        
        if (!fat_buffer) {
            fat_buffer = (uint32_t*)kmalloc(512);
            if (!fat_buffer) return 0;
        }
        
        memcpy(fat_buffer, sector_buffer, 512);
        fat_buffer_sector = fat_sector;
    }
    
    // Записываем значение
    fat_buffer[fat_offset / 4] = (fat_buffer[fat_offset / 4] & 0xF0000000) | value;
    memcpy(sector_buffer, fat_buffer, 512);
    
    // Записываем на диск
    if (!write_sector(fat_sector)) return 0;
    
    // Обновляем вторую FAT (если есть)
    if (fs.fat_count > 1) {
        if (!write_sector(fat_sector + fs.sectors_per_fat)) return 0;
    }
    
    return 1;
}

// Поиск свободного кластера
static uint32_t find_free_cluster(void) {
    // Начинаем с кластера 2 (первые два зарезервированы)
    for (uint32_t cluster = 2; cluster < fs.total_clusters + 2; cluster++) {
        uint32_t entry = read_fat_entry(cluster);
        if (entry == FAT32_CLUSTER_FREE) {
            return cluster;
        }
    }
    return 0; // Нет свободных кластеров
}

// Вычисление LBA для кластера
static uint32_t cluster_to_lba(uint32_t cluster) {
    if (cluster < 2) return 0;
    return fs.data_start + (cluster - 2) * fs.sectors_per_cluster;
}

// Преобразование имени файла в формат 8.3
static void name_to_83(const char* name, char* out_name, char* out_ext) {
    memset(out_name, ' ', 8);
    memset(out_ext, ' ', 3);
    
    const char* dot = strchr(name, '.');
    int name_len = dot ? (dot - name) : strlen(name);
    int ext_len = dot ? strlen(dot + 1) : 0;
    
    // Имя (максимум 8 символов)
    for (int i = 0; i < name_len && i < 8; i++) {
        char c = name[i];
        if (c >= 'a' && c <= 'z') c -= 32; // В верхний регистр
        out_name[i] = c;
    }
    
    // Расширение (максимум 3 символа)
    if (dot) {
        for (int i = 0; i < ext_len && i < 3; i++) {
            char c = dot[i + 1];
            if (c >= 'a' && c <= 'z') c -= 32;
            out_ext[i] = c;
        }
    }
}

// Преобразование из формата 8.3
static void name_from_83(const char* name83, const char* ext83, char* out) {
    int i;
    
    // Копируем имя
    for (i = 0; i < 8 && name83[i] != ' '; i++) {
        out[i] = name83[i];
    }
    
    // Если есть расширение
    if (ext83[0] != ' ') {
        out[i++] = '.';
        for (int j = 0; j < 3 && ext83[j] != ' '; j++) {
            out[i++] = ext83[j];
        }
    }
    
    out[i] = '\0';
}

// Поиск записи в директории
static fat32_dir_entry_t* find_dir_entry(uint32_t dir_cluster, const char* name, 
                                        uint32_t* sector_out, uint32_t* offset_out) {
    char target_name[8], target_ext[3];
    name_to_83(name, target_name, target_ext);
    
    uint32_t current_cluster = dir_cluster;
    uint32_t sector = cluster_to_lba(current_cluster);
    
    while (1) {
        for (uint32_t s = 0; s < fs.sectors_per_cluster; s++) {
            if (!read_sector(sector + s)) continue;
            
            fat32_dir_entry_t* entries = (fat32_dir_entry_t*)sector_buffer;
            
            for (uint32_t i = 0; i < fs.bytes_per_sector / sizeof(fat32_dir_entry_t); i++) {
                // Конец директории
                if (entries[i].name[0] == 0x00) {
                    return NULL;
                }
                
                // Удаленная запись
                if (entries[i].name[0] == 0xE5) {
                    continue;
                }
                
                // Длинное имя - пропускаем
                if (entries[i].attributes == FAT32_ATTR_LONG_NAME) {
                    continue;
                }
                
                // Сравниваем имя
                if (memcmp(entries[i].name, target_name, 8) == 0 &&
                    memcmp(entries[i].ext, target_ext, 3) == 0) {
                    
                    if (sector_out) *sector_out = sector + s;
                    if (offset_out) *offset_out = i * sizeof(fat32_dir_entry_t);
                    return &entries[i];
                }
            }
        }
        
        // Переходим к следующему кластеру
        uint32_t next_cluster = read_fat_entry(current_cluster);
        if (next_cluster >= FAT32_CLUSTER_EOF_MIN) {
            break; // Конец цепочки
        }
        
        current_cluster = next_cluster;
        sector = cluster_to_lba(current_cluster);
    }
    
    return NULL;
}

// ==================== ОСНОВНЫЕ ФУНКЦИИ ====================

// Инициализация FAT32
int fat32_init(uint8_t disk_num, uint32_t partition_start) {
    serial_puts("[FAT32] Initializing filesystem...\n");
    
    memset(&fs, 0, sizeof(fs));
    current_disk = disk_num;
    fs.partition_start = partition_start;
    
    // Читаем загрузочный сектор
    if (!read_sector(partition_start)) {
        serial_puts("[FAT32] Error: Cannot read boot sector\n");
        return 0;
    }
    
    fat32_boot_sector_t* bs = (fat32_boot_sector_t*)sector_buffer;
    
    // Проверяем сигнатуру
    if (bs->boot_signature != 0x29) {
        serial_puts("[FAT32] Error: Invalid boot signature\n");
        return 0;
    }
    
    // Проверяем тип файловой системы
    if (memcmp(bs->fs_type, "FAT32", 5) != 0) {
        serial_puts("[FAT32] Error: Not a FAT32 filesystem\n");
        return 0;
    }
    
    // Заполняем структуру
    fs.bytes_per_sector = bs->bytes_per_sector;
    fs.sectors_per_cluster = bs->sectors_per_cluster;
    fs.cluster_size = fs.bytes_per_sector * fs.sectors_per_cluster;
    fs.reserved_sectors = bs->reserved_sectors;
    fs.fat_count = bs->fat_count;
    fs.sectors_per_fat = bs->sectors_per_fat_32;
    fs.total_sectors = bs->total_sectors_32;
    fs.root_cluster = bs->root_cluster;
    
    // Вычисляем важные смещения
    fs.fat_start = partition_start + fs.reserved_sectors;
    fs.data_start = fs.fat_start + (fs.fat_count * fs.sectors_per_fat);
    
    // Вычисляем количество кластеров
    uint32_t data_sectors = fs.total_sectors - (fs.data_start - partition_start);
    fs.total_clusters = data_sectors / fs.sectors_per_cluster;
    
    // Копируем метку тома
    memcpy(fs.volume_label, bs->volume_label, 11);
    fs.volume_label[11] = '\0';
    
    // Убираем пробелы
    int len = strlen(fs.volume_label);
    while (len > 0 && fs.volume_label[len-1] == ' ') {
        fs.volume_label[--len] = '\0';
    }
    
    fs.volume_id = bs->volume_id;
    fs.mounted = 1;
    
    // Подсчет свободных кластеров
    fs.free_clusters = 0;
    fs.used_clusters = 0;
    fs.bad_clusters = 0;
    
    for (uint32_t cluster = 2; cluster < fs.total_clusters + 2; cluster++) {
        uint32_t entry = read_fat_entry(cluster);
        if (entry == FAT32_CLUSTER_FREE) {
            fs.free_clusters++;
        } else if (entry == FAT32_CLUSTER_BAD) {
            fs.bad_clusters++;
        } else {
            fs.used_clusters++;
        }
    }
    
    serial_puts("[FAT32] Mounted successfully\n");
    serial_puts("  Volume: ");
    serial_puts(fs.volume_label);
    serial_puts("\n");
    serial_puts("  Cluster size: ");
    serial_puts_num(fs.cluster_size / 1024);
    serial_puts(" KB\n");
    serial_puts("  Total clusters: ");
    serial_puts_num(fs.total_clusters);
    serial_puts("\n");
    serial_puts("  Free clusters: ");
    serial_puts_num(fs.free_clusters);
    serial_puts(" (");
    serial_puts_num((fs.free_clusters * fs.cluster_size) / (1024 * 1024));
    serial_puts(" MB)\n");
    
    return 1;
}

// Открытие файла
fat32_file_t* fat32_open(const char* path, const char* mode) {
    if (!fs.mounted) return NULL;
    
    // Пока поддерживаем только корневую директорию
    if (strchr(path, '/') != NULL) {
        serial_puts("[FAT32] Error: Subdirectories not supported yet\n");
        return NULL;
    }
    
    fat32_file_t* file = (fat32_file_t*)kmalloc(sizeof(fat32_file_t));
    if (!file) return NULL;
    
    memset(file, 0, sizeof(fat32_file_t));
    file->fs = &fs;
    strcpy(file->name, path);
    
    // Ищем файл в корневой директории
    fat32_dir_entry_t* entry = find_dir_entry(fs.root_cluster, path, NULL, NULL);
    
    if (entry) {
        // Файл существует
        file->start_cluster = (entry->cluster_high << 16) | entry->cluster_low;
        file->current_cluster = file->start_cluster;
        file->size = entry->file_size;
        file->attributes = entry->attributes;
    } else {
        // Файл не существует
        if (strchr(mode, 'w') || strchr(mode, 'a')) {
            // Создаем новый файл
            file->start_cluster = 0;
            file->size = 0;
            file->attributes = FAT32_ATTR_ARCHIVE;
        } else {
            // Режим чтения, но файла нет
            kfree(file);
            return NULL;
        }
    }
    
    // Устанавливаем режим
    if (strchr(mode, 'r')) file->mode = 'r';
    if (strchr(mode, 'w')) file->mode = 'w';
    if (strchr(mode, 'a')) file->mode = 'a';
    
    file->opened = 1;
    return file;
}

// Чтение из файла
uint32_t fat32_read(fat32_file_t* file, void* buffer, uint32_t size) {
    if (!file || !file->opened || file->mode != 'r') return 0;
    
    // Не читаем больше, чем есть
    if (file->position + size > file->size) {
        size = file->size - file->position;
    }
    
    if (size == 0) return 0;
    
    uint8_t* buf = (uint8_t*)buffer;
    uint32_t bytes_read = 0;
    uint32_t current_cluster = file->current_cluster;
    uint32_t cluster_offset = file->position % fs.cluster_size;
    
    // Вычисляем текущий сектор и смещение
    uint32_t sector_in_cluster = cluster_offset / fs.bytes_per_sector;
    uint32_t sector_offset = cluster_offset % fs.bytes_per_sector;
    uint32_t lba = cluster_to_lba(current_cluster) + sector_in_cluster;
    
    while (bytes_read < size) {
        // Читаем сектор
        if (!read_sector(lba)) break;
        
        // Сколько можно прочитать из этого сектора
        uint32_t chunk = fs.bytes_per_sector - sector_offset;
        if (chunk > size - bytes_read) {
            chunk = size - bytes_read;
        }
        
        // Копируем данные
        memcpy(buf + bytes_read, sector_buffer + sector_offset, chunk);
        bytes_read += chunk;
        file->position += chunk;
        
        // Сбрасываем смещение в секторе
        sector_offset = 0;
        
        // Переходим к следующему сектору
        if (++sector_in_cluster >= fs.sectors_per_cluster) {
            sector_in_cluster = 0;
            
            // Переходим к следующему кластеру
            uint32_t next_cluster = read_fat_entry(current_cluster);
            if (next_cluster >= FAT32_CLUSTER_EOF_MIN) {
                break; // Конец файла
            }
            current_cluster = next_cluster;
        }
        
        lba = cluster_to_lba(current_cluster) + sector_in_cluster;
    }
    
    file->current_cluster = current_cluster;
    return bytes_read;
}

// Закрытие файла
int fat32_close(fat32_file_t* file) {
    if (!file || !file->opened) return 0;
    
    // TODO: Запись изменений (для режима записи)
    
    kfree(file);
    return 1;
}

// Получение информации о файловой системе
void fat32_print_info(void) {
    if (!fs.mounted) {
        serial_puts("[FAT32] Filesystem not mounted\n");
        return;
    }
    
    serial_puts("\n=== FAT32 FILESYSTEM INFO ===\n");
    serial_puts("Volume label: ");
    serial_puts(fs.volume_label);
    serial_puts("\n");
    
    serial_puts("Bytes per sector: ");
    serial_puts_num(fs.bytes_per_sector);
    serial_puts("\n");
    
    serial_puts("Sectors per cluster: ");
    serial_puts_num(fs.sectors_per_cluster);
    serial_puts("\n");
    
    serial_puts("Cluster size: ");
    serial_puts_num(fs.cluster_size);
    serial_puts(" bytes (");
    serial_puts_num(fs.cluster_size / 1024);
    serial_puts(" KB)\n");
    
    serial_puts("Total clusters: ");
    serial_puts_num(fs.total_clusters);
    serial_puts("\n");
    
    serial_puts("Free clusters: ");
    serial_puts_num(fs.free_clusters);
    serial_puts(" (");
    serial_puts_num((fs.free_clusters * fs.cluster_size) / (1024 * 1024));
    serial_puts(" MB)\n");
    
    serial_puts("Used clusters: ");
    serial_puts_num(fs.used_clusters);
    serial_puts("\n");
    
    serial_puts("Bad clusters: ");
    serial_puts_num(fs.bad_clusters);
    serial_puts("\n");
    
    serial_puts("Total space: ");
    serial_puts_num((fs.total_clusters * fs.cluster_size) / (1024 * 1024));
    serial_puts(" MB\n");
    
    serial_puts("==============================\n");
}

// Чтение всего файла в память
void* fat32_read_whole_file(const char* path, uint32_t* size) {
    fat32_file_t* file = fat32_open(path, "r");
    if (!file) return NULL;
    
    void* buffer = kmalloc(file->size);
    if (!buffer) {
        fat32_close(file);
        return NULL;
    }
    
    fat32_read(file, buffer, file->size);
    
    if (size) *size = file->size;
    
    fat32_close(file);
    return buffer;
}

// Создание файла с данными
int fat32_create_file_with_data(const char* path, const void* data, uint32_t size) {
    // TODO: Реализовать запись файлов
    serial_puts("[FAT32] File writing not implemented yet\n");
    return 0;
}

// Список файлов в директории
void fat32_list(const char* path) {
    if (!fs.mounted) {
        serial_puts("[FAT32] Filesystem not mounted\n");
        return;
    }
    
    // Пока только корневая директория
    if (path && strcmp(path, "/") != 0) {
        serial_puts("[FAT32] Only root directory supported\n");
        return;
    }
    
    serial_puts("\n=== DIRECTORY LISTING ===\n");
    
    uint32_t current_cluster = fs.root_cluster;
    uint32_t sector = cluster_to_lba(current_cluster);
    uint32_t file_count = 0;
    
    while (1) {
        for (uint32_t s = 0; s < fs.sectors_per_cluster; s++) {
            if (!read_sector(sector + s)) continue;
            
            fat32_dir_entry_t* entries = (fat32_dir_entry_t*)sector_buffer;
            
            for (uint32_t i = 0; i < fs.bytes_per_sector / sizeof(fat32_dir_entry_t); i++) {
                // Конец директории
                if (entries[i].name[0] == 0x00) {
                    serial_puts("\nTotal files: ");
                    serial_puts_num(file_count);
                    serial_puts("\n");
                    return;
                }
                
                // Удаленная запись
                if (entries[i].name[0] == 0xE5) {
                    continue;
                }
                
                // Длинное имя
                if (entries[i].attributes == FAT32_ATTR_LONG_NAME) {
                    continue;
                }
                
                // Том
                if (entries[i].attributes == FAT32_ATTR_VOLUME_ID) {
                    continue;
                }
                
                // Преобразуем имя
                char name[13];
                name_from_83(entries[i].name, entries[i].ext, name);
                
                serial_puts("[");
                serial_puts(entries[i].attributes & FAT32_ATTR_DIRECTORY ? "DIR" : "FILE");
                serial_puts("] ");
                serial_puts(name);
                
                if (!(entries[i].attributes & FAT32_ATTR_DIRECTORY)) {
                    serial_puts(" (");
                    serial_puts_num(entries[i].file_size);
                    serial_puts(" bytes)");
                }
                
                serial_puts("\n");
                file_count++;
            }
        }
        
        // Переходим к следующему кластеру
        uint32_t next_cluster = read_fat_entry(current_cluster);
        if (next_cluster >= FAT32_CLUSTER_EOF_MIN) {
            break;
        }
        
        current_cluster = next_cluster;
        sector = cluster_to_lba(current_cluster);
    }
    
    serial_puts("\nTotal files: ");
    serial_puts_num(file_count);
    serial_puts("\n");
}

// Получение свободного места
uint32_t fat32_get_free_space(void) {
    if (!fs.mounted) return 0;
    return fs.free_clusters * fs.cluster_size;
}

// Получение общего размера
uint32_t fat32_get_total_space(void) {
    if (!fs.mounted) return 0;
    return fs.total_clusters * fs.cluster_size;
}

// Получение метки тома
const char* fat32_get_volume_label(void) {
    return fs.mounted ? fs.volume_label : "Unknown";
}

// Получение указателя на файловую систему
fat32_fs_t* fat32_get_fs(void) {
    return fs.mounted ? &fs : NULL;
}