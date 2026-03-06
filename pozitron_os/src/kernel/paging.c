#include "kernel/paging.h"
#include "kernel/memory.h"
#include "drivers/serial.h"
#include "lib/string.h"
#include "core/isr.h"

page_directory_t* current_directory = NULL;
static page_directory_t* kernel_directory = NULL;
static uint32_t* page_frames = NULL;
static uint32_t total_frames = 0;

#define ADDR_TO_FRAME(addr) ((uint32_t)(addr) / PAGE_SIZE)
#define FRAME_TO_ADDR(frame) ((uint32_t)(frame) * PAGE_SIZE)

static void set_frame(uint32_t frame) {
    if (frame >= total_frames) return;
    uint32_t idx = frame / 32;
    uint32_t bit = frame % 32;
    page_frames[idx] |= (1 << bit);
}

static void clear_frame(uint32_t frame) {
    if (frame >= total_frames) return;
    uint32_t idx = frame / 32;
    uint32_t bit = frame % 32;
    page_frames[idx] &= ~(1 << bit);
}

static uint32_t find_free_frame(void) {
    for (uint32_t i = 0; i < (total_frames + 31) / 32; i++) {
        if (page_frames[i] != 0xFFFFFFFF) {
            for (uint32_t j = 0; j < 32; j++) {
                uint32_t frame = i * 32 + j;
                if (frame < total_frames && !(page_frames[i] & (1 << j))) {
                    return frame;
                }
            }
        }
    }
    return 0xFFFFFFFF;
}

static uint32_t alloc_frame(void) {
    uint32_t frame = find_free_frame();
    if (frame == 0xFFFFFFFF) return 0;
    set_frame(frame);
    return FRAME_TO_ADDR(frame);
}

void paging_init(void) {
    memory_info_t mem_info = get_memory_info();
    total_frames = mem_info.total_memory / PAGE_SIZE;
    
    uint32_t frames_size = ((total_frames + 31) / 32) * 4;
    page_frames = (uint32_t*)kmalloc(frames_size);
    if (!page_frames) {
        serial_puts("[PAGING] Failed to allocate page frames\n");
        return;
    }
    memset(page_frames, 0, frames_size);
    
    kernel_directory = (page_directory_t*)kmalloc_aligned(sizeof(page_directory_t), PAGE_SIZE);
    if (!kernel_directory) {
        serial_puts("[PAGING] Failed to allocate kernel directory\n");
        return;
    }
    memset(kernel_directory->entries, 0, sizeof(kernel_directory->entries));
    
    for (uint32_t addr = 0; addr < 0x400000; addr += PAGE_SIZE) {
        uint32_t dir_idx = (addr >> 22) & 0x3FF;
        uint32_t table_idx = (addr >> 12) & 0x3FF;
        
        if (!(kernel_directory->entries[dir_idx] & PAGE_PRESENT)) {
            page_table_t* table = (page_table_t*)kmalloc_aligned(sizeof(page_table_t), PAGE_SIZE);
            if (!table) {
                serial_puts("[PAGING] Failed to allocate page table\n");
                return;
            }
            memset(table->entries, 0, sizeof(table->entries));
            kernel_directory->entries[dir_idx] = (uint32_t)table | PAGE_PRESENT | PAGE_WRITABLE;
        }
        
        page_table_t* table = (page_table_t*)(kernel_directory->entries[dir_idx] & 0xFFFFF000);
        table->entries[table_idx] = addr | PAGE_PRESENT | PAGE_WRITABLE;
        
        uint32_t frame = ADDR_TO_FRAME(addr);
        if (frame < total_frames) set_frame(frame);
    }
    
    for (uint32_t addr = 0xA0000; addr < 0xC0000; addr += PAGE_SIZE) {
        uint32_t dir_idx = (addr >> 22) & 0x3FF;
        uint32_t table_idx = (addr >> 12) & 0x3FF;
        
        if (!(kernel_directory->entries[dir_idx] & PAGE_PRESENT)) {
            page_table_t* table = (page_table_t*)kmalloc_aligned(sizeof(page_table_t), PAGE_SIZE);
            if (!table) {
                serial_puts("[PAGING] Failed to allocate page table\n");
                return;
            }
            memset(table->entries, 0, sizeof(table->entries));
            kernel_directory->entries[dir_idx] = (uint32_t)table | PAGE_PRESENT | PAGE_WRITABLE;
        }
        
        page_table_t* table = (page_table_t*)(kernel_directory->entries[dir_idx] & 0xFFFFF000);
        table->entries[table_idx] = addr | PAGE_PRESENT | PAGE_WRITABLE;
    }
    
    struct fb_info* fb = vesa_get_info();
    if (fb && fb->found && fb->address) {
        uint32_t fb_start = (uint32_t)fb->address;
        uint32_t fb_size = fb->height * fb->pitch;
        uint32_t fb_end = fb_start + fb_size;
        
        for (uint32_t addr = fb_start; addr < fb_end; addr += PAGE_SIZE) {
            uint32_t dir_idx = (addr >> 22) & 0x3FF;
            uint32_t table_idx = (addr >> 12) & 0x3FF;
            
            if (!(kernel_directory->entries[dir_idx] & PAGE_PRESENT)) {
                page_table_t* table = (page_table_t*)kmalloc_aligned(sizeof(page_table_t), PAGE_SIZE);
                if (!table) {
                    serial_puts("[PAGING] Failed to allocate page table for framebuffer\n");
                    return;
                }
                memset(table->entries, 0, sizeof(table->entries));
                kernel_directory->entries[dir_idx] = (uint32_t)table | PAGE_PRESENT | PAGE_WRITABLE;
            }
            
            page_table_t* table = (page_table_t*)(kernel_directory->entries[dir_idx] & 0xFFFFF000);
            table->entries[table_idx] = addr | PAGE_PRESENT | PAGE_WRITABLE;
        }
    }
    
    current_directory = kernel_directory;
    asm volatile("mov %0, %%cr3" : : "r"(current_directory));
    
    serial_puts("[PAGING] Initialized\n");
}

page_directory_t* paging_create_directory(void) {
    page_directory_t* dir = (page_directory_t*)kmalloc_aligned(sizeof(page_directory_t), PAGE_SIZE);
    if (!dir) return NULL;
    
    memcpy(dir, kernel_directory, sizeof(page_directory_t));
    return dir;
}

void paging_switch_directory(page_directory_t* dir) {
    current_directory = dir;
    asm volatile("mov %0, %%cr3" : : "r"(dir));
}

void paging_map_page(page_directory_t* dir, uint32_t virt, uint32_t phys, uint32_t flags) {
    uint32_t dir_idx = (virt >> 22) & 0x3FF;
    uint32_t table_idx = (virt >> 12) & 0x3FF;
    
    if (!(dir->entries[dir_idx] & PAGE_PRESENT)) {
        page_table_t* table = (page_table_t*)kmalloc_aligned(sizeof(page_table_t), PAGE_SIZE);
        if (!table) return;
        memset(table->entries, 0, sizeof(table->entries));
        dir->entries[dir_idx] = (uint32_t)table | PAGE_PRESENT | PAGE_WRITABLE | (flags & PAGE_USER);
    }
    
    page_table_t* table = (page_table_t*)(dir->entries[dir_idx] & 0xFFFFF000);
    table->entries[table_idx] = (phys & 0xFFFFF000) | flags | PAGE_PRESENT;
    
    asm volatile("invlpg (%0)" : : "r"(virt));
}

void paging_unmap_page(page_directory_t* dir, uint32_t virt) {
    uint32_t dir_idx = (virt >> 22) & 0x3FF;
    uint32_t table_idx = (virt >> 12) & 0x3FF;
    
    if (!(dir->entries[dir_idx] & PAGE_PRESENT)) return;
    
    page_table_t* table = (page_table_t*)(dir->entries[dir_idx] & 0xFFFFF000);
    table->entries[table_idx] = 0;
    
    asm volatile("invlpg (%0)" : : "r"(virt));
}

uint32_t paging_get_physical(page_directory_t* dir, uint32_t virt) {
    uint32_t dir_idx = (virt >> 22) & 0x3FF;
    uint32_t table_idx = (virt >> 12) & 0x3FF;
    
    if (!(dir->entries[dir_idx] & PAGE_PRESENT)) return 0;
    
    page_table_t* table = (page_table_t*)(dir->entries[dir_idx] & 0xFFFFF000);
    if (!(table->entries[table_idx] & PAGE_PRESENT)) return 0;
    
    return (table->entries[table_idx] & 0xFFFFF000) | (virt & 0xFFF);
}

void page_fault_handler(registers_t* r) {
    uint32_t fault_addr;
    asm volatile("mov %%cr2, %0" : "=r"(fault_addr));
    
    serial_puts("\n*** PAGE FAULT ***\n");
    serial_puts("Address: 0x"); serial_puts_num_hex(fault_addr); serial_puts("\n");
    serial_puts("Error:   0x"); serial_puts_num_hex(r->err_code); serial_puts("\n");
    serial_puts("EIP:     0x"); serial_puts_num_hex(r->eip); serial_puts("\n");
    
    if (r->err_code & 0x1) serial_puts("  Protection violation\n");
    else serial_puts("  Non-present page\n");
    
    if (r->err_code & 0x2) serial_puts("  Write\n");
    else serial_puts("  Read\n");
    
    if (r->err_code & 0x4) serial_puts("  User mode\n");
    else serial_puts("  Supervisor mode\n");
    
    serial_puts("System halted.\n");
    
    asm volatile("cli; hlt");
    for(;;);
}