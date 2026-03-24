#include "fs/ext2.h"
#include "kernel/memory.h"
#include "drivers/serial.h"
#include "lib/string.h"
#include <stddef.h>

//Проблема была именно в том, 
//что disk_read при чтении
//суперблока перезаписывал
//заголовок большого свободного 
//блока, потому что priv был
//выделен без выравнивания

#define EXT2_BLOCK_SIZE(priv) ((priv)->block_size)
#define EXT2_INODE_SIZE(priv) ((priv)->inode_size)

static int ext2_read_block(struct ext2_private* priv, uint32_t block, void* buf)
{
    uint32_t sector = block * (priv->block_size / 512);
    uint32_t sectors = priv->block_size / 512;
    return disk_read(priv->disk, sector, sectors, buf);
}

static int ext2_write_block(struct ext2_private* priv, uint32_t block, void* buf)
{
    uint32_t sector = block * (priv->block_size / 512);
    uint32_t sectors = priv->block_size / 512;
    return disk_write(priv->disk, sector, sectors, buf);
}

static int ext2_read_bitmap(struct ext2_private* priv, uint32_t group, int inode_bitmap)
{
    uint32_t block;
    if (inode_bitmap) {
        block = priv->groups[group].bg_inode_bitmap;
    } else {
        block = priv->groups[group].bg_block_bitmap;
    }
    return ext2_read_block(priv, block, priv->bitmap_buf);
}

static int ext2_write_bitmap(struct ext2_private* priv, uint32_t group, int inode_bitmap)
{
    uint32_t block;
    if (inode_bitmap) {
        block = priv->groups[group].bg_inode_bitmap;
    } else {
        block = priv->groups[group].bg_block_bitmap;
    }
    return ext2_write_block(priv, block, priv->bitmap_buf);
}

static int ext2_test_bit(uint8_t* bitmap, uint32_t bit)
{
    return (bitmap[bit / 8] >> (bit % 8)) & 1;
}

static void ext2_set_bit(uint8_t* bitmap, uint32_t bit, int value)
{
    if (value) {
        bitmap[bit / 8] |= (1 << (bit % 8));
    } else {
        bitmap[bit / 8] &= ~(1 << (bit % 8));
    }
}

static uint32_t ext2_alloc_inode(struct ext2_private* priv, uint32_t group)
{
    uint32_t i;
    uint32_t inode;
    
    if (group >= priv->groups_count) return 0;
    
    if (ext2_read_bitmap(priv, group, 1) != 0) return 0;
    
    for (i = 0; i < priv->inodes_per_group; i++) {
        if (!ext2_test_bit(priv->bitmap_buf, i)) {
            ext2_set_bit(priv->bitmap_buf, i, 1);
            ext2_write_bitmap(priv, group, 1);
            
            inode = group * priv->inodes_per_group + i + 1;
            priv->groups[group].bg_free_inodes_count--;
            priv->sb.s_free_inodes_count--;
            
            return inode;
        }
    }
    
    return 0;
}

static void ext2_free_inode(struct ext2_private* priv, uint32_t inode)
{
    uint32_t group;
    uint32_t index;
    
    group = (inode - 1) / priv->inodes_per_group;
    index = (inode - 1) % priv->inodes_per_group;
    
    if (group >= priv->groups_count) return;
    
    if (ext2_read_bitmap(priv, group, 1) != 0) return;
    
    ext2_set_bit(priv->bitmap_buf, index, 0);
    ext2_write_bitmap(priv, group, 1);
    
    priv->groups[group].bg_free_inodes_count++;
    priv->sb.s_free_inodes_count++;
}

static uint32_t ext2_alloc_block(struct ext2_private* priv, uint32_t group)
{
    uint32_t i;
    uint32_t block;
    
    if (group >= priv->groups_count) return 0;
    
    if (ext2_read_bitmap(priv, group, 0) != 0) return 0;
    
    for (i = 0; i < priv->blocks_per_group; i++) {
        if (!ext2_test_bit(priv->bitmap_buf, i)) {
            ext2_set_bit(priv->bitmap_buf, i, 1);
            ext2_write_bitmap(priv, group, 0);
            
            block = group * priv->blocks_per_group + i + priv->sb.s_first_data_block;
            priv->groups[group].bg_free_blocks_count--;
            priv->sb.s_free_blocks_count--;
            
            return block;
        }
    }
    
    return 0;
}

static void ext2_free_block(struct ext2_private* priv, uint32_t block)
{
    uint32_t group;
    uint32_t index;
    
    group = (block - priv->sb.s_first_data_block) / priv->blocks_per_group;
    index = (block - priv->sb.s_first_data_block) % priv->blocks_per_group;
    
    if (group >= priv->groups_count) return;
    
    if (ext2_read_bitmap(priv, group, 0) != 0) return;
    
    ext2_set_bit(priv->bitmap_buf, index, 0);
    ext2_write_bitmap(priv, group, 0);
    
    priv->groups[group].bg_free_blocks_count++;
    priv->sb.s_free_blocks_count++;
}

static int ext2_read_inode(struct ext2_private* priv, uint32_t ino, struct ext2_inode* inode)
{
    uint32_t group;
    uint32_t index;
    uint32_t table_block;
    uint32_t offset;
    
    if (ino == 0 || ino > priv->sb.s_inodes_count) return -1;
    
    group = (ino - 1) / priv->inodes_per_group;
    index = (ino - 1) % priv->inodes_per_group;
    table_block = priv->groups[group].bg_inode_table;
    offset = index * priv->inode_size;
    
    if (ext2_read_block(priv, table_block + offset / priv->block_size, priv->inode_buf) != 0) {
        return -1;
    }
    
    memcpy(inode, priv->inode_buf + (offset % priv->block_size), sizeof(struct ext2_inode));
    return 0;
}

static int ext2_write_inode(struct ext2_private* priv, uint32_t ino, struct ext2_inode* inode)
{
    uint32_t group;
    uint32_t index;
    uint32_t table_block;
    uint32_t offset;
    
    if (ino == 0 || ino > priv->sb.s_inodes_count) return -1;
    
    group = (ino - 1) / priv->inodes_per_group;
    index = (ino - 1) % priv->inodes_per_group;
    table_block = priv->groups[group].bg_inode_table;
    offset = index * priv->inode_size;
    
    if (ext2_read_block(priv, table_block + offset / priv->block_size, priv->inode_buf) != 0) {
        return -1;
    }
    
    memcpy(priv->inode_buf + (offset % priv->block_size), inode, sizeof(struct ext2_inode));
    
    return ext2_write_block(priv, table_block + offset / priv->block_size, priv->inode_buf);
}

static int ext2_read_block_from_inode(struct ext2_private* priv, struct ext2_inode* inode, 
                                       uint32_t block_num, uint32_t* block)
{
    uint32_t blocks[512];
    uint32_t indirect[512];
    uint32_t double_indirect[512];
    uint32_t i;
    uint32_t per_block = priv->block_size / 4;
    
    if (block_num < 12) {
        *block = inode->i_block[block_num];
        return 0;
    }
    
    block_num -= 12;
    
    if (block_num < per_block) {
        if (inode->i_block[12] == 0) return -1;
        if (ext2_read_block(priv, inode->i_block[12], indirect) != 0) return -1;
        *block = indirect[block_num];
        return 0;
    }
    
    block_num -= per_block;
    
    if (block_num < per_block * per_block) {
        if (inode->i_block[13] == 0) return -1;
        if (ext2_read_block(priv, inode->i_block[13], double_indirect) != 0) return -1;
        
        i = block_num / per_block;
        if (ext2_read_block(priv, double_indirect[i], indirect) != 0) return -1;
        
        *block = indirect[block_num % per_block];
        return 0;
    }
    
    return -1;
}

static int ext2_write_block_to_inode(struct ext2_private* priv, struct ext2_inode* inode,
                                      uint32_t block_num, uint32_t block)
{
    uint32_t blocks[512];
    uint32_t indirect[512];
    uint32_t double_indirect[512];
    uint32_t i;
    uint32_t per_block = priv->block_size / 4;
    
    if (block_num < 12) {
        inode->i_block[block_num] = block;
        return 0;
    }
    
    block_num -= 12;
    
    if (block_num < per_block) {
        if (inode->i_block[12] == 0) {
            inode->i_block[12] = ext2_alloc_block(priv, 0);
            if (inode->i_block[12] == 0) return -1;
            memset(indirect, 0, priv->block_size);
            ext2_write_block(priv, inode->i_block[12], indirect);
        }
        if (ext2_read_block(priv, inode->i_block[12], indirect) != 0) return -1;
        indirect[block_num] = block;
        return ext2_write_block(priv, inode->i_block[12], indirect);
    }
    
    block_num -= per_block;
    
    if (block_num < per_block * per_block) {
        if (inode->i_block[13] == 0) {
            inode->i_block[13] = ext2_alloc_block(priv, 0);
            if (inode->i_block[13] == 0) return -1;
            memset(double_indirect, 0, priv->block_size);
            ext2_write_block(priv, inode->i_block[13], double_indirect);
        }
        if (ext2_read_block(priv, inode->i_block[13], double_indirect) != 0) return -1;
        
        i = block_num / per_block;
        if (double_indirect[i] == 0) {
            double_indirect[i] = ext2_alloc_block(priv, 0);
            if (double_indirect[i] == 0) return -1;
            memset(indirect, 0, priv->block_size);
            ext2_write_block(priv, double_indirect[i], indirect);
            ext2_write_block(priv, inode->i_block[13], double_indirect);
        }
        
        if (ext2_read_block(priv, double_indirect[i], indirect) != 0) return -1;
        indirect[block_num % per_block] = block;
        return ext2_write_block(priv, double_indirect[i], indirect);
    }
    
    return -1;
}

static int ext2_read_data(struct ext2_private* priv, struct ext2_inode* inode,
                          uint32_t offset, void* buf, uint32_t size, uint32_t* bytes_read)
{
    uint32_t block_num;
    uint32_t block_offset;
    uint32_t block;
    uint32_t read = 0;
    uint32_t to_read;
    
    if (offset >= inode->i_size) {
        *bytes_read = 0;
        return 0;
    }
    if (offset + size > inode->i_size) size = inode->i_size - offset;
    
    block_num = offset / priv->block_size;
    block_offset = offset % priv->block_size;
    
    while (read < size) {
        if (ext2_read_block_from_inode(priv, inode, block_num, &block) != 0) {
            *bytes_read = read;
            return -1;
        }
        
        if (block == 0) {
            to_read = priv->block_size - block_offset;
            if (to_read > size - read) to_read = size - read;
            memset((uint8_t*)buf + read, 0, to_read);
        } else {
            if (ext2_read_block(priv, block, priv->block_buf) != 0) {
                *bytes_read = read;
                return -1;
            }
            
            to_read = priv->block_size - block_offset;
            if (to_read > size - read) to_read = size - read;
            memcpy((uint8_t*)buf + read, priv->block_buf + block_offset, to_read);
        }
        
        read += to_read;
        block_num++;
        block_offset = 0;
    }
    
    *bytes_read = read;
    return 0;
}

static int ext2_write_data(struct ext2_private* priv, struct ext2_inode* inode,
                           uint32_t offset, const void* buf, uint32_t size, uint32_t* bytes_written)
{
    uint32_t block_num;
    uint32_t block_offset;
    uint32_t block;
    uint32_t written = 0;
    uint32_t to_write;
    int new_block;
    
    block_num = offset / priv->block_size;
    block_offset = offset % priv->block_size;
    
    while (written < size) {
        if (ext2_read_block_from_inode(priv, inode, block_num, &block) != 0) {
            block = 0;
        }
        
        new_block = (block == 0);
        
        if (new_block) {
            block = ext2_alloc_block(priv, 0);
            if (block == 0) {
                *bytes_written = written;
                return -1;
            }
            if (ext2_write_block_to_inode(priv, inode, block_num, block) != 0) {
                ext2_free_block(priv, block);
                *bytes_written = written;
                return -1;
            }
        }
        
        if (block_offset != 0 || (to_write = size - written) < priv->block_size) {
            if (ext2_read_block(priv, block, priv->block_buf) != 0) {
                *bytes_written = written;
                return -1;
            }
        }
        
        to_write = priv->block_size - block_offset;
        if (to_write > size - written) to_write = size - written;
        
        memcpy(priv->block_buf + block_offset, (uint8_t*)buf + written, to_write);
        
        if (ext2_write_block(priv, block, priv->block_buf) != 0) {
            *bytes_written = written;
            return -1;
        }
        
        written += to_write;
        block_num++;
        block_offset = 0;
        
        if (offset + written > inode->i_size) {
            inode->i_size = offset + written;
        }
        
        inode->i_blocks = (inode->i_size + priv->block_size - 1) / priv->block_size;
    }
    
    *bytes_written = written;
    return 0;
}

static int ext2_lookup(vfs_inode_t* dir, const char* name, vfs_inode_t** result)
{
    struct ext2_private* priv;
    struct ext2_inode inode;
    struct ext2_dir_entry* entry;
    uint32_t offset = 0;
    uint32_t bytes_read;
    uint32_t entry_offset;
    
    if (!dir || !name || !result) return -1;
    
    priv = (struct ext2_private*)dir->private_data;
    if (!priv) return -1;
    
    if (ext2_read_inode(priv, dir->i_ino, &inode) != 0) return -1;
    
    while (offset < inode.i_size) {
        if (ext2_read_data(priv, &inode, offset, priv->block_buf, priv->block_size, &bytes_read) != 0) {
            return -1;
        }
        if (bytes_read == 0) break;
        
        entry_offset = 0;
        while (entry_offset < bytes_read) {
            entry = (struct ext2_dir_entry*)(priv->block_buf + entry_offset);
            
            if (entry->inode != 0 && entry->name_len == strlen(name) &&
                strncmp(entry->name, name, entry->name_len) == 0) {
                
                *result = kmalloc(sizeof(vfs_inode_t));
                if (!*result) return -1;
                
                memset(*result, 0, sizeof(vfs_inode_t));
                (*result)->i_ino = entry->inode;
                (*result)->i_mode = inode.i_mode;
                (*result)->i_size = inode.i_size;
                (*result)->i_blocks = inode.i_blocks;
                (*result)->private_data = priv;
                (*result)->sb = dir->sb;
                (*result)->fops = dir->fops;
                (*result)->iops = dir->iops;
                
                return 0;
            }
            
            entry_offset += entry->rec_len;
        }
        
        offset += bytes_read;
    }
    
    return -1;
}

static int ext2_read(vfs_file_t* file, void* buf, uint32_t size, uint32_t* bytes_read)
{
    struct ext2_private* priv;
    struct ext2_inode inode;
    
    if (!file || !buf || !bytes_read) return -1;
    
    priv = (struct ext2_private*)file->f_inode->private_data;
    if (!priv) return -1;
    
    if (ext2_read_inode(priv, file->f_inode->i_ino, &inode) != 0) return -1;
    
    return ext2_read_data(priv, &inode, file->f_pos, buf, size, bytes_read);
}

static int ext2_write(vfs_file_t* file, const void* buf, uint32_t size, uint32_t* bytes_written)
{
    struct ext2_private* priv;
    struct ext2_inode inode;
    int ret;
    
    if (!file || !buf || !bytes_written) return -1;
    
    priv = (struct ext2_private*)file->f_inode->private_data;
    if (!priv) return -1;
    
    if (ext2_read_inode(priv, file->f_inode->i_ino, &inode) != 0) return -1;
    
    ret = ext2_write_data(priv, &inode, file->f_pos, buf, size, bytes_written);
    if (ret != 0) return ret;
    
    file->f_pos += *bytes_written;
    
    return ext2_write_inode(priv, file->f_inode->i_ino, &inode);
}

static int ext2_lseek(vfs_file_t* file, uint32_t offset, int whence)
{
    struct ext2_private* priv;
    struct ext2_inode inode;
    uint32_t new_pos;
    
    if (!file) return -1;
    
    priv = (struct ext2_private*)file->f_inode->private_data;
    if (!priv) return -1;
    
    if (ext2_read_inode(priv, file->f_inode->i_ino, &inode) != 0) return -1;
    
    switch (whence) {
        case 0:
            new_pos = offset;
            break;
        case 1:
            new_pos = file->f_pos + offset;
            break;
        case 2:
            new_pos = inode.i_size + offset;
            break;
        default:
            return -1;
    }
    
    if (new_pos > inode.i_size) {
        new_pos = inode.i_size;
    }
    
    file->f_pos = new_pos;
    return 0;
}

static int ext2_readdir(vfs_file_t* file, void* dirent, uint32_t* bytes_read)
{
    struct ext2_private* priv;
    struct ext2_inode inode;
    struct ext2_dir_entry* entry;
    struct vfs_dirent* dent = (struct vfs_dirent*)dirent;
    uint32_t offset = file->f_pos;
    uint32_t bytes;
    uint32_t entry_offset;
    
    if (!file || !dirent || !bytes_read) return -1;
    
    priv = (struct ext2_private*)file->f_inode->private_data;
    if (!priv) return -1;
    
    if (ext2_read_inode(priv, file->f_inode->i_ino, &inode) != 0) return -1;
    
    if (offset >= inode.i_size) {
        *bytes_read = 0;
        return 0;
    }
    
    if (ext2_read_data(priv, &inode, offset, priv->block_buf, priv->block_size, &bytes) != 0) {
        return -1;
    }
    if (bytes == 0) return -1;
    
    entry_offset = 0;
    while (entry_offset < bytes) {
        entry = (struct ext2_dir_entry*)(priv->block_buf + entry_offset);
        
        if (entry->inode != 0) {
            dent->d_ino = entry->inode;
            dent->d_type = entry->file_type;
            dent->d_namelen = entry->name_len;
            memcpy(dent->d_name, entry->name, entry->name_len);
            dent->d_name[entry->name_len] = '\0';
            
            file->f_pos = offset + entry_offset + entry->rec_len;
            *bytes_read = sizeof(struct vfs_dirent);
            return 0;
        }
        
        entry_offset += entry->rec_len;
    }
    
    file->f_pos = offset + bytes;
    *bytes_read = 0;
    return 0;
}

static int ext2_create(vfs_inode_t* dir, const char* name, uint32_t mode, vfs_inode_t** result)
{
    struct ext2_private* priv;
    struct ext2_inode parent_inode;
    struct ext2_inode new_inode;
    struct ext2_dir_entry* entry;
    uint32_t ino;
    uint32_t offset = 0;
    uint32_t bytes;
    uint32_t entry_offset;
    uint32_t rec_len;
    int found_space = 0;
    
    if (!dir || !name || !result) return -1;
    
    priv = (struct ext2_private*)dir->private_data;
    if (!priv) return -1;
    
    if (ext2_read_inode(priv, dir->i_ino, &parent_inode) != 0) return -1;
    
    ino = ext2_alloc_inode(priv, 0);
    if (ino == 0) return -1;
    
    memset(&new_inode, 0, sizeof(struct ext2_inode));
    new_inode.i_mode = mode | EXT2_S_IFREG;
    new_inode.i_uid = 0;
    new_inode.i_gid = 0;
    new_inode.i_links_count = 1;
    new_inode.i_blocks = 0;
    new_inode.i_size = 0;
    new_inode.i_atime = 0;
    new_inode.i_ctime = 0;
    new_inode.i_mtime = 0;
    
    if (ext2_write_inode(priv, ino, &new_inode) != 0) {
        ext2_free_inode(priv, ino);
        return -1;
    }
    
    rec_len = 8 + ((name[0] == '\0') ? 0 : strlen(name));
    if (rec_len < 8) rec_len = 8;
    if (rec_len % 4) rec_len += 4 - (rec_len % 4);
    
    while (offset < parent_inode.i_size) {
        if (ext2_read_data(priv, &parent_inode, offset, priv->block_buf, priv->block_size, &bytes) != 0) {
            ext2_free_inode(priv, ino);
            return -1;
        }
        if (bytes == 0) break;
        
        entry_offset = 0;
        while (entry_offset < bytes) {
            entry = (struct ext2_dir_entry*)(priv->block_buf + entry_offset);
            
            if (entry->inode == 0) {
                if (entry->rec_len >= rec_len) {
                    found_space = 1;
                    break;
                }
            } else if (entry->name_len == strlen(name) &&
                       strncmp(entry->name, name, entry->name_len) == 0) {
                ext2_free_inode(priv, ino);
                return -1;
            }
            
            entry_offset += entry->rec_len;
        }
        
        if (found_space) break;
        offset += bytes;
    }
    
    if (!found_space) {
        offset = parent_inode.i_size;
        if (offset % priv->block_size == 0) {
            uint32_t new_block = ext2_alloc_block(priv, 0);
            if (new_block == 0) {
                ext2_free_inode(priv, ino);
                return -1;
            }
            if (ext2_write_block_to_inode(priv, &parent_inode, offset / priv->block_size, new_block) != 0) {
                ext2_free_block(priv, new_block);
                ext2_free_inode(priv, ino);
                return -1;
            }
            memset(priv->block_buf, 0, priv->block_size);
            ext2_write_block(priv, new_block, priv->block_buf);
        }
        
        if (ext2_read_data(priv, &parent_inode, offset, priv->block_buf, priv->block_size, &bytes) != 0) {
            ext2_free_inode(priv, ino);
            return -1;
        }
        
        entry = (struct ext2_dir_entry*)priv->block_buf;
        entry->inode = 0;
        entry->rec_len = priv->block_size - (offset % priv->block_size);
        
        entry_offset = 0;
        found_space = 1;
    }
    
    entry = (struct ext2_dir_entry*)(priv->block_buf + entry_offset);
    if (entry->rec_len >= rec_len) {
        uint32_t new_rec_len = entry->rec_len - rec_len;
        
        entry->inode = ino;
        entry->name_len = strlen(name);
        entry->file_type = EXT2_FT_REG_FILE;
        memcpy(entry->name, name, entry->name_len);
        entry->rec_len = rec_len;
        
        if (new_rec_len >= 8) {
            struct ext2_dir_entry* next = (struct ext2_dir_entry*)((uint8_t*)entry + rec_len);
            next->inode = 0;
            next->rec_len = new_rec_len;
        }
        
        if (ext2_write_data(priv, &parent_inode, offset + entry_offset,
                           priv->block_buf + entry_offset, rec_len, &bytes) != 0) {
            ext2_free_inode(priv, ino);
            return -1;
        }
        
        parent_inode.i_links_count++;
        ext2_write_inode(priv, dir->i_ino, &parent_inode);
        
        *result = kmalloc(sizeof(vfs_inode_t));
        if (!*result) {
            ext2_free_inode(priv, ino);
            return -1;
        }
        
        memset(*result, 0, sizeof(vfs_inode_t));
        (*result)->i_ino = ino;
        (*result)->i_mode = new_inode.i_mode;
        (*result)->i_size = 0;
        (*result)->i_blocks = 0;
        (*result)->private_data = priv;
        (*result)->sb = dir->sb;
        (*result)->fops = dir->fops;
        (*result)->iops = dir->iops;
        
        return 0;
    }
    
    ext2_free_inode(priv, ino);
    return -1;
}

static int ext2_mkdir(vfs_inode_t* dir, const char* name, uint32_t mode)
{
    struct ext2_private* priv;
    struct ext2_inode parent_inode;
    struct ext2_inode new_inode;
    struct ext2_dir_entry* entry;
    uint32_t ino;
    uint32_t block;
    uint32_t offset = 0;
    uint32_t bytes;
    uint32_t entry_offset;
    uint32_t rec_len;
    int found_space = 0;
    
    if (!dir || !name) return -1;
    
    priv = (struct ext2_private*)dir->private_data;
    if (!priv) return -1;
    
    if (ext2_read_inode(priv, dir->i_ino, &parent_inode) != 0) return -1;
    
    ino = ext2_alloc_inode(priv, 0);
    if (ino == 0) return -1;
    
    block = ext2_alloc_block(priv, 0);
    if (block == 0) {
        ext2_free_inode(priv, ino);
        return -1;
    }
    
    memset(&new_inode, 0, sizeof(struct ext2_inode));
    new_inode.i_mode = mode | EXT2_S_IFDIR;
    new_inode.i_uid = 0;
    new_inode.i_gid = 0;
    new_inode.i_links_count = 2;
    new_inode.i_blocks = 1;
    new_inode.i_size = priv->block_size;
    new_inode.i_block[0] = block;
    new_inode.i_atime = 0;
    new_inode.i_ctime = 0;
    new_inode.i_mtime = 0;
    
    if (ext2_write_inode(priv, ino, &new_inode) != 0) {
        ext2_free_block(priv, block);
        ext2_free_inode(priv, ino);
        return -1;
    }
    
    memset(priv->block_buf, 0, priv->block_size);
    
    entry = (struct ext2_dir_entry*)priv->block_buf;
    entry->inode = ino;
    entry->name_len = 1;
    entry->file_type = EXT2_FT_DIR;
    entry->name[0] = '.';
    entry->rec_len = 12;
    
    entry = (struct ext2_dir_entry*)(priv->block_buf + 12);
    entry->inode = dir->i_ino;
    entry->name_len = 2;
    entry->file_type = EXT2_FT_DIR;
    entry->name[0] = '.';
    entry->name[1] = '.';
    entry->rec_len = priv->block_size - 12;
    
    ext2_write_block(priv, block, priv->block_buf);
    
    rec_len = 8 + strlen(name);
    if (rec_len < 8) rec_len = 8;
    if (rec_len % 4) rec_len += 4 - (rec_len % 4);
    
    while (offset < parent_inode.i_size) {
        if (ext2_read_data(priv, &parent_inode, offset, priv->block_buf, priv->block_size, &bytes) != 0) {
            return -1;
        }
        if (bytes == 0) break;
        
        entry_offset = 0;
        while (entry_offset < bytes) {
            entry = (struct ext2_dir_entry*)(priv->block_buf + entry_offset);
            
            if (entry->inode == 0) {
                if (entry->rec_len >= rec_len) {
                    found_space = 1;
                    break;
                }
            } else if (entry->name_len == strlen(name) &&
                       strncmp(entry->name, name, entry->name_len) == 0) {
                return -1;
            }
            
            entry_offset += entry->rec_len;
        }
        
        if (found_space) break;
        offset += bytes;
    }
    
    if (!found_space) {
        offset = parent_inode.i_size;
        if (offset % priv->block_size == 0) {
            uint32_t new_block = ext2_alloc_block(priv, 0);
            if (new_block == 0) return -1;
            if (ext2_write_block_to_inode(priv, &parent_inode, offset / priv->block_size, new_block) != 0) {
                ext2_free_block(priv, new_block);
                return -1;
            }
            memset(priv->block_buf, 0, priv->block_size);
            ext2_write_block(priv, new_block, priv->block_buf);
        }
        
        if (ext2_read_data(priv, &parent_inode, offset, priv->block_buf, priv->block_size, &bytes) != 0) {
            return -1;
        }
        
        entry = (struct ext2_dir_entry*)priv->block_buf;
        entry->inode = 0;
        entry->rec_len = priv->block_size - (offset % priv->block_size);
        
        entry_offset = 0;
        found_space = 1;
    }
    
    entry = (struct ext2_dir_entry*)(priv->block_buf + entry_offset);
    if (entry->rec_len >= rec_len) {
        uint32_t new_rec_len = entry->rec_len - rec_len;
        
        entry->inode = ino;
        entry->name_len = strlen(name);
        entry->file_type = EXT2_FT_DIR;
        memcpy(entry->name, name, entry->name_len);
        entry->rec_len = rec_len;
        
        if (new_rec_len >= 8) {
            struct ext2_dir_entry* next = (struct ext2_dir_entry*)((uint8_t*)entry + rec_len);
            next->inode = 0;
            next->rec_len = new_rec_len;
        }
        
        if (ext2_write_data(priv, &parent_inode, offset + entry_offset,
                           priv->block_buf + entry_offset, rec_len, &bytes) != 0) {
            return -1;
        }
        
        parent_inode.i_links_count++;
        ext2_write_inode(priv, dir->i_ino, &parent_inode);
        
        return 0;
    }
    
    return -1;
}

static int ext2_unlink(vfs_inode_t* dir, const char* name)
{
    struct ext2_private* priv;
    struct ext2_inode parent_inode;
    struct ext2_inode file_inode;
    struct ext2_dir_entry* entry;
    uint32_t offset = 0;
    uint32_t bytes;
    uint32_t entry_offset;
    uint32_t found_offset = 0;
    uint32_t found_entry_offset = 0;
    uint32_t found_rec_len = 0;
    
    if (!dir || !name) return -1;
    
    priv = (struct ext2_private*)dir->private_data;
    if (!priv) return -1;
    
    if (ext2_read_inode(priv, dir->i_ino, &parent_inode) != 0) return -1;
    
    while (offset < parent_inode.i_size) {
        if (ext2_read_data(priv, &parent_inode, offset, priv->block_buf, priv->block_size, &bytes) != 0) {
            return -1;
        }
        if (bytes == 0) break;
        
        entry_offset = 0;
        while (entry_offset < bytes) {
            entry = (struct ext2_dir_entry*)(priv->block_buf + entry_offset);
            
            if (entry->inode != 0 && entry->name_len == strlen(name) &&
                strncmp(entry->name, name, entry->name_len) == 0) {
                
                found_offset = offset;
                found_entry_offset = entry_offset;
                found_rec_len = entry->rec_len;
                break;
            }
            
            entry_offset += entry->rec_len;
        }
        
        if (found_rec_len) break;
        offset += bytes;
    }
    
    if (!found_rec_len) return -1;
    
    if (ext2_read_inode(priv, ((struct ext2_dir_entry*)(priv->block_buf + found_entry_offset))->inode, &file_inode) != 0) {
        return -1;
    }
    
    if (file_inode.i_mode & EXT2_S_IFDIR) {
        if (file_inode.i_links_count > 2) return -1;
    }
    
    entry = (struct ext2_dir_entry*)(priv->block_buf + found_entry_offset);
    entry->inode = 0;
    
    if (found_entry_offset + found_rec_len < bytes) {
        struct ext2_dir_entry* next = (struct ext2_dir_entry*)((uint8_t*)entry + found_rec_len);
        if (next->inode == 0) {
            entry->rec_len += next->rec_len;
        }
    }
    
    if (ext2_write_data(priv, &parent_inode, found_offset + found_entry_offset,
                       priv->block_buf + found_entry_offset, found_rec_len, &bytes) != 0) {
        return -1;
    }
    
    file_inode.i_links_count--;
    
    if (file_inode.i_links_count == 0) {
        uint32_t i;
        for (i = 0; i < file_inode.i_blocks; i++) {
            uint32_t block;
            if (ext2_read_block_from_inode(priv, &file_inode, i, &block) == 0 && block != 0) {
                ext2_free_block(priv, block);
            }
        }
        ext2_free_inode(priv, ((struct ext2_dir_entry*)(priv->block_buf + found_entry_offset))->inode);
    } else {
        ext2_write_inode(priv, ((struct ext2_dir_entry*)(priv->block_buf + found_entry_offset))->inode, &file_inode);
    }
    
    parent_inode.i_links_count--;
    ext2_write_inode(priv, dir->i_ino, &parent_inode);
    
    return 0;
}

static vfs_file_operations_t ext2_fops = {
    .open = NULL,
    .read = ext2_read,
    .write = ext2_write,
    .lseek = ext2_lseek,
    .close = NULL,
    .readdir = ext2_readdir,
};

static vfs_inode_operations_t ext2_iops = {
    .lookup = ext2_lookup,
    .create = ext2_create,
    .mkdir = ext2_mkdir,
    .rmdir = ext2_unlink,
    .unlink = ext2_unlink,
    .link = NULL,
    .rename = NULL,
};

static void ext2_cleanup_private(struct ext2_private* priv)
{
    if (!priv) return;
    
    if (priv->groups) {
        kfree_aligned(priv->groups);
        priv->groups = NULL;
    }
    if (priv->block_buf) {
        kfree_aligned(priv->block_buf);
        priv->block_buf = NULL;
    }
    if (priv->inode_buf) {
        kfree_aligned(priv->inode_buf);
        priv->inode_buf = NULL;
    }
    if (priv->bitmap_buf) {
        kfree_aligned(priv->bitmap_buf);
        priv->bitmap_buf = NULL;
    }
}

static int ext2_mount(disk_t* disk, vfs_superblock_t** sb)
{
    struct ext2_private* priv;
    vfs_superblock_t* super;
    uint32_t group_desc_blocks;
    uint32_t i;
    uint8_t* sb_buf;
    
    // Выделяем priv с выравниванием 1024, чтобы избежать перекрытия с заголовками кучи
    priv = kmalloc_aligned(sizeof(struct ext2_private), 1024);
    if (!priv) return -1;
    
    memset(priv, 0, sizeof(struct ext2_private));
    priv->disk = disk;
    
    // Используем выровненный буфер для чтения суперблока
    sb_buf = kmalloc_aligned(1024, 1024);
    if (!sb_buf) {
        kfree_aligned(priv);
        return -1;
    }
    
    if (disk_read(disk, 2, 2, sb_buf) != 0) {
        kfree_aligned(sb_buf);
        kfree_aligned(priv);
        return -1;
    }
    
    memcpy(&priv->sb, sb_buf, sizeof(struct ext2_superblock));
    kfree_aligned(sb_buf);
    
    if (priv->sb.s_magic != EXT2_SUPER_MAGIC) {
        kfree_aligned(priv);
        return -1;
    }
    
    priv->block_size = 1024 << priv->sb.s_log_block_size;
    priv->inode_size = priv->sb.s_rev_level == 0 ? 128 : priv->sb.s_inode_size;
    priv->blocks_per_group = priv->sb.s_blocks_per_group;
    priv->inodes_per_group = priv->sb.s_inodes_per_group;
    priv->groups_count = (priv->sb.s_blocks_count + priv->blocks_per_group - 1) / priv->blocks_per_group;
    
    group_desc_blocks = (priv->groups_count * sizeof(struct ext2_group_desc) + priv->block_size - 1) / priv->block_size;
    
    // Выделяем память для дескрипторов групп с выравниванием по размеру блока
    priv->groups = kmalloc_aligned(priv->groups_count * sizeof(struct ext2_group_desc), priv->block_size);
    if (!priv->groups) {
        kfree_aligned(priv);
        return -1;
    }
    memset(priv->groups, 0, priv->groups_count * sizeof(struct ext2_group_desc));
    
    // Читаем дескрипторы групп
    for (i = 0; i < group_desc_blocks; i++) {
        uint32_t block = priv->sb.s_first_data_block + 1 + i;
        uint32_t offset = i * priv->block_size;
        
        if (disk_read(disk, block * (priv->block_size / 512), priv->block_size / 512,
                      (uint8_t*)priv->groups + offset) != 0) {
            ext2_cleanup_private(priv);
            kfree_aligned(priv);
            return -1;
        }
    }
    
    // Выделяем буферы с выравниванием по размеру блока
    priv->block_buf = kmalloc_aligned(priv->block_size, priv->block_size);
    priv->inode_buf = kmalloc_aligned(priv->block_size, priv->block_size);
    priv->bitmap_buf = kmalloc_aligned(priv->block_size, priv->block_size);
    
    if (!priv->block_buf || !priv->inode_buf || !priv->bitmap_buf) {
        ext2_cleanup_private(priv);
        kfree_aligned(priv);
        return -1;
    }
    
    super = kmalloc(sizeof(vfs_superblock_t));
    if (!super) {
        ext2_cleanup_private(priv);
        kfree_aligned(priv);
        return -1;
    }
    
    memset(super, 0, sizeof(vfs_superblock_t));
    super->s_magic = EXT2_SUPER_MAGIC;
    super->s_blocksize = priv->block_size;
    super->s_blocks = priv->sb.s_blocks_count;
    super->s_inodes = priv->sb.s_inodes_count;
    super->s_free_blocks = priv->sb.s_free_blocks_count;
    super->s_free_inodes = priv->sb.s_free_inodes_count;
    super->s_disk = disk;
    super->private_data = priv;
    super->sops = NULL;
    super->s_root = kmalloc(sizeof(vfs_inode_t));
    
    if (!super->s_root) {
        ext2_cleanup_private(priv);
        kfree(super);
        kfree_aligned(priv);
        return -1;
    }
    
    memset(super->s_root, 0, sizeof(vfs_inode_t));
    super->s_root->i_ino = EXT2_ROOT_INO;
    super->s_root->private_data = priv;
    super->s_root->sb = super;
    super->s_root->fops = &ext2_fops;
    super->s_root->iops = &ext2_iops;
    
    *sb = super;
    
    serial_puts("[EXT2] Mounted: blocks=");
    serial_puts_num(priv->sb.s_blocks_count);
    serial_puts(" inodes=");
    serial_puts_num(priv->sb.s_inodes_count);
    serial_puts(" block_size=");
    serial_puts_num(priv->block_size);
    serial_puts("\n");
    
    return 0;
}

static int ext2_format_group(struct ext2_private* priv, uint32_t group)
{
    uint32_t block_bitmap_block;
    uint32_t inode_bitmap_block;
    uint32_t inode_table_block;
    uint32_t i;
    uint8_t* buf;
    
    buf = kmalloc_aligned(priv->block_size, priv->block_size);
    if (!buf) return -1;
    
    block_bitmap_block = priv->groups[group].bg_block_bitmap;
    inode_bitmap_block = priv->groups[group].bg_inode_bitmap;
    inode_table_block = priv->groups[group].bg_inode_table;
    
    memset(buf, 0, priv->block_size);
    
    if (group == 0) {
        for (i = 0; i < priv->sb.s_first_data_block + 1; i++) {
            ext2_set_bit(buf, i, 1);
        }
        ext2_set_bit(buf, block_bitmap_block - priv->sb.s_first_data_block, 1);
        ext2_set_bit(buf, inode_bitmap_block - priv->sb.s_first_data_block, 1);
        for (i = 0; i < (priv->inodes_per_group * priv->inode_size + priv->block_size - 1) / priv->block_size; i++) {
            ext2_set_bit(buf, inode_table_block + i - priv->sb.s_first_data_block, 1);
        }
    }
    
    ext2_write_block(priv, block_bitmap_block, buf);
    
    memset(buf, 0, priv->block_size);
    if (group == 0) {
        ext2_set_bit(buf, 0, 1);
    }
    ext2_write_block(priv, inode_bitmap_block, buf);
    
    memset(buf, 0, priv->block_size);
    for (i = 0; i < (priv->inodes_per_group * priv->inode_size + priv->block_size - 1) / priv->block_size; i++) {
        ext2_write_block(priv, inode_table_block + i, buf);
    }
    
    kfree_aligned(buf);
    return 0;
}

int ext2_format(disk_t* disk)
{
    struct ext2_superblock sb;
    struct ext2_private priv;
    uint32_t block_size;
    uint32_t blocks_per_group;
    uint32_t inodes_per_group;
    uint32_t total_blocks;
    uint32_t total_inodes;
    uint32_t groups;
    uint32_t i, j;
    uint32_t block;
    uint8_t* buf;
    
    serial_puts("[EXT2] Formatting disk...\n");
    
    total_blocks = disk->sectors / (1024 / 512);
    
    if (total_blocks < 2048) {
        serial_puts("[EXT2] Disk too small for ext2\n");
        return -1;
    }
    
    block_size = 1024;
    blocks_per_group = 8192;
    inodes_per_group = 2048;
    
    if (total_blocks > 32768) {
        block_size = 4096;
        blocks_per_group = 32768;
        inodes_per_group = 8192;
    } else if (total_blocks > 8192) {
        block_size = 2048;
        blocks_per_group = 16384;
        inodes_per_group = 4096;
    }
    
    groups = (total_blocks + blocks_per_group - 1) / blocks_per_group;
    total_inodes = groups * inodes_per_group;
    
    memset(&sb, 0, sizeof(sb));
    sb.s_inodes_count = total_inodes;
    sb.s_blocks_count = total_blocks;
    sb.s_r_blocks_count = total_blocks / 20;
    sb.s_free_blocks_count = total_blocks;
    sb.s_free_inodes_count = total_inodes;
    sb.s_first_data_block = (block_size == 1024) ? 1 : 0;
    sb.s_log_block_size = (block_size == 1024) ? 0 : (block_size == 2048) ? 1 : 2;
    sb.s_log_frag_size = sb.s_log_block_size;
    sb.s_blocks_per_group = blocks_per_group;
    sb.s_frags_per_group = blocks_per_group;
    sb.s_inodes_per_group = inodes_per_group;
    sb.s_mtime = 0;
    sb.s_wtime = 0;
    sb.s_mnt_count = 0;
    sb.s_max_mnt_count = 0xFFFF;
    sb.s_magic = EXT2_SUPER_MAGIC;
    sb.s_state = 1;
    sb.s_errors = 1;
    sb.s_minor_rev_level = 0;
    sb.s_lastcheck = 0;
    sb.s_checkinterval = 0;
    sb.s_creator_os = 0;
    sb.s_rev_level = 0;
    sb.s_def_resuid = 0;
    sb.s_def_resgid = 0;
    sb.s_first_ino = 11;
    sb.s_inode_size = 128;
    sb.s_block_group_nr = 0;
    sb.s_feature_compat = 0;
    sb.s_feature_incompat = 0;
    sb.s_feature_ro_compat = 0;
    memcpy(sb.s_volume_name, "PozitronOS", 10);
    
    priv.disk = disk;
    priv.block_size = block_size;
    priv.sb = sb;
    priv.blocks_per_group = blocks_per_group;
    priv.inodes_per_group = inodes_per_group;
    priv.groups_count = groups;
    
    buf = kmalloc_aligned(block_size, block_size);
    if (!buf) return -1;
    
    if (sb.s_first_data_block == 0) {
        memset(buf, 0, block_size);
        ext2_write_block(&priv, 0, buf);
    }
    
    memset(buf, 0, block_size);
    memcpy(buf, &sb, sizeof(sb));
    ext2_write_block(&priv, sb.s_first_data_block, buf);
    
    priv.groups = kmalloc(groups * sizeof(struct ext2_group_desc));
    if (!priv.groups) {
        kfree(buf);
        return -1;
    }
    
    block = sb.s_first_data_block + 1;
    for (i = 0; i < groups; i++) {
        priv.groups[i].bg_block_bitmap = block++;
        priv.groups[i].bg_inode_bitmap = block++;
        priv.groups[i].bg_inode_table = block;
        block += (inodes_per_group * 128 + block_size - 1) / block_size;
        priv.groups[i].bg_free_blocks_count = blocks_per_group;
        priv.groups[i].bg_free_inodes_count = inodes_per_group;
        priv.groups[i].bg_used_dirs_count = 0;
    }
    
    for (i = 0; i < groups; i++) {
        ext2_format_group(&priv, i);
    }
    
    memset(buf, 0, block_size);
    for (i = 0; i < groups; i++) {
        memcpy(buf + (i % (block_size / sizeof(struct ext2_group_desc))) * sizeof(struct ext2_group_desc),
               &priv.groups[i], sizeof(struct ext2_group_desc));
        if ((i + 1) % (block_size / sizeof(struct ext2_group_desc)) == 0 || i == groups - 1) {
            ext2_write_block(&priv, sb.s_first_data_block + 1 + i / (block_size / sizeof(struct ext2_group_desc)), buf);
            memset(buf, 0, block_size);
        }
    }
    
    kfree(priv.groups);
    
    struct ext2_inode root_inode;
    memset(&root_inode, 0, sizeof(root_inode));
    root_inode.i_mode = EXT2_S_IFDIR | 0755;
    root_inode.i_uid = 0;
    root_inode.i_gid = 0;
    root_inode.i_links_count = 2;
    root_inode.i_blocks = 0;
    root_inode.i_size = 0;
    root_inode.i_atime = 0;
    root_inode.i_ctime = 0;
    root_inode.i_mtime = 0;
    
    uint32_t root_inode_block = ext2_alloc_inode(&priv, 0);
    if (root_inode_block != EXT2_ROOT_INO) {
        kfree(buf);
        return -1;
    }
    
    ext2_write_inode(&priv, EXT2_ROOT_INO, &root_inode);
    
    buf = kmalloc_aligned(block_size, block_size);
    
    serial_puts("[EXT2] Format complete\n");
    return 0;
}

int ext2_check(disk_t* disk)
{
    struct ext2_superblock sb;
    
    if (disk_read(disk, 2, 2, &sb) != 0) return 0;
    
    return (sb.s_magic == EXT2_SUPER_MAGIC);
}

static vfs_filesystem_t ext2_fs = {
    .name = "ext2",
    .magic = EXT2_SUPER_MAGIC,
    .mount = ext2_mount,
};

void ext2_init(void)
{
    vfs_register_fs(&ext2_fs);
}