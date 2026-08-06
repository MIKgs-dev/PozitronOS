#include "loader/elf_drv_loader.h"
#include "loader/elf.h"
#include "fs/vfs.h"
#include "kernel/memory.h"
#include "kernel/paging.h"
#include "drivers/serial.h"
#include "lib/string.h"
#include "lib/mini_printf.h"

extern page_directory_t* current_directory;

int elf_drv_load(const char* path, elf_drv_load_result_t* result) {
    serial_puts("[ELF_DRV] Loading driver...\n");
    
    struct vfs_file* f = NULL;
    if (vfs_open(path, 0, &f) != 0 || !f) {
        serial_puts("[ELF_DRV] ERROR: Cannot open file\n");
        return -1;
    }
    
    Elf32_Ehdr ehdr;
    uint32_t bytes;
    vfs_read(f, &ehdr, sizeof(ehdr), &bytes);
    
    if (ehdr.e_ident[EI_MAG0] != ELFMAG0 || 
        ehdr.e_ident[EI_MAG1] != ELFMAG1 ||
        ehdr.e_ident[EI_MAG2] != ELFMAG2 || 
        ehdr.e_ident[EI_MAG3] != ELFMAG3) {
        serial_puts("[ELF_DRV] ERROR: Invalid ELF\n");
        vfs_close(f);
        return -1;
    }
    
    Elf32_Phdr phdr;
    void* base = NULL;
    uint32_t total_size = 0;
    
    vfs_lseek(f, ehdr.e_phoff, 0);
    for (int i = 0; i < ehdr.e_phnum; i++) {
        vfs_read(f, &phdr, sizeof(phdr), &bytes);
        if (bytes != sizeof(phdr)) break;
        
        if (phdr.p_type == PT_LOAD) {
            uint32_t start = phdr.p_vaddr & ~(PAGE_SIZE - 1);
            uint32_t end = (phdr.p_vaddr + phdr.p_memsz + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
            uint32_t size = end - start;
            
            void* mem = kmalloc_aligned(size, PAGE_SIZE);
            if (!mem) {
                vfs_close(f);
                return -1;
            }
            memset(mem, 0, size);
            
            for (uint32_t off = 0; off < size; off += PAGE_SIZE) {
                uint32_t phys = virt_to_phys((uint8_t*)mem + off);
                uint32_t vaddr = start + off;
                paging_map_page(current_directory, vaddr, phys, 
                               PAGE_PRESENT | PAGE_WRITABLE);
            }
            
            if (phdr.p_filesz > 0) {
                vfs_lseek(f, phdr.p_offset, 0);
                vfs_read(f, (void*)phdr.p_vaddr, phdr.p_filesz, &bytes);
            }
            
            if (!base) base = (void*)start;
            total_size += size;
        }
    }
    
    vfs_close(f);
    
    if (!base) {
        serial_puts("[ELF_DRV] ERROR: No loadable segments\n");
        return -1;
    }
    
    result->base_addr = base;
    result->size = total_size;
    result->entry_point = (void*)ehdr.e_entry;
    
    serial_printf("[ELF_DRV] Loaded at 0x%x, entry at 0x%x\n", base, ehdr.e_entry);
    return 0;
}

int elf_drv_unload(elf_drv_load_result_t* result) {
    if (!result->base_addr) return -1;
    kfree_aligned(result->base_addr);
    result->base_addr = NULL;
    return 0;
}