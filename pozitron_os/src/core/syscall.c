#include "core/syscall.h"
#include "core/isr.h"
#include "drivers/serial.h"
#include "drivers/vesa.h"
#include "drivers/keyboard.h"
#include "drivers/timer.h"
#include "kernel/scheduler.h"
#include "kernel/memory.h"
#include "fs/pfs.h"
#include "lib/string.h"

// Структура файлового дескриптора
typedef struct {
    uint32_t inode_num;
    pfs_inode_t inode;
    uint32_t offset;
    uint8_t used;
} file_descriptor_t;

#define MAX_FD 32
static file_descriptor_t fd_table[MAX_FD];
static char current_working_dir[256] = "/";

// Таблица системных вызовов
typedef int (*syscall_t)(uint32_t arg1, uint32_t arg2, uint32_t arg3);
static syscall_t syscall_table[32];

// ============================================================================
// ВНУТРЕННИЕ ФУНКЦИИ
// ============================================================================

static int fd_alloc(void) {
    for (int i = 0; i < MAX_FD; i++) {
        if (!fd_table[i].used) {
            fd_table[i].used = 1;
            fd_table[i].offset = 0;
            return i;
        }
    }
    return -1;
}

static void fd_free(int fd) {
    if (fd >= 0 && fd < MAX_FD) {
        fd_table[fd].used = 0;
    }
}

static file_descriptor_t* fd_get(int fd) {
    if (fd >= 0 && fd < MAX_FD && fd_table[fd].used) {
        return &fd_table[fd];
    }
    return NULL;
}

static char* resolve_path(const char* path, char* resolved) {
    if (path[0] == '/') {
        strcpy(resolved, path);
    } else {
        strcpy(resolved, current_working_dir);
        if (resolved[strlen(resolved)-1] != '/') {
            strcat(resolved, "/");
        }
        strcat(resolved, path);
    }
    
    // Упрощаем путь (убираем /./ и //)
    char* src = resolved;
    char* dst = resolved;
    while (*src) {
        *dst++ = *src++;
        if (src[-1] == '/') {
            while (*src == '/') src++;
        }
    }
    *dst = '\0';
    
    return resolved;
}

// ============================================================================
// РЕАЛИЗАЦИИ СИСТЕМНЫХ ВЫЗОВОВ
// ============================================================================

static int sys_exit(uint32_t code, uint32_t arg2, uint32_t arg3) {
    (void)arg2; (void)arg3;
    serial_puts("[SYSCALL] Process exit with code ");
    serial_puts_num(code);
    serial_puts("\n");
    
    // Закрываем все файловые дескрипторы процесса
    for (int i = 0; i < MAX_FD; i++) {
        fd_table[i].used = 0;
    }
    
    task_exit();
    return 0;
}

static int sys_write(uint32_t fd, uint32_t buf, uint32_t count) {
    if (fd == 1 || fd == 2) {  // stdout или stderr
        char* str = (char*)buf;
        uint32_t written = 0;
        
        for (uint32_t i = 0; i < count; i++) {
            if (str[i] == '\0') break;
            serial_write(str[i]);
            written++;
        }
        
        return written;
    }
    
    file_descriptor_t* f = fd_get(fd);
    if (!f) return -1;
    
    if (!(f->inode.mode & PFS_MODE_WRITE)) return -1;
    
    int res = pfs_write(&f->inode, f->offset, count, (void*)buf);
    if (res > 0) {
        f->offset += res;
    }
    return res;
}

static int sys_read(uint32_t fd, uint32_t buf, uint32_t count) {
    if (fd == 0) {  // stdin
        char* buffer = (char*)buf;
        uint32_t read = 0;
        
        while (read < count) {
            uint8_t scancode = 0;
            while (!(inb(KEYBOARD_STATUS_PORT) & 0x01)) {
                asm volatile("pause");
            }
            scancode = inb(KEYBOARD_DATA_PORT);
            
            if (!(scancode & 0x80)) {
                keyboard_state_t state = {0};
                char c = keyboard_scancode_to_char(scancode, state);
                
                if (c == '\n') {
                    buffer[read++] = '\n';
                    serial_write('\n');
                    break;
                } else if (c == '\b') {
                    if (read > 0) {
                        read--;
                        serial_puts("\b \b");
                    }
                } else if (c >= ' ') {
                    buffer[read++] = c;
                    serial_write(c);
                }
            }
        }
        
        return read;
    }
    
    file_descriptor_t* f = fd_get(fd);
    if (!f) return -1;
    
    if (!(f->inode.mode & PFS_MODE_READ)) return -1;
    
    int res = pfs_read(&f->inode, f->offset, count, (void*)buf);
    if (res > 0) {
        f->offset += res;
    }
    return res;
}

static int sys_open(uint32_t path, uint32_t flags, uint32_t mode) {
    char* filename = (char*)path;
    char resolved[256];
    resolve_path(filename, resolved);
    
    serial_puts("[SYSCALL] Opening: ");
    serial_puts(resolved);
    serial_puts("\n");
    
    pfs_inode_t inode;
    int res = pfs_open(resolved, &inode);
    
    if (res == PFS_ERR_NOT_FOUND && (flags & 0x40)) {
        uint32_t file_mode = PFS_MODE_READ | PFS_MODE_WRITE;
        if (mode & 0x1) file_mode |= PFS_MODE_READ;
        if (mode & 0x2) file_mode |= PFS_MODE_WRITE;
        
        if (pfs_create(resolved, file_mode) != PFS_OK) {
            return -1;
        }
        
        if (pfs_open(resolved, &inode) != PFS_OK) {
            return -1;
        }
    } else if (res != PFS_OK) {
        return -1;
    }
    
    int fd = fd_alloc();
    if (fd < 0) return -1;
    
    uint32_t inode_num;
    if (pfs_path_to_inode(resolved, &inode_num) != PFS_OK) {
        fd_free(fd);
        return -1;
    }
    
    fd_table[fd].inode_num = inode_num;
    fd_table[fd].inode = inode;
    fd_table[fd].offset = 0;
    
    return fd;
}

static int sys_close(uint32_t fd, uint32_t arg2, uint32_t arg3) {
    (void)arg2; (void)arg3;
    
    if (!fd_get(fd)) return -1;
    
    fd_free(fd);
    return 0;
}

static int sys_getpid(uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    (void)arg1; (void)arg2; (void)arg3;
    
    task_t* current = task_get_current();
    if (current) return current->id;
    return 0;
}

static int sys_brk(uint32_t addr, uint32_t arg2, uint32_t arg3) {
    (void)arg2; (void)arg3;
    
    static uint32_t program_break = 0;
    static uint32_t program_break_end = 0;
    
    if (addr == 0) {
        return program_break;
    }
    
    if (addr > program_break_end) {
        uint32_t new_pages = (addr - program_break_end + 0xFFF) & ~0xFFF;
        program_break_end += new_pages;
    }
    
    program_break = addr;
    return program_break;
}

static int sys_sleep(uint32_t ticks, uint32_t arg2, uint32_t arg3) {
    (void)arg2; (void)arg3;
    
    uint32_t start = timer_get_ticks();
    while (timer_get_ticks() - start < ticks) {
        task_yield();
    }
    return 0;
}

static int sys_getcwd(uint32_t buf, uint32_t size, uint32_t arg3) {
    (void)arg3;
    
    char* buffer = (char*)buf;
    uint32_t len = strlen(current_working_dir);
    
    if (len + 1 > size) return -1;
    
    strcpy(buffer, current_working_dir);
    return len;
}

static int sys_chdir(uint32_t path, uint32_t arg2, uint32_t arg3) {
    (void)arg2; (void)arg3;
    
    char* dirname = (char*)path;
    char resolved[256];
    resolve_path(dirname, resolved);
    
    pfs_inode_t inode;
    if (pfs_lookup(resolved, &inode) != PFS_OK) {
        return -1;
    }
    
    if (!(inode.mode & PFS_MODE_DIR)) {
        return -1;
    }
    
    strcpy(current_working_dir, resolved);
    return 0;
}

static int sys_mkdir(uint32_t path, uint32_t mode, uint32_t arg3) {
    (void)mode; (void)arg3;
    
    char* dirname = (char*)path;
    char resolved[256];
    resolve_path(dirname, resolved);
    
    return pfs_mkdir(resolved);
}

static int sys_unlink(uint32_t path, uint32_t arg2, uint32_t arg3) {
    (void)arg2; (void)arg3;
    
    char* filename = (char*)path;
    char resolved[256];
    resolve_path(filename, resolved);
    
    return pfs_delete(resolved);
}

static int sys_opendir(uint32_t path, uint32_t arg2, uint32_t arg3) {
    (void)arg2; (void)arg3;
    
    char* dirname = (char*)path;
    char resolved[256];
    resolve_path(dirname, resolved);
    
    pfs_inode_t inode;
    if (pfs_lookup(resolved, &inode) != PFS_OK) {
        return -1;
    }
    
    if (!(inode.mode & PFS_MODE_DIR)) {
        return -1;
    }
    
    int fd = fd_alloc();
    if (fd < 0) return -1;
    
    uint32_t inode_num;
    if (pfs_path_to_inode(resolved, &inode_num) != PFS_OK) {
        fd_free(fd);
        return -1;
    }
    
    fd_table[fd].inode_num = inode_num;
    fd_table[fd].inode = inode;
    fd_table[fd].offset = 0;
    
    return fd;
}

static int sys_readdir(uint32_t fd, uint32_t dirent_ptr, uint32_t arg3) {
    (void)arg3;
    
    file_descriptor_t* f = fd_get(fd);
    if (!f) return -1;
    
    if (!(f->inode.mode & PFS_MODE_DIR)) return -1;
    
    pfs_dirent_t* dirent = (pfs_dirent_t*)dirent_ptr;
    
    uint32_t block = f->offset / PFS_BLOCK_SIZE;
    uint32_t offset_in_block = f->offset % PFS_BLOCK_SIZE;
    
    pfs_dirent_t block_entries[PFS_BLOCK_SIZE / sizeof(pfs_dirent_t)];
    uint32_t block_num = pfs_get_file_block(&f->inode, block);
    
    if (block_num == 0) return 0;
    
    if (pfs_read_block(block_num, block_entries) < 0) return -1;
    
    int entries_per_block = PFS_BLOCK_SIZE / sizeof(pfs_dirent_t);
    int start_index = offset_in_block / sizeof(pfs_dirent_t);
    
    for (int i = start_index; i < entries_per_block; i++) {
        if (block_entries[i].inode != 0) {
            *dirent = block_entries[i];
            f->offset += sizeof(pfs_dirent_t);
            return 1;
        }
    }
    
    return 0;
}

static int sys_closedir(uint32_t fd, uint32_t arg2, uint32_t arg3) {
    (void)arg2; (void)arg3;
    return sys_close(fd, 0, 0);
}

static int sys_stat(uint32_t path, uint32_t stat_ptr, uint32_t arg3) {
    (void)arg3;
    
    char* filename = (char*)path;
    char resolved[256];
    resolve_path(filename, resolved);
    
    pfs_inode_t inode;
    if (pfs_lookup(resolved, &inode) != PFS_OK) {
        return -1;
    }
    
    uint32_t* stat_buf = (uint32_t*)stat_ptr;
    stat_buf[0] = inode.size;
    stat_buf[1] = inode.mode;
    stat_buf[2] = inode.ctime;
    stat_buf[3] = inode.mtime;
    
    return 0;
}

// ============================================================================
// ИНИЦИАЛИЗАЦИЯ
// ============================================================================

void syscall_init(void) {
    for (int i = 0; i < 32; i++) {
        syscall_table[i] = NULL;
    }
    
    syscall_table[SYS_EXIT]     = sys_exit;
    syscall_table[SYS_WRITE]    = sys_write;
    syscall_table[SYS_READ]     = sys_read;
    syscall_table[SYS_OPEN]     = sys_open;
    syscall_table[SYS_CLOSE]    = sys_close;
    syscall_table[SYS_GETPID]   = sys_getpid;
    syscall_table[SYS_BRK]      = sys_brk;
    syscall_table[SYS_SLEEP]    = sys_sleep;
    syscall_table[SYS_GETCWD]   = sys_getcwd;
    syscall_table[SYS_CHDIR]    = sys_chdir;
    syscall_table[SYS_MKDIR]    = sys_mkdir;
    syscall_table[SYS_UNLINK]   = sys_unlink;
    syscall_table[SYS_OPENDIR]  = sys_opendir;
    syscall_table[SYS_READDIR]  = sys_readdir;
    syscall_table[SYS_CLOSEDIR] = sys_closedir;
    syscall_table[SYS_STAT]     = sys_stat;
    
    for (int i = 0; i < MAX_FD; i++) {
        fd_table[i].used = 0;
    }
    
    isr_install_handler(0x80, syscall_handler);
    
    serial_puts("[SYSCALL] System calls initialized (32 entries)\n");
}

// ============================================================================
// ОБРАБОТЧИК ПРЕРЫВАНИЯ 0x80
// ============================================================================

void syscall_handler(registers_t* r) {
    uint32_t syscall_num = r->eax;
    uint32_t arg1 = r->ebx;
    uint32_t arg2 = r->ecx;
    uint32_t arg3 = r->edx;
    
    if (syscall_num < 32 && syscall_table[syscall_num]) {
        int result = syscall_table[syscall_num](arg1, arg2, arg3);
        r->eax = result;
    } else {
        serial_puts("[SYSCALL] Unknown syscall: ");
        serial_puts_num(syscall_num);
        serial_puts("\n");
        r->eax = -1;
    }
}