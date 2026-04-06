#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>
#include "drivers/vesa.h"
#include "core/isr.h"

#define PAGE_SIZE 4096
#define PAGE_TABLE_ENTRIES 1024
#define PAGE_DIR_ENTRIES 1024

#define PAGE_PRESENT   0x01
#define PAGE_WRITABLE  0x02
#define PAGE_USER      0x04
#define PAGE_WRITETHROUGH 0x08
#define PAGE_CACHE_DISABLE 0x10
#define PAGE_ACCESSED  0x20
#define PAGE_DIRTY     0x40
#define PAGE_SIZE_4MB  0x80
#define PAGE_GLOBAL    0x100

typedef struct {
    uint32_t entries[1024];
} page_directory_t;

typedef struct {
    uint32_t entries[1024];
} page_table_t;

void paging_init(void);
page_directory_t* paging_create_directory(void);
void paging_switch_directory(page_directory_t* dir);
void paging_map_page(page_directory_t* dir, uint32_t virt, uint32_t phys, uint32_t flags);
void paging_unmap_page(page_directory_t* dir, uint32_t virt);
uint32_t paging_get_physical(page_directory_t* dir, uint32_t virt);
void page_fault_handler(registers_t* r);

extern page_directory_t* current_directory;

static inline int paging_is_enabled(void) {
    uint32_t cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    return (cr0 & 0x80000000) ? 1 : 0;
}

#endif