#include "fs/pfs.h"
#include "drivers/disk.h"
#include "drivers/serial.h"
#include "kernel/memory.h"
#include "lib/string.h"

static int current_disk = -1;
static pfs_super_t super;

// ============================================================================
// ВНУТРЕННИЕ ФУНКЦИИ РАБОТЫ С БЛОКАМИ
// ============================================================================

static int read_block(uint32_t block_num, void* buffer) {
    return disk_read(current_disk, block_num, 1, buffer);
}

static int write_block(uint32_t block_num, const void* buffer) {
    return disk_write(current_disk, block_num, 1, buffer);
}

static void inode_to_block(uint32_t inode_num, uint32_t* block, uint32_t* offset) {
    uint32_t bitmap_blocks = (super.inode_count * 8 + PFS_BLOCK_SIZE - 1) / PFS_BLOCK_SIZE;
    uint32_t inode_table_start = 2 + 2 * bitmap_blocks;
    uint32_t inodes_per_block = PFS_BLOCK_SIZE / sizeof(pfs_inode_t);
    
    *block = inode_table_start + inode_num / inodes_per_block;
    *offset = (inode_num % inodes_per_block) * sizeof(pfs_inode_t);
}

static int bitmap_block_test(uint32_t block_num) {
    uint32_t bitmap_block = 2 + block_num / (PFS_BLOCK_SIZE * 8);
    uint32_t bit = block_num % (PFS_BLOCK_SIZE * 8);
    uint32_t byte = bit / 8;
    uint32_t bit_in_byte = bit % 8;
    
    uint8_t buffer[PFS_BLOCK_SIZE];
    if (read_block(bitmap_block, buffer) < 0) return -1;
    
    return (buffer[byte] >> bit_in_byte) & 1;
}

static void bitmap_block_set(uint32_t block_num, int value) {
    uint32_t bitmap_block = 2 + block_num / (PFS_BLOCK_SIZE * 8);
    uint32_t bit = block_num % (PFS_BLOCK_SIZE * 8);
    uint32_t byte = bit / 8;
    uint32_t bit_in_byte = bit % 8;
    
    uint8_t buffer[PFS_BLOCK_SIZE];
    if (read_block(bitmap_block, buffer) < 0) return;
    
    if (value)
        buffer[byte] |= (1 << bit_in_byte);
    else
        buffer[byte] &= ~(1 << bit_in_byte);
    
    write_block(bitmap_block, buffer);
}

static uint32_t alloc_block(void) {
    if (super.free_blocks == 0) return 0;
    
    for (uint32_t b = super.first_free_block; b < super.total_blocks; b++) {
        if (!bitmap_block_test(b)) {
            bitmap_block_set(b, 1);
            super.free_blocks--;
            super.first_free_block = b + 1;
            write_block(1, &super);
            return b;
        }
    }
    
    for (uint32_t b = 2; b < super.first_free_block; b++) {
        if (!bitmap_block_test(b)) {
            bitmap_block_set(b, 1);
            super.free_blocks--;
            super.first_free_block = b + 1;
            write_block(1, &super);
            return b;
        }
    }
    
    return 0;
}

static void free_block(uint32_t block_num) {
    if (block_num == 0) return;
    bitmap_block_set(block_num, 0);
    super.free_blocks++;
    if (block_num < super.first_free_block)
        super.first_free_block = block_num;
    write_block(1, &super);
}

static uint32_t get_file_block(pfs_inode_t* inode, uint32_t lblock) {
    if (lblock < PFS_DIRECT_BLOCKS)
        return inode->blocks[lblock];
    
    lblock -= PFS_DIRECT_BLOCKS;
    
    if (lblock < PFS_INDIRECT_BLOCKS) {
        if (inode->indirect == 0) return 0;
        uint32_t indirect_table[PFS_INDIRECT_BLOCKS];
        if (read_block(inode->indirect, indirect_table) < 0) return 0;
        return indirect_table[lblock];
    }
    
    lblock -= PFS_INDIRECT_BLOCKS;
    
    if (lblock < PFS_INDIRECT_BLOCKS * PFS_INDIRECT_BLOCKS) {
        if (inode->double_indirect == 0) return 0;
        uint32_t double_indirect_table[PFS_INDIRECT_BLOCKS];
        if (read_block(inode->double_indirect, double_indirect_table) < 0) return 0;
        
        uint32_t indirect_block = double_indirect_table[lblock / PFS_INDIRECT_BLOCKS];
        if (indirect_block == 0) return 0;
        
        uint32_t indirect_table[PFS_INDIRECT_BLOCKS];
        if (read_block(indirect_block, indirect_table) < 0) return 0;
        
        return indirect_table[lblock % PFS_INDIRECT_BLOCKS];
    }
    
    return 0;
}

static int set_file_block(pfs_inode_t* inode, uint32_t lblock, uint32_t block_num) {
    if (lblock < PFS_DIRECT_BLOCKS) {
        inode->blocks[lblock] = block_num;
        return 0;
    }
    
    lblock -= PFS_DIRECT_BLOCKS;
    
    if (lblock < PFS_INDIRECT_BLOCKS) {
        if (inode->indirect == 0) {
            inode->indirect = alloc_block();
            if (inode->indirect == 0) return -1;
        }
        
        uint32_t indirect_table[PFS_INDIRECT_BLOCKS];
        if (read_block(inode->indirect, indirect_table) < 0) return -1;
        
        indirect_table[lblock] = block_num;
        return write_block(inode->indirect, indirect_table);
    }
    
    lblock -= PFS_INDIRECT_BLOCKS;
    
    if (lblock < PFS_INDIRECT_BLOCKS * PFS_INDIRECT_BLOCKS) {
        if (inode->double_indirect == 0) {
            inode->double_indirect = alloc_block();
            if (inode->double_indirect == 0) return -1;
        }
        
        uint32_t double_indirect_table[PFS_INDIRECT_BLOCKS];
        if (read_block(inode->double_indirect, double_indirect_table) < 0) return -1;
        
        uint32_t indirect_index = lblock / PFS_INDIRECT_BLOCKS;
        uint32_t block_index = lblock % PFS_INDIRECT_BLOCKS;
        
        if (double_indirect_table[indirect_index] == 0) {
            double_indirect_table[indirect_index] = alloc_block();
            if (double_indirect_table[indirect_index] == 0) return -1;
            if (write_block(inode->double_indirect, double_indirect_table) < 0) return -1;
        }
        
        uint32_t indirect_table[PFS_INDIRECT_BLOCKS];
        if (read_block(double_indirect_table[indirect_index], indirect_table) < 0) return -1;
        
        indirect_table[block_index] = block_num;
        return write_block(double_indirect_table[indirect_index], indirect_table);
    }
    
    return -1;
}

static void free_file_blocks(pfs_inode_t* inode) {
    for (int i = 0; i < PFS_DIRECT_BLOCKS; i++) {
        if (inode->blocks[i]) {
            free_block(inode->blocks[i]);
            inode->blocks[i] = 0;
        }
    }
    
    if (inode->indirect) {
        uint32_t indirect_table[PFS_INDIRECT_BLOCKS];
        if (read_block(inode->indirect, indirect_table) == 0) {
            for (int i = 0; i < PFS_INDIRECT_BLOCKS; i++) {
                if (indirect_table[i]) free_block(indirect_table[i]);
            }
        }
        free_block(inode->indirect);
        inode->indirect = 0;
    }
    
    if (inode->double_indirect) {
        uint32_t double_indirect_table[PFS_INDIRECT_BLOCKS];
        if (read_block(inode->double_indirect, double_indirect_table) == 0) {
            for (int i = 0; i < PFS_INDIRECT_BLOCKS; i++) {
                if (double_indirect_table[i]) {
                    uint32_t indirect_table[PFS_INDIRECT_BLOCKS];
                    if (read_block(double_indirect_table[i], indirect_table) == 0) {
                        for (int j = 0; j < PFS_INDIRECT_BLOCKS; j++) {
                            if (indirect_table[j]) free_block(indirect_table[j]);
                        }
                    }
                    free_block(double_indirect_table[i]);
                }
            }
        }
        free_block(inode->double_indirect);
        inode->double_indirect = 0;
    }
}

// ============================================================================
// INODE ОПЕРАЦИИ
// ============================================================================

int pfs_read_inode(uint32_t inode_num, pfs_inode_t* inode) {
    if (!inode || inode_num >= super.inode_count) return PFS_ERR_INVALID;
    
    uint32_t block, offset;
    inode_to_block(inode_num, &block, &offset);
    
    uint8_t buffer[PFS_BLOCK_SIZE];
    if (read_block(block, buffer) < 0) return PFS_ERR_IO;
    
    memcpy(inode, buffer + offset, sizeof(pfs_inode_t));
    return PFS_OK;
}

int pfs_write_inode(uint32_t inode_num, pfs_inode_t* inode) {
    if (!inode || inode_num >= super.inode_count) return PFS_ERR_INVALID;
    
    uint32_t block, offset;
    inode_to_block(inode_num, &block, &offset);
    
    uint8_t buffer[PFS_BLOCK_SIZE];
    if (read_block(block, buffer) < 0) return PFS_ERR_IO;
    
    memcpy(buffer + offset, inode, sizeof(pfs_inode_t));
    return write_block(block, buffer);
}

static int alloc_inode(void) {
    if (super.free_inodes == 0) return -1;
    
    for (uint32_t i = super.first_free_inode; i < super.inode_count; i++) {
        pfs_inode_t inode;
        if (pfs_read_inode(i, &inode) != PFS_OK) continue;
        if (inode.name[0] == 0) {
            super.free_inodes--;
            super.first_free_inode = i + 1;
            write_block(1, &super);
            return i;
        }
    }
    
    for (uint32_t i = 0; i < super.first_free_inode; i++) {
        pfs_inode_t inode;
        if (pfs_read_inode(i, &inode) != PFS_OK) continue;
        if (inode.name[0] == 0) {
            super.free_inodes--;
            super.first_free_inode = i + 1;
            write_block(1, &super);
            return i;
        }
    }
    
    return -1;
}

static void free_inode(uint32_t inode_num) {
    pfs_inode_t inode;
    memset(&inode, 0, sizeof(inode));
    pfs_write_inode(inode_num, &inode);
    super.free_inodes++;
    if (inode_num < super.first_free_inode)
        super.first_free_inode = inode_num;
    write_block(1, &super);
}

// ============================================================================
// РАБОТА С ПУТЯМИ
// ============================================================================

static int find_in_dir(uint32_t dir_inode, const char* name, uint32_t* found_inode) {
    pfs_inode_t dir;
    if (pfs_read_inode(dir_inode, &dir) != PFS_OK) return PFS_ERR_IO;
    if (!(dir.mode & PFS_MODE_DIR)) return PFS_ERR_NOT_DIR;
    
    uint32_t blocks = (dir.size + PFS_BLOCK_SIZE - 1) / PFS_BLOCK_SIZE;
    pfs_dirent_t dirents[PFS_BLOCK_SIZE / sizeof(pfs_dirent_t)];
    
    for (uint32_t b = 0; b < blocks; b++) {
        uint32_t block_num = get_file_block(&dir, b);
        if (block_num == 0) continue;
        
        if (read_block(block_num, dirents) < 0) return PFS_ERR_IO;
        
        int entries_per_block = PFS_BLOCK_SIZE / sizeof(pfs_dirent_t);
        for (int i = 0; i < entries_per_block; i++) {
            if (dirents[i].inode != 0 && strcmp(dirents[i].name, name) == 0) {
                if (found_inode) *found_inode = dirents[i].inode;
                return PFS_OK;
            }
        }
    }
    
    return PFS_ERR_NOT_FOUND;
}

static int add_to_dir(uint32_t dir_inode, uint32_t new_inode, const char* name) {
    pfs_inode_t dir;
    if (pfs_read_inode(dir_inode, &dir) != PFS_OK) return PFS_ERR_IO;
    if (!(dir.mode & PFS_MODE_DIR)) return PFS_ERR_NOT_DIR;
    
    uint32_t blocks = (dir.size + PFS_BLOCK_SIZE - 1) / PFS_BLOCK_SIZE;
    pfs_dirent_t dirents[PFS_BLOCK_SIZE / sizeof(pfs_dirent_t)];
    
    // Ищем свободное место в существующих блоках
    for (uint32_t b = 0; b < blocks; b++) {
        uint32_t block_num = get_file_block(&dir, b);
        if (block_num == 0) continue;
        
        if (read_block(block_num, dirents) < 0) return PFS_ERR_IO;
        
        int entries_per_block = PFS_BLOCK_SIZE / sizeof(pfs_dirent_t);
        for (int i = 0; i < entries_per_block; i++) {
            if (dirents[i].inode == 0) {
                // Нашли свободную запись
                pfs_inode_t new;
                pfs_read_inode(new_inode, &new);
                
                dirents[i].inode = new_inode;
                dirents[i].mode = new.mode;
                dirents[i].size = new.size;
                strncpy(dirents[i].name, name, PFS_NAME_LEN - 1);
                
                return write_block(block_num, dirents);
            }
        }
    }
    
    // Нужен новый блок
    uint32_t new_block = alloc_block();
    if (new_block == 0) return PFS_ERR_NO_SPACE;
    
    memset(dirents, 0, sizeof(dirents));
    
    pfs_inode_t new;
    pfs_read_inode(new_inode, &new);
    
    dirents[0].inode = new_inode;
    dirents[0].mode = new.mode;
    dirents[0].size = new.size;
    strncpy(dirents[0].name, name, PFS_NAME_LEN - 1);
    
    if (write_block(new_block, dirents) < 0) {
        free_block(new_block);
        return PFS_ERR_IO;
    }
    
    if (set_file_block(&dir, blocks, new_block) < 0) {
        free_block(new_block);
        return PFS_ERR_IO;
    }
    
    dir.size += PFS_BLOCK_SIZE;
    pfs_write_inode(dir_inode, &dir);
    
    return PFS_OK;
}

static int remove_from_dir(uint32_t dir_inode, const char* name) {
    pfs_inode_t dir;
    if (pfs_read_inode(dir_inode, &dir) != PFS_OK) return PFS_ERR_IO;
    if (!(dir.mode & PFS_MODE_DIR)) return PFS_ERR_NOT_DIR;
    
    uint32_t blocks = (dir.size + PFS_BLOCK_SIZE - 1) / PFS_BLOCK_SIZE;
    pfs_dirent_t dirents[PFS_BLOCK_SIZE / sizeof(pfs_dirent_t)];
    
    for (uint32_t b = 0; b < blocks; b++) {
        uint32_t block_num = get_file_block(&dir, b);
        if (block_num == 0) continue;
        
        if (read_block(block_num, dirents) < 0) return PFS_ERR_IO;
        
        int entries_per_block = PFS_BLOCK_SIZE / sizeof(pfs_dirent_t);
        for (int i = 0; i < entries_per_block; i++) {
            if (dirents[i].inode != 0 && strcmp(dirents[i].name, name) == 0) {
                dirents[i].inode = 0;
                dirents[i].name[0] = '\0';
                return write_block(block_num, dirents);
            }
        }
    }
    
    return PFS_ERR_NOT_FOUND;
}

int pfs_lookup(const char* path, pfs_inode_t* inode) {
    if (!path || path[0] != '/') return PFS_ERR_INVALID;
    if (path[1] == '\0') {
        // Корневой каталог
        return pfs_read_inode(super.root_inode, inode);
    }
    
    uint32_t current_inode = super.root_inode;
    char path_copy[256];
    strncpy(path_copy, path + 1, 255);
    path_copy[255] = '\0';
    
    char* part = strtok(path_copy, "/");
    while (part) {
        uint32_t next_inode;
        if (find_in_dir(current_inode, part, &next_inode) != PFS_OK)
            return PFS_ERR_NOT_FOUND;
        
        current_inode = next_inode;
        part = strtok(NULL, "/");
    }
    
    return pfs_read_inode(current_inode, inode);
}

int pfs_path_to_inode(const char* path, uint32_t* inode_num) {
    pfs_inode_t inode;
    int res = pfs_lookup(path, &inode);
    if (res != PFS_OK) return res;
    
    // Находим номер inode по имени (туповато, но для начала сойдёт)
    for (uint32_t i = 0; i < super.inode_count; i++) {
        pfs_inode_t tmp;
        if (pfs_read_inode(i, &tmp) != PFS_OK) continue;
        if (strcmp(tmp.name, inode.name) == 0 && tmp.mode == inode.mode) {
            *inode_num = i;
            return PFS_OK;
        }
    }
    
    return PFS_ERR_NOT_FOUND;
}

int pfs_find(const char* name, pfs_inode_t* inode) {
    for (uint32_t i = 0; i < super.inode_count; i++) {
        pfs_inode_t tmp;
        if (pfs_read_inode(i, &tmp) != PFS_OK) continue;
        if (tmp.name[0] && strcmp(tmp.name, name) == 0) {
            *inode = tmp;
            return i;
        }
    }
    return PFS_ERR_NOT_FOUND;
}

// ============================================================================
// ОПЕРАЦИИ С ФАЙЛАМИ
// ============================================================================

int pfs_create(const char* path, uint32_t mode) {
    // Извлекаем имя файла и родительский каталог
    char path_copy[256];
    strncpy(path_copy, path, 255);
    path_copy[255] = '\0';
    
    char* last_slash = strrchr(path_copy, '/');
    if (!last_slash) return PFS_ERR_INVALID;
    
    *last_slash = '\0';
    char* filename = last_slash + 1;
    char* dir_path = path_copy;
    if (dir_path[0] == '\0') dir_path = "/";
    
    // Проверяем, существует ли уже
    pfs_inode_t existing;
    if (pfs_lookup(path, &existing) == PFS_OK)
        return PFS_ERR_EXISTS;
    
    // Получаем inode родительского каталога
    pfs_inode_t dir_inode;
    if (pfs_lookup(dir_path, &dir_inode) != PFS_OK)
        return PFS_ERR_NOT_FOUND;
    
    uint32_t dir_inode_num;
    if (pfs_path_to_inode(dir_path, &dir_inode_num) != PFS_OK)
        return PFS_ERR_NOT_FOUND;
    
    if (!(dir_inode.mode & PFS_MODE_DIR))
        return PFS_ERR_NOT_DIR;
    
    // Выделяем новый inode
    int inode_num = alloc_inode();
    if (inode_num < 0) return PFS_ERR_NO_INODE;
    
    // Заполняем inode
    pfs_inode_t inode;
    memset(&inode, 0, sizeof(inode));
    strncpy(inode.name, filename, PFS_NAME_LEN - 1);
    inode.mode = mode | PFS_MODE_FILE;
    inode.refcount = 1;
    
    if (pfs_write_inode(inode_num, &inode) != PFS_OK) {
        free_inode(inode_num);
        return PFS_ERR_IO;
    }
    
    // Добавляем запись в родительский каталог
    int res = add_to_dir(dir_inode_num, inode_num, filename);
    if (res != PFS_OK) {
        free_inode(inode_num);
        return res;
    }
    
    serial_puts("[PFS] Created: ");
    serial_puts(path);
    serial_puts("\n");
    
    return PFS_OK;
}

int pfs_delete(const char* path) {
    if (strcmp(path, "/") == 0) return PFS_ERR_INVALID;  // нельзя удалить корень
    
    // Извлекаем имя и родительский каталог
    char path_copy[256];
    strncpy(path_copy, path, 255);
    path_copy[255] = '\0';
    
    char* last_slash = strrchr(path_copy, '/');
    if (!last_slash) return PFS_ERR_INVALID;
    
    *last_slash = '\0';
    char* filename = last_slash + 1;
    char* dir_path = path_copy;
    if (dir_path[0] == '\0') dir_path = "/";
    
    // Получаем inode удаляемого файла
    pfs_inode_t inode;
    if (pfs_lookup(path, &inode) != PFS_OK)
        return PFS_ERR_NOT_FOUND;
    
    uint32_t inode_num;
    if (pfs_path_to_inode(path, &inode_num) != PFS_OK)
        return PFS_ERR_NOT_FOUND;
    
    // Получаем родительский каталог
    uint32_t dir_inode_num;
    if (pfs_path_to_inode(dir_path, &dir_inode_num) != PFS_OK)
        return PFS_ERR_NOT_FOUND;
    
    // Удаляем из каталога
    remove_from_dir(dir_inode_num, filename);
    
    // Освобождаем блоки и inode
    free_file_blocks(&inode);
    free_inode(inode_num);
    
    serial_puts("[PFS] Deleted: ");
    serial_puts(path);
    serial_puts("\n");
    
    return PFS_OK;
}

int pfs_open(const char* path, pfs_inode_t* inode) {
    return pfs_lookup(path, inode);
}

int pfs_read(pfs_inode_t* inode, uint32_t offset, uint32_t size, void* buffer) {
    if (!inode || !buffer) return -1;
    if (offset >= inode->size) return 0;
    if (offset + size > inode->size) size = inode->size - offset;
    if (size == 0) return 0;
    
    uint32_t bytes_read = 0;
    uint32_t lblock = offset / PFS_BLOCK_SIZE;
    uint32_t block_off = offset % PFS_BLOCK_SIZE;
    
    while (bytes_read < size) {
        uint32_t block_num = get_file_block(inode, lblock);
        uint32_t copy = PFS_BLOCK_SIZE - block_off;
        if (copy > size - bytes_read) copy = size - bytes_read;
        
        if (block_num == 0) {
            memset((uint8_t*)buffer + bytes_read, 0, copy);
        } else {
            uint8_t block[PFS_BLOCK_SIZE];
            if (read_block(block_num, block) < 0) return -1;
            memcpy((uint8_t*)buffer + bytes_read, block + block_off, copy);
        }
        
        bytes_read += copy;
        lblock++;
        block_off = 0;
    }
    
    return bytes_read;
}

int pfs_write(pfs_inode_t* inode, uint32_t offset, uint32_t size, const void* buffer) {
    if (!inode || !buffer) return -1;
    if (size == 0) return 0;
    
    uint32_t bytes_written = 0;
    uint32_t lblock = offset / PFS_BLOCK_SIZE;
    uint32_t block_off = offset % PFS_BLOCK_SIZE;
    uint32_t inode_num = 0;  // нужно будет получить
    
    while (bytes_written < size) {
        uint32_t block_num = get_file_block(inode, lblock);
        if (block_num == 0) {
            block_num = alloc_block();
            if (block_num == 0) return PFS_ERR_NO_SPACE;
            if (set_file_block(inode, lblock, block_num) < 0) {
                free_block(block_num);
                return PFS_ERR_IO;
            }
        }
        
        uint8_t block[PFS_BLOCK_SIZE];
        uint32_t copy = PFS_BLOCK_SIZE - block_off;
        if (copy > size - bytes_written) copy = size - bytes_written;
        
        // Если пишем не весь блок, читаем сначала
        if (copy < PFS_BLOCK_SIZE || block_off != 0) {
            if (read_block(block_num, block) < 0) return -1;
        }
        
        memcpy(block + block_off, (uint8_t*)buffer + bytes_written, copy);
        
        if (write_block(block_num, block) < 0) return -1;
        
        bytes_written += copy;
        lblock++;
        block_off = 0;
    }
    
    if (offset + size > inode->size) {
        inode->size = offset + size;
        inode->mtime = 0;  // TODO: время
        
        // Находим номер inode
        for (uint32_t i = 0; i < super.inode_count; i++) {
            pfs_inode_t tmp;
            if (pfs_read_inode(i, &tmp) != PFS_OK) continue;
            if (tmp.mode == inode->mode && strcmp(tmp.name, inode->name) == 0) {
                pfs_write_inode(i, inode);
                break;
            }
        }
    }
    
    return bytes_written;
}

int pfs_truncate(pfs_inode_t* inode, uint32_t new_size) {
    if (!inode) return -1;
    
    uint32_t old_blocks = (inode->size + PFS_BLOCK_SIZE - 1) / PFS_BLOCK_SIZE;
    uint32_t new_blocks = (new_size + PFS_BLOCK_SIZE - 1) / PFS_BLOCK_SIZE;
    
    if (new_blocks < old_blocks) {
        for (uint32_t i = new_blocks; i < old_blocks; i++) {
            uint32_t block_num = get_file_block(inode, i);
            if (block_num) {
                free_block(block_num);
                set_file_block(inode, i, 0);
            }
        }
    }
    
    inode->size = new_size;
    inode->mtime = 0;
    
    return PFS_OK;
}

// ============================================================================
// ОПЕРАЦИИ С КАТАЛОГАМИ
// ============================================================================

int pfs_mkdir(const char* path) {
    // Извлекаем имя и родительский каталог
    char path_copy[256];
    strncpy(path_copy, path, 255);
    path_copy[255] = '\0';
    
    char* last_slash = strrchr(path_copy, '/');
    if (!last_slash) return PFS_ERR_INVALID;
    
    *last_slash = '\0';
    char* dirname = last_slash + 1;
    char* parent_path = path_copy;
    if (parent_path[0] == '\0') parent_path = "/";
    
    // Проверяем, существует ли уже
    pfs_inode_t existing;
    if (pfs_lookup(path, &existing) == PFS_OK)
        return PFS_ERR_EXISTS;
    
    // Получаем родительский каталог
    pfs_inode_t parent_inode;
    if (pfs_lookup(parent_path, &parent_inode) != PFS_OK)
        return PFS_ERR_NOT_FOUND;
    
    uint32_t parent_inode_num;
    if (pfs_path_to_inode(parent_path, &parent_inode_num) != PFS_OK)
        return PFS_ERR_NOT_FOUND;
    
    if (!(parent_inode.mode & PFS_MODE_DIR))
        return PFS_ERR_NOT_DIR;
    
    // Выделяем новый inode
    int inode_num = alloc_inode();
    if (inode_num < 0) return PFS_ERR_NO_INODE;
    
    // Заполняем inode каталога
    pfs_inode_t inode;
    memset(&inode, 0, sizeof(inode));
    strncpy(inode.name, dirname, PFS_NAME_LEN - 1);
    inode.mode = PFS_MODE_DIR | PFS_MODE_READ | PFS_MODE_WRITE;
    inode.refcount = 1;
    
    if (pfs_write_inode(inode_num, &inode) != PFS_OK) {
        free_inode(inode_num);
        return PFS_ERR_IO;
    }
    
    // Добавляем запись в родительский каталог
    int res = add_to_dir(parent_inode_num, inode_num, dirname);
    if (res != PFS_OK) {
        free_inode(inode_num);
        return res;
    }
    
    serial_puts("[PFS] Directory created: ");
    serial_puts(path);
    serial_puts("\n");
    
    return PFS_OK;
}

int pfs_rmdir(const char* path) {
    if (strcmp(path, "/") == 0) return PFS_ERR_INVALID;
    
    pfs_inode_t dir_inode;
    if (pfs_lookup(path, &dir_inode) != PFS_OK)
        return PFS_ERR_NOT_FOUND;
    
    if (!(dir_inode.mode & PFS_MODE_DIR))
        return PFS_ERR_NOT_DIR;
    
    // Проверяем, пуст ли каталог
    uint32_t blocks = (dir_inode.size + PFS_BLOCK_SIZE - 1) / PFS_BLOCK_SIZE;
    pfs_dirent_t dirents[PFS_BLOCK_SIZE / sizeof(pfs_dirent_t)];
    
    for (uint32_t b = 0; b < blocks; b++) {
        uint32_t block_num = get_file_block(&dir_inode, b);
        if (block_num == 0) continue;
        
        if (read_block(block_num, dirents) < 0) return PFS_ERR_IO;
        
        int entries_per_block = PFS_BLOCK_SIZE / sizeof(pfs_dirent_t);
        for (int i = 0; i < entries_per_block; i++) {
            if (dirents[i].inode != 0) {
                return PFS_ERR_NOT_DIR;  // не пуст
            }
        }
    }
    
    return pfs_delete(path);  // удаляем как обычный файл
}

int pfs_readdir(const char* path, pfs_dirent_t* entries, uint32_t max_entries) {
    pfs_inode_t dir_inode;
    if (pfs_lookup(path, &dir_inode) != PFS_OK)
        return PFS_ERR_NOT_FOUND;
    
    if (!(dir_inode.mode & PFS_MODE_DIR))
        return PFS_ERR_NOT_DIR;
    
    uint32_t blocks = (dir_inode.size + PFS_BLOCK_SIZE - 1) / PFS_BLOCK_SIZE;
    pfs_dirent_t block_entries[PFS_BLOCK_SIZE / sizeof(pfs_dirent_t)];
    uint32_t count = 0;
    
    for (uint32_t b = 0; b < blocks && count < max_entries; b++) {
        uint32_t block_num = get_file_block(&dir_inode, b);
        if (block_num == 0) continue;
        
        if (read_block(block_num, block_entries) < 0) return PFS_ERR_IO;
        
        int entries_per_block = PFS_BLOCK_SIZE / sizeof(pfs_dirent_t);
        for (int i = 0; i < entries_per_block && count < max_entries; i++) {
            if (block_entries[i].inode != 0) {
                entries[count++] = block_entries[i];
            }
        }
    }
    
    return count;
}

int pfs_isdir(pfs_inode_t* inode) {
    return (inode->mode & PFS_MODE_DIR) != 0;
}

// ============================================================================
// УТИЛИТЫ
// ============================================================================

int pfs_exists(const char* path) {
    pfs_inode_t inode;
    return pfs_lookup(path, &inode) == PFS_OK;
}

void pfs_stat(const char* path) {
    pfs_inode_t inode;
    if (pfs_lookup(path, &inode) != PFS_OK) {
        serial_puts("[PFS] Not found: ");
        serial_puts(path);
        serial_puts("\n");
        return;
    }
    
    serial_puts("[PFS] ");
    serial_puts(path);
    serial_puts("\n");
    serial_puts("  Size: ");
    serial_puts_num(inode.size);
    serial_puts(" bytes\n");
    serial_puts("  Type: ");
    if (inode.mode & PFS_MODE_DIR) serial_puts("DIR");
    else if (inode.mode & PFS_MODE_FILE) serial_puts("FILE");
    serial_puts("\n");
    serial_puts("  Mode: ");
    if (inode.mode & PFS_MODE_READ) serial_puts("R");
    if (inode.mode & PFS_MODE_WRITE) serial_puts("W");
    serial_puts("\n");
}

int pfs_get_free_blocks(void) {
    return super.free_blocks;
}

int pfs_get_free_inodes(void) {
    return super.free_inodes;
}

// ============================================================================
// ЭКСПОРТИРУЕМЫЕ ВНУТРЕННИЕ ФУНКЦИИ (для syscall.c)
// ============================================================================

uint32_t pfs_get_file_block(pfs_inode_t* inode, uint32_t lblock) {
    return get_file_block(inode, lblock);
}

int pfs_read_block(uint32_t block_num, void* buffer) {
    return read_block(block_num, buffer);
}

int pfs_write_block(uint32_t block_num, const void* buffer) {
    return write_block(block_num, buffer);
}

// ============================================================================
// МОНТИРОВАНИЕ И ФОРМАТИРОВАНИЕ
// ============================================================================

int pfs_mount(int disk_num) {
    if (disk_num < 0 || disk_num >= disk_get_count()) return -1;
    current_disk = disk_num;
    
    if (read_block(1, &super) < 0) {
        serial_puts("[PFS] Failed to read superblock\n");
        return -1;
    }
    
    if (super.magic != PFS_MAGIC) {
        serial_puts("[PFS] No PozitronFS found\n");
        return -1;
    }
    
    if (super.version != PFS_VERSION) {
        serial_puts("[PFS] Unsupported version\n");
        return -1;
    }
    
    serial_puts("[PFS] Mounted: ");
    serial_puts_num(super.total_blocks);
    serial_puts(" blocks, ");
    serial_puts_num(super.free_blocks);
    serial_puts(" free, ");
    serial_puts_num(super.inode_count);
    serial_puts(" inodes (");
    serial_puts_num(super.free_inodes);
    serial_puts(" free)\n");
    
    return PFS_OK;
}

int pfs_format(int disk_num) {
    if (disk_num < 0 || disk_num >= disk_get_count()) return -1;
    current_disk = disk_num;
    
    const disk_info_t* dinfo = disk_get_info(disk_num);
    if (!dinfo) return -1;
    
    uint32_t total_blocks = (uint32_t)dinfo->total_sectors;
    uint32_t inode_count = total_blocks / 16;  // ~6% под inodes
    if (inode_count < 64) inode_count = 64;
    if (inode_count > 65536) inode_count = 65536;
    
    uint32_t bitmap_blocks = (inode_count * 8 + PFS_BLOCK_SIZE - 1) / PFS_BLOCK_SIZE;
    uint32_t inode_blocks = (inode_count * sizeof(pfs_inode_t) + PFS_BLOCK_SIZE - 1) / PFS_BLOCK_SIZE;
    uint32_t first_data_block = 2 + 2 * bitmap_blocks + inode_blocks;
    
    // Заполняем суперблок
    memset(&super, 0, sizeof(super));
    super.magic = PFS_MAGIC;
    super.version = PFS_VERSION;
    super.block_size = PFS_BLOCK_SIZE;
    super.total_blocks = total_blocks;
    super.inode_count = inode_count;
    super.free_blocks = total_blocks - first_data_block;
    super.free_inodes = inode_count;
    super.root_inode = 0;
    super.first_free_block = first_data_block;
    super.first_free_inode = 0;
    
    // Записываем суперблок
    if (write_block(1, &super) < 0) return -1;
    
    // Очищаем битовые карты
    uint8_t zero[PFS_BLOCK_SIZE] = {0};
    for (uint32_t i = 0; i < 2 * bitmap_blocks; i++) {
        write_block(2 + i, zero);
    }
    
    // Очищаем таблицу inodes
    for (uint32_t i = 0; i < inode_blocks; i++) {
        write_block(2 + 2 * bitmap_blocks + i, zero);
    }
    
    // Создаём корневой каталог
    pfs_inode_t root;
    memset(&root, 0, sizeof(root));
    strcpy(root.name, "/");
    root.mode = PFS_MODE_DIR | PFS_MODE_READ | PFS_MODE_WRITE;
    root.refcount = 1;
    pfs_write_inode(0, &root);
    
    super.free_inodes--;
    super.first_free_inode = 1;
    write_block(1, &super);
    
    serial_puts("[PFS] Formatted: ");
    serial_puts_num(total_blocks);
    serial_puts(" blocks, ");
    serial_puts_num(inode_count);
    serial_puts(" inodes\n");
    
    return PFS_OK;
}