#include "loader/elf.h"
#include "fs/vfs.h"
#include "kernel/memory.h"
#include "kernel/paging.h"
#include "drivers/serial.h"
#include "lib/string.h"
#include "kernel/task.h"
#include "kernel/notif.h"

extern page_directory_t* current_directory;

static page_directory_t* elf_create_user_directory(void) {
    page_directory_t* dir = (page_directory_t*)kmalloc_aligned(sizeof(page_directory_t), PAGE_SIZE);
    if (!dir) return NULL;
    
    memset(dir, 0, sizeof(page_directory_t));
    
    if (kernel_page_directory) {
        for (uint32_t i = 0; i < 1024; i++) {
            if (kernel_page_directory->entries[i]) {
                dir->entries[i] = kernel_page_directory->entries[i];
            }
        }
    } else {
        serial_puts("[ELF] ERROR: kernel_page_directory is NULL!\n");
        notif_error("ELF Loader", "Kernel page directory is NULL!");
    }
    
    return dir;
}

static int elf_map_phdr(page_directory_t* dir, Elf32_Phdr* phdr, struct vfs_file* f) {
    if (phdr->p_type != PT_LOAD) return 0;
    if (phdr->p_memsz == 0) return 0;
    
    uint32_t start_vaddr = phdr->p_vaddr & ~(PAGE_SIZE - 1);
    uint32_t end_vaddr = (phdr->p_vaddr + phdr->p_memsz + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    uint32_t start_page = start_vaddr / PAGE_SIZE;
    uint32_t end_page = end_vaddr / PAGE_SIZE;
    
    for (uint32_t i = start_page; i < end_page; i++) {
        uint32_t phys = (uint32_t)kmalloc_aligned(PAGE_SIZE, PAGE_SIZE);
        if (!phys) return -1;
        memset((void*)phys, 0, PAGE_SIZE);
        
        page_table_t* table = NULL;
        uint32_t dir_idx = i / 1024;
        uint32_t table_idx = i % 1024;
        
        if (!(dir->entries[dir_idx] & PAGE_PRESENT)) {
            page_table_t* new_table = (page_table_t*)kmalloc_aligned(sizeof(page_table_t), PAGE_SIZE);
            if (!new_table) return -1;
            memset(new_table, 0, sizeof(page_table_t));
            dir->entries[dir_idx] = (uint32_t)new_table | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
        }
        
        table = (page_table_t*)(dir->entries[dir_idx] & 0xFFFFF000);
        table->entries[table_idx] = phys | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
    }
    
    uint32_t old_cr3;
    asm volatile("mov %%cr3, %0" : "=r"(old_cr3));
    asm volatile("mov %0, %%cr3" : : "r"(dir));
    
    if (phdr->p_filesz > 0) {
        uint32_t bytes_read = 0;
        vfs_lseek(f, phdr->p_offset, 0);
        vfs_read(f, (void*)phdr->p_vaddr, phdr->p_filesz, &bytes_read);
    }
    
    if (phdr->p_memsz > phdr->p_filesz) {
        memset((void*)(phdr->p_vaddr + phdr->p_filesz), 0, 
               phdr->p_memsz - phdr->p_filesz);
    }
    
    asm volatile("mov %0, %%cr3" : : "r"(old_cr3));
    return 0;
}

static int elf_setup_stack(page_directory_t* dir) {
    uint32_t stack_virt = (0x40000000 + 0x100000) - PAGE_SIZE; 
    uint32_t phys = (uint32_t)kmalloc_aligned(PAGE_SIZE, PAGE_SIZE);
    if (!phys) return -1;
    memset((void*)phys, 0, PAGE_SIZE);
    
    uint32_t i = stack_virt / PAGE_SIZE;
    uint32_t dir_idx = i / 1024;
    uint32_t table_idx = i % 1024;
    
    if (!(dir->entries[dir_idx] & PAGE_PRESENT)) {
        page_table_t* new_table = (page_table_t*)kmalloc_aligned(sizeof(page_table_t), PAGE_SIZE);
        if (!new_table) return -1;
        memset(new_table, 0, sizeof(page_table_t));
        dir->entries[dir_idx] = (uint32_t)new_table | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
    }
    
    page_table_t* table = (page_table_t*)(dir->entries[dir_idx] & 0xFFFFF000);
    table->entries[table_idx] = phys | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
    
    return 0;
}

int elf_load(const char* path) {
    struct vfs_file* f = NULL;
    
    int res = vfs_open(path, 0, &f); 
    if (res != 0 || !f) {
        serial_puts("[ELF] Failed to open file\n");
        notif_printf(NOTIF_ERROR, "ELF Loader", "Failed to open file: %s", path);
        return -1;
    }
    
    Elf32_Ehdr ehdr;
    uint32_t bytes = 0;
    vfs_read(f, &ehdr, sizeof(ehdr), &bytes);
    
    if (ehdr.e_ident[EI_MAG0] != ELFMAG0 || ehdr.e_ident[EI_MAG1] != ELFMAG1 ||
        ehdr.e_ident[EI_MAG2] != ELFMAG2 || ehdr.e_ident[EI_MAG3] != ELFMAG3) {
        serial_puts("[ELF] Invalid ELF magic\n");
        vfs_close(f);
        notif_printf(NOTIF_ERROR, "ELF Loader", "Invalid ELF magic format in %s", path);
        return -1;
    }
    
    asm volatile("cli");

    page_directory_t* dir = elf_create_user_directory();
    if (!dir) {
        serial_puts("[ELF] Failed to create page directory\n");
        vfs_close(f);
        asm volatile("sti");
        notif_error("ELF Loader", "Out of memory: target page directory failed");
        return -1;
    }
    
    uint32_t old_cr3;
    asm volatile("mov %%cr3, %0" : "=r"(old_cr3));
    asm volatile("mov %0, %%cr3" : : "r"(dir));
    
    Elf32_Phdr phdr;
    vfs_lseek(f, ehdr.e_phoff, 0);
    for (int i = 0; i < ehdr.e_phnum; i++) {
        uint32_t bytes_read = 0;
        vfs_read(f, &phdr, sizeof(phdr), &bytes_read);
        if (bytes_read != sizeof(phdr)) break;
        
        if (phdr.p_type == PT_LOAD) {
            if (elf_map_phdr(dir, &phdr, f) != 0) {
                serial_puts("[ELF] Failed to map segment\n");
                asm volatile("mov %0, %%cr3" : : "r"(old_cr3));
                vfs_close(f);
                asm volatile("sti");
                notif_printf(NOTIF_ERROR, "ELF Loader", "Failed to map segment %d for %s", i, path);
                return -1;
            }
        }
    }
    
    asm volatile("mov %0, %%cr3" : : "r"(old_cr3));
    vfs_close(f);
    
    if (elf_setup_stack(dir) != 0) {
        serial_puts("[ELF] Failed to setup stack\n");
        asm volatile("sti");
        notif_error("ELF Loader", "Failed to allocate user stack space");
        return -1;
    }
    
    serial_puts("[ELF] Registering process in scheduler...\n");
    task_create((uint32_t)ehdr.e_entry, (uint32_t)dir);
    serial_puts("[ELF] Task successfully created for executable!\n");
    
    asm volatile("sti");
    
    return 0;
}