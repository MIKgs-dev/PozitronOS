#ifndef PFS_H
#define PFS_H

#include <stdint.h>

#define PFS_MAGIC           0x504F5A49  // "POZI"
#define PFS_VERSION         1
#define PFS_BLOCK_SIZE      512

#define PFS_NAME_LEN        32
#define PFS_DIRECT_BLOCKS   12
#define PFS_INDIRECT_BLOCKS (PFS_BLOCK_SIZE / 4)  // 128

// Типы файлов (mode)
#define PFS_MODE_FILE       0x01
#define PFS_MODE_DIR        0x02
#define PFS_MODE_READ       0x04
#define PFS_MODE_WRITE      0x08

// Ошибки
#define PFS_OK              0
#define PFS_ERR_NOT_FOUND   -1
#define PFS_ERR_NO_SPACE    -2
#define PFS_ERR_NO_INODE    -3
#define PFS_ERR_IO          -4
#define PFS_ERR_NOT_DIR     -5
#define PFS_ERR_NOT_FILE    -6
#define PFS_ERR_EXISTS      -7
#define PFS_ERR_INVALID     -8

// Суперблок (сектор 1)
typedef struct {
    uint32_t magic;                 // "POZI"
    uint32_t version;                // 1
    uint32_t block_size;             // 512
    uint32_t total_blocks;           // всего блоков
    uint32_t inode_count;            // всего inodes
    uint32_t free_blocks;            // свободных блоков
    uint32_t free_inodes;            // свободных inodes
    uint32_t root_inode;             // номер корневого каталога
    uint32_t first_free_block;       // первый свободный блок
    uint32_t first_free_inode;       // первый свободный inode
    uint8_t  reserved[512 - 10*4];   // до 512 байт
} pfs_super_t;

// Inode (64 байта)
typedef struct {
    uint32_t mode;                    // тип и права
    uint32_t size;                     // размер в байтах
    uint32_t ctime;                    // время создания
    uint32_t mtime;                    // время модификации
    uint32_t blocks[PFS_DIRECT_BLOCKS]; // прямые блоки
    uint32_t indirect;                  // косвенный блок
    uint32_t double_indirect;           // двойной косвенный
    uint32_t refcount;                   // счётчик ссылок
    char     name[PFS_NAME_LEN];          // имя файла
    uint32_t reserved[2];                // выравнивание до 64 байт
} pfs_inode_t;

// Запись каталога (32 байта)
typedef struct {
    uint32_t inode;                      // номер inode
    uint32_t mode;                        // тип файла
    char     name[PFS_NAME_LEN];           // имя
    uint32_t size;                         // размер (для быстрого доступа)
} pfs_dirent_t;

// ============================================================================
// ОСНОВНЫЕ ФУНКЦИИ
// ============================================================================

// Монтирование/форматирование
int pfs_mount(int disk_num);
int pfs_format(int disk_num);

// Чтение/запись inode
int pfs_read_inode(uint32_t inode_num, pfs_inode_t* inode);
int pfs_write_inode(uint32_t inode_num, pfs_inode_t* inode);

// Поиск по имени (плоский)
int pfs_find(const char* name, pfs_inode_t* inode);

// Поиск по пути (с каталогами)
int pfs_lookup(const char* path, pfs_inode_t* inode);
int pfs_path_to_inode(const char* path, uint32_t* inode_num);

// Операции с файлами
int pfs_create(const char* path, uint32_t mode);
int pfs_delete(const char* path);
int pfs_rename(const char* old_path, const char* new_path);
int pfs_open(const char* path, pfs_inode_t* inode);

// Чтение/запись
int pfs_read(pfs_inode_t* inode, uint32_t offset, uint32_t size, void* buffer);
int pfs_write(pfs_inode_t* inode, uint32_t offset, uint32_t size, const void* buffer);
int pfs_truncate(pfs_inode_t* inode, uint32_t new_size);

// Операции с каталогами
int pfs_mkdir(const char* path);
int pfs_rmdir(const char* path);
int pfs_readdir(const char* path, pfs_dirent_t* entries, uint32_t max_entries);
int pfs_isdir(pfs_inode_t* inode);

// Утилиты
int pfs_exists(const char* path);
void pfs_stat(const char* path);
int pfs_get_free_blocks(void);
int pfs_get_free_inodes(void);

uint32_t pfs_get_file_block(pfs_inode_t* inode, uint32_t lblock);
int pfs_read_block(uint32_t block_num, void* buffer);
int pfs_write_block(uint32_t block_num, const void* buffer);

#endif