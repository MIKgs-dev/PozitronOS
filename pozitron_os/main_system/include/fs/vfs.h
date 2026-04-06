#ifndef VFS_H
#define VFS_H

#include <stdint.h>
#include "drivers/disk.h"

#define FS_FILE        0x01
#define FS_DIRECTORY   0x02
#define FS_CHARDEV     0x03
#define FS_BLOCKDEV    0x04
#define FS_PIPE        0x05
#define FS_SYMLINK     0x06
#define FS_MOUNTPOINT  0x07

#define FS_IRUSR        0x0100
#define FS_IWUSR        0x0080
#define FS_IXUSR        0x0040
#define FS_IRGRP        0x0020
#define FS_IWGRP        0x0010
#define FS_IXGRP        0x0008
#define FS_IROTH        0x0004
#define FS_IWOTH        0x0002
#define FS_IXOTH        0x0001

#define FS_O_RDONLY     0x0001
#define FS_O_WRONLY     0x0002
#define FS_O_RDWR       0x0003
#define FS_O_CREAT      0x0100
#define FS_O_TRUNC      0x0200
#define FS_O_APPEND     0x0400
#define FS_O_DIRECTORY  0x1000

struct vfs_dirent {
    uint32_t d_ino;
    uint8_t  d_type;
    uint8_t  d_namelen;
    char     d_name[256];
};

struct vfs_inode;
struct vfs_file;
struct vfs_superblock;

typedef struct vfs_file_operations {
    int (*open)(struct vfs_inode* inode, struct vfs_file* file);
    int (*read)(struct vfs_file* file, void* buf, uint32_t size, uint32_t* bytes_read);
    int (*write)(struct vfs_file* file, const void* buf, uint32_t size, uint32_t* bytes_written);
    int (*lseek)(struct vfs_file* file, uint32_t offset, int whence);
    int (*close)(struct vfs_file* file);
    int (*readdir)(struct vfs_file* file, void* dirent, uint32_t* bytes_read);
} vfs_file_operations_t;

typedef struct vfs_inode_operations {
    int (*lookup)(struct vfs_inode* dir, const char* name, struct vfs_inode** result);
    int (*create)(struct vfs_inode* dir, const char* name, uint32_t mode, struct vfs_inode** result);
    int (*mkdir)(struct vfs_inode* dir, const char* name, uint32_t mode);
    int (*rmdir)(struct vfs_inode* dir, const char* name);
    int (*unlink)(struct vfs_inode* dir, const char* name);
    int (*link)(struct vfs_inode* old_inode, struct vfs_inode* dir, const char* name);
    int (*rename)(struct vfs_inode* old_dir, const char* old_name, struct vfs_inode* new_dir, const char* new_name);
    int (*truncate)(struct vfs_inode* inode, uint32_t size);
} vfs_inode_operations_t;

typedef struct vfs_superblock_operations {
    int (*read_inode)(struct vfs_superblock* sb, uint32_t ino, struct vfs_inode** inode);
    int (*write_inode)(struct vfs_superblock* sb, struct vfs_inode* inode);
    int (*sync)(struct vfs_superblock* sb);
} vfs_superblock_operations_t;

struct vfs_inode {
    uint32_t        i_ino;
    uint32_t        i_mode;
    uint32_t        i_uid;
    uint32_t        i_gid;
    uint32_t        i_size;
    uint32_t        i_atime;
    uint32_t        i_mtime;
    uint32_t        i_ctime;
    uint32_t        i_blocks;
    uint32_t        i_nlink;
    void*           private_data;
    struct vfs_superblock* sb;
    vfs_file_operations_t* fops;
    vfs_inode_operations_t* iops;
};

struct vfs_file {
    uint32_t        f_flags;
    uint32_t        f_pos;
    struct vfs_inode* f_inode;
    void*           private_data;
    vfs_file_operations_t* fops;
};

struct vfs_superblock {
    uint32_t        s_magic;
    uint32_t        s_blocksize;
    uint32_t        s_blocks;
    uint32_t        s_inodes;
    uint32_t        s_free_blocks;
    uint32_t        s_free_inodes;
    disk_t*         s_disk;
    void*           private_data;
    struct vfs_inode* s_root;
    vfs_superblock_operations_t* sops;
};

typedef struct vfs_inode vfs_inode_t;
typedef struct vfs_file vfs_file_t;
typedef struct vfs_superblock vfs_superblock_t;

typedef struct vfs_filesystem {
    const char* name;
    uint32_t magic;
    int (*mount)(disk_t* disk, struct vfs_superblock** sb);
} vfs_filesystem_t;

int vfs_init(void);
int vfs_register_fs(vfs_filesystem_t* fs);
vfs_filesystem_t* vfs_get_fs(const char* name);

int vfs_mount(const char* device, const char* mountpoint, const char* fstype);
int vfs_umount(const char* mountpoint);

int vfs_open(const char* path, int flags, struct vfs_file** file);
int vfs_close(struct vfs_file* file);
int vfs_read(struct vfs_file* file, void* buf, uint32_t size, uint32_t* bytes_read);
int vfs_write(struct vfs_file* file, const void* buf, uint32_t size, uint32_t* bytes_written);
int vfs_lseek(struct vfs_file* file, uint32_t offset, int whence);
int vfs_readdir(struct vfs_file* file, struct vfs_dirent* dirent, uint32_t* bytes_read);

int vfs_stat(const char* path, struct vfs_inode** inode);
int vfs_mkdir(const char* path, uint32_t mode);
int vfs_mkdir_p(const char* path, uint32_t mode);
int vfs_rmdir(const char* path);
int vfs_unlink(const char* path);
int vfs_exists(const char* path);
int vfs_copy_file(const char* src, const char* dst);
int vfs_copy_dir(const char* src, const char* dst);
int vfs_copy_template(const char* src, const char* dst);
int vfs_write_config(const char* path, const char* key, const char* value);
char* vfs_read_config(const char* path, const char* key);

void vfs_path_split(const char* path, char* dir, char* name);
int vfs_path_walk(const char* path, struct vfs_inode* start, struct vfs_inode** result);

#endif