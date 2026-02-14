#ifndef FS_FAT32_H
#define FS_FAT32_H

#include <stdint.h>
#include <stddef.h>

// Максимальная длина имени файла (8.3 формат)
#define FAT32_MAX_NAME 13
#define FAT32_MAX_PATH 256

// Атрибуты файлов
#define FAT32_ATTR_READ_ONLY  0x01
#define FAT32_ATTR_HIDDEN     0x02
#define FAT32_ATTR_SYSTEM     0x04
#define FAT32_ATTR_VOLUME_ID  0x08
#define FAT32_ATTR_DIRECTORY  0x10
#define FAT32_ATTR_ARCHIVE    0x20
#define FAT32_ATTR_LONG_NAME  0x0F

// Специальные кластеры
#define FAT32_CLUSTER_FREE    0x00000000
#define FAT32_CLUSTER_RESERVED 0x00000001
#define FAT32_CLUSTER_BAD     0x0FFFFFF7
#define FAT32_CLUSTER_EOF_MIN 0x0FFFFFF8
#define FAT32_CLUSTER_EOF_MAX 0x0FFFFFFF

// Структура загрузочного сектора FAT32
typedef struct {
    // Общие для всех FAT
    uint8_t jmp[3];
    char oem[8];
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t fat_count;
    uint16_t root_entries;
    uint16_t total_sectors_16;
    uint8_t media_type;
    uint16_t sectors_per_fat_16;
    uint16_t sectors_per_track;
    uint16_t head_count;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    
    // Специфичные для FAT32
    uint32_t sectors_per_fat_32;
    uint16_t flags;
    uint16_t fat_version;
    uint32_t root_cluster;
    uint16_t fs_info_sector;
    uint16_t backup_boot_sector;
    uint8_t reserved[12];
    uint8_t drive_number;
    uint8_t reserved1;
    uint8_t boot_signature;
    uint32_t volume_id;
    char volume_label[11];
    char fs_type[8];
} __attribute__((packed)) fat32_boot_sector_t;

// Запись в директории (32 байта)
typedef struct {
    char name[8];           // Имя файла
    char ext[3];           // Расширение
    uint8_t attributes;    // Атрибуты
    uint8_t reserved;      // Зарезервировано
    uint8_t create_time_tenth; // Десятые секунды создания
    uint16_t create_time;  // Время создания
    uint16_t create_date;  // Дата создания
    uint16_t access_date;  // Дата последнего доступа
    uint16_t cluster_high; // Старшие 16 бит первого кластера
    uint16_t modify_time;  // Время изменения
    uint16_t modify_date;  // Дата изменения
    uint16_t cluster_low;  // Младшие 16 бит первого кластера
    uint32_t file_size;    // Размер файла в байтах
} __attribute__((packed)) fat32_dir_entry_t;

// Структура для работы с FAT32
typedef struct {
    // Параметры файловой системы
    uint32_t partition_start;    // Начало раздела (LBA)
    uint32_t bytes_per_sector;
    uint32_t sectors_per_cluster;
    uint32_t cluster_size;       // bytes_per_sector * sectors_per_cluster
    uint32_t reserved_sectors;
    uint32_t fat_count;
    uint32_t sectors_per_fat;
    uint32_t total_sectors;
    uint32_t total_clusters;
    uint32_t data_start;         // Начало данных (LBA)
    uint32_t fat_start;          // Начало FAT (LBA)
    uint32_t root_cluster;
    
    // Кэш
    uint32_t* fat_cache;
    uint32_t fat_cache_size;
    uint8_t* sector_cache;
    uint32_t cached_sector;
    uint8_t sector_dirty;
    
    // Статистика
    uint32_t free_clusters;
    uint32_t used_clusters;
    uint32_t bad_clusters;
    
    // Состояние
    uint8_t mounted;
    char volume_label[12];
    uint32_t volume_id;
} fat32_fs_t;

// Структура для работы с файлом
typedef struct {
    fat32_fs_t* fs;           // Файловая система
    char name[FAT32_MAX_NAME]; // Имя файла
    uint32_t start_cluster;   // Первый кластер
    uint32_t current_cluster; // Текущий кластер
    uint32_t size;            // Размер файла
    uint32_t position;        // Текущая позиция
    uint32_t sector_offset;   // Смещение в текущем секторе
    uint8_t mode;             // Режим (чтение/запись)
    uint8_t attributes;       // Атрибуты файла
    uint8_t opened;           // Файл открыт
} fat32_file_t;

// Структура для работы с директорией
typedef struct {
    fat32_fs_t* fs;
    uint32_t cluster;         // Кластер директории
    uint32_t position;        // Позиция в директории
    char path[FAT32_MAX_PATH];
    uint8_t opened;
} fat32_dir_t;

// ==================== ФУНКЦИИ ФАЙЛОВОЙ СИСТЕМЫ ====================

// Инициализация
int fat32_init(uint8_t disk_num, uint32_t partition_start);
int fat32_format(uint8_t disk_num, uint32_t partition_start, const char* label);
void fat32_unmount(void);
fat32_fs_t* fat32_get_fs(void);

// Информация
void fat32_print_info(void);
uint32_t fat32_get_free_space(void);
uint32_t fat32_get_total_space(void);
uint32_t fat32_get_used_space(void);
const char* fat32_get_volume_label(void);

// Работа с файлами
fat32_file_t* fat32_open(const char* path, const char* mode);
int fat32_close(fat32_file_t* file);
uint32_t fat32_read(fat32_file_t* file, void* buffer, uint32_t size);
uint32_t fat32_write(fat32_file_t* file, const void* buffer, uint32_t size);
int fat32_seek(fat32_file_t* file, uint32_t offset);
uint32_t fat32_tell(fat32_file_t* file);
int fat32_eof(fat32_file_t* file);

// Управление файлами
int fat32_create(const char* path);
int fat32_delete(const char* path);
int fat32_rename(const char* old_path, const char* new_path);
int fat32_exists(const char* path);
uint32_t fat32_get_size(const char* path);

// Работа с директориями
fat32_dir_t* fat32_opendir(const char* path);
int fat32_readdir(fat32_dir_t* dir, char* name, uint8_t* is_dir, uint32_t* size);
int fat32_closedir(fat32_dir_t* dir);
int fat32_mkdir(const char* path);
int fat32_rmdir(const char* path);
int fat32_chdir(const char* path);
void fat32_list(const char* path);

// Утилиты
int fat32_copy(const char* src, const char* dst);
int fat32_move(const char* src, const char* dst);
int fat32_create_file_with_data(const char* path, const void* data, uint32_t size);
void* fat32_read_whole_file(const char* path, uint32_t* size);

// Проверка и восстановление
int fat32_check(void);
int fat32_defrag(void);
int fat32_repair(void);

#endif // FS_FAT32_H