#include "fs/vfs.h"
#include "fs/ext2.h"
#include "kernel/memory.h"
#include "drivers/serial.h"
#include "lib/string.h"
#include <stddef.h>

#define MAX_MOUNTS 16
#define MAX_FILESYSTEMS 8
#define MAX_OPEN_FILES 64

typedef struct vfs_mount {
    char mountpoint[256];
    struct vfs_superblock* sb;
    struct vfs_mount* next;
} vfs_mount_t;

static vfs_filesystem_t* filesystems[MAX_FILESYSTEMS];
static int fs_count = 0;

static vfs_mount_t* mounts = NULL;
static struct vfs_file* open_files[MAX_OPEN_FILES];
static int open_file_count = 0;

static struct vfs_inode* root_inode = NULL;

static vfs_mount_t* find_mount(const char* path)
{
    vfs_mount_t* m = mounts;
    vfs_mount_t* best = NULL;
    int best_len = 0;
    int len;
    
    while (m) {
        len = strlen(m->mountpoint);
        if (strncmp(path, m->mountpoint, len) == 0) {
            if (len > best_len) {
                best = m;
                best_len = len;
            }
        }
        m = m->next;
    }
    
    return best;
}

static int path_walk(const char* path, struct vfs_inode** result)
{
    struct vfs_inode* current = root_inode;
    char buffer[256];
    char* token;
    char* saveptr = NULL;
    
    if (!path || !result) return -1;
    if (!current) return -1;
    if (*path == '\0') {
        *result = current;
        return 0;
    }
    
    if (*path == '/') path++;
    if (*path == '\0') {
        *result = current;
        return 0;
    }
    
    strncpy(buffer, path, 255);
    buffer[255] = '\0';
    
    token = strtok(buffer, "/");
    while (token) {
        if (!current->iops || !current->iops->lookup) {
            return -1;
        }
        
        if (current->iops->lookup(current, token, &current) != 0) {
            return -1;
        }
        
        token = strtok(NULL, "/");
    }
    
    *result = current;
    return 0;
}

int vfs_init(void)
{
    serial_puts("[VFS] Initializing...\n");
    
    mounts = NULL;
    open_file_count = 0;
    root_inode = NULL;
    
    memset(filesystems, 0, sizeof(filesystems));
    memset(open_files, 0, sizeof(open_files));
    fs_count = 0;
    
    ext2_init();
    
    serial_puts("[VFS] Initialized\n");
    return 0;
}

int vfs_register_fs(vfs_filesystem_t* fs)
{
    if (!fs || fs_count >= MAX_FILESYSTEMS) return -1;
    
    for (int i = 0; i < fs_count; i++) {
        if (strcmp(filesystems[i]->name, fs->name) == 0) {
            return -1;
        }
    }
    
    filesystems[fs_count++] = fs;
    
    serial_puts("[VFS] Registered filesystem: ");
    serial_puts(fs->name);
    serial_puts("\n");
    
    return 0;
}

vfs_filesystem_t* vfs_get_fs(const char* name)
{
    for (int i = 0; i < fs_count; i++) {
        if (strcmp(filesystems[i]->name, name) == 0) {
            return filesystems[i];
        }
    }
    return NULL;
}

int vfs_mount(const char* device, const char* mountpoint, const char* fstype)
{
    vfs_filesystem_t* fs;
    vfs_mount_t* m;
    disk_t* disk;
    int idx;
    
    if (!device || !mountpoint || !fstype) return -1;
    
    fs = vfs_get_fs(fstype);
    if (!fs) return -1;
    
    idx = atoi(device);
    disk = disk_get(idx);
    if (!disk) return -1;
    
    m = kmalloc(sizeof(vfs_mount_t));
    if (!m) return -1;
    
    memset(m, 0, sizeof(vfs_mount_t));
    strncpy(m->mountpoint, mountpoint, 255);
    m->mountpoint[255] = '\0';
    
    if (fs->mount(disk, &m->sb) != 0) {
        kfree(m);
        return -1;
    }
    
    m->next = mounts;
    mounts = m;
    
    if (strcmp(mountpoint, "/") == 0) {
        root_inode = m->sb->s_root;
        serial_puts("[VFS] Root filesystem mounted\n");
    }
    
    serial_puts("[VFS] Mounted ");
    serial_puts(fstype);
    serial_puts(" on ");
    serial_puts(mountpoint);
    serial_puts("\n");
    
    return 0;
}

int vfs_umount(const char* mountpoint)
{
    vfs_mount_t* m = mounts;
    vfs_mount_t* prev = NULL;
    
    while (m) {
        if (strcmp(m->mountpoint, mountpoint) == 0) {
            if (prev) {
                prev->next = m->next;
            } else {
                mounts = m->next;
            }
            
            if (m->sb && m->sb->sops && m->sb->sops->sync) {
                m->sb->sops->sync(m->sb);
            }
            
            if (m->sb && m->sb->private_data) {
                kfree_aligned(m->sb->private_data);
            }
            if (m->sb && m->sb->s_root) {
                kfree_aligned(m->sb->s_root);
            }
            if (m->sb) {
                kfree_aligned(m->sb);
            }
            
            kfree(m);
            return 0;
        }
        prev = m;
        m = m->next;
    }
    
    return -1;
}

int vfs_open(const char* path, int flags, struct vfs_file** file)
{
    struct vfs_inode* inode;
    struct vfs_file* f;
    int ret;
    
    if (!path || !file) return -1;
    if (open_file_count >= MAX_OPEN_FILES) return -1;
    
    if (path_walk(path, &inode) != 0) {
        if (!(flags & FS_O_CREAT)) return -1;
        
        char dir_path[256];
        char name[256];
        struct vfs_inode* parent;
        
        vfs_path_split(path, dir_path, name);
        if (path_walk(dir_path, &parent) != 0) return -1;
        if (!parent->iops || !parent->iops->create) return -1;
        
        if (parent->iops->create(parent, name, FS_IRUSR | FS_IWUSR, &inode) != 0) {
            return -1;
        }
    }
    
    f = kmalloc(sizeof(struct vfs_file));
    if (!f) return -1;
    
    memset(f, 0, sizeof(struct vfs_file));
    f->f_flags = flags;
    f->f_inode = inode;
    f->fops = inode->fops;
    f->f_pos = 0;
    
    if (flags & FS_O_TRUNC && inode->iops && inode->iops->truncate) {
        inode->iops->truncate(inode, 0);
    }
    
    if (f->fops && f->fops->open) {
        ret = f->fops->open(inode, f);
        if (ret != 0) {
            kfree(f);
            return ret;
        }
    }
    
    open_files[open_file_count++] = f;
    *file = f;
    
    return 0;
}

int vfs_close(struct vfs_file* file)
{
    int i;
    int ret = 0;
    
    if (!file) return -1;
    
    if (file->fops && file->fops->close) {
        ret = file->fops->close(file);
    }
    
    for (i = 0; i < open_file_count; i++) {
        if (open_files[i] == file) {
            open_files[i] = open_files[--open_file_count];
            break;
        }
    }
    
    kfree(file);
    return ret;
}

int vfs_read(struct vfs_file* file, void* buf, uint32_t size, uint32_t* bytes_read)
{
    if (!file || !buf || !bytes_read) return -1;
    if (!file->fops || !file->fops->read) return -1;
    
    return file->fops->read(file, buf, size, bytes_read);
}

int vfs_write(struct vfs_file* file, const void* buf, uint32_t size, uint32_t* bytes_written)
{
    if (!file || !buf || !bytes_written) return -1;
    if (!file->fops || !file->fops->write) return -1;
    
    return file->fops->write(file, buf, size, bytes_written);
}

int vfs_lseek(struct vfs_file* file, uint32_t offset, int whence)
{
    if (!file) return -1;
    if (!file->fops || !file->fops->lseek) return -1;
    
    return file->fops->lseek(file, offset, whence);
}

int vfs_stat(const char* path, struct vfs_inode** inode)
{
    if (!path || !inode) return -1;
    return path_walk(path, inode);
}

int vfs_mkdir(const char* path, uint32_t mode)
{
    struct vfs_inode* parent;
    char dir[256];
    char name[256];
    
    if (!path) return -1;
    
    vfs_path_split(path, dir, name);
    
    if (path_walk(dir, &parent) != 0) return -1;
    if (!parent->iops || !parent->iops->mkdir) return -1;
    
    return parent->iops->mkdir(parent, name, mode | FS_DIRECTORY);
}

int vfs_rmdir(const char* path)
{
    struct vfs_inode* parent;
    char dir[256];
    char name[256];
    
    if (!path) return -1;
    
    vfs_path_split(path, dir, name);
    
    if (path_walk(dir, &parent) != 0) return -1;
    if (!parent->iops || !parent->iops->rmdir) return -1;
    
    return parent->iops->rmdir(parent, name);
}

int vfs_unlink(const char* path)
{
    struct vfs_inode* parent;
    char dir[256];
    char name[256];
    
    if (!path) return -1;
    
    vfs_path_split(path, dir, name);
    
    if (path_walk(dir, &parent) != 0) return -1;
    if (!parent->iops || !parent->iops->unlink) return -1;
    
    return parent->iops->unlink(parent, name);
}

void vfs_path_split(const char* path, char* dir, char* name)
{
    const char* last_slash;
    int len;
    
    if (!path || !dir || !name) return;
    
    last_slash = strrchr(path, '/');
    
    if (!last_slash) {
        dir[0] = '.';
        dir[1] = '\0';
        strcpy(name, path);
        return;
    }
    
    len = last_slash - path;
    if (len == 0) {
        dir[0] = '/';
        dir[1] = '\0';
    } else {
        strncpy(dir, path, len);
        dir[len] = '\0';
    }
    
    strcpy(name, last_slash + 1);
}

int vfs_path_walk(const char* path, struct vfs_inode* start, struct vfs_inode** result)
{
    struct vfs_inode* current = start;
    char buffer[256];
    char* token;
    
    if (!path || !result) return -1;
    if (!start) return -1;
    
    if (*path == '\0') {
        *result = current;
        return 0;
    }
    
    if (*path == '/') path++;
    if (*path == '\0') {
        *result = current;
        return 0;
    }
    
    strncpy(buffer, path, 255);
    buffer[255] = '\0';
    
    token = strtok(buffer, "/");
    while (token) {
        if (!current->iops || !current->iops->lookup) {
            return -1;
        }
        
        if (current->iops->lookup(current, token, &current) != 0) {
            return -1;
        }
        
        token = strtok(NULL, "/");
    }
    
    *result = current;
    return 0;
}