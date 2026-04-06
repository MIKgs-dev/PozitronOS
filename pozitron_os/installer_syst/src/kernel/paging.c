#include "kernel/paging.h"
#include "kernel/memory.h"
#include "drivers/serial.h"
#include "lib/string.h"
#include "core/isr.h"
#include "hw/scanner.h"
#include "drivers/pci.h"

extern mem_region_t* memory_regions;
extern uint32_t heap_start;

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

static uint32_t get_bar_size(int bus, int dev, int func, int index) {
    uint32_t reg = 0x10 + index * 4;
    uint32_t original = pci_read32(bus, dev, func, reg);
    
    if (original & 1) return 0;
    if ((original & 0xFFFFFFF0) == 0) return 0;
    
    pci_write32(bus, dev, func, reg, 0xFFFFFFFF);
    uint32_t mask = pci_read32(bus, dev, func, reg);
    pci_write32(bus, dev, func, reg, original);
    
    if (mask == 0) return 0;
    
    uint32_t size = (~(mask & 0xFFFFFFF0)) + 1;
    return size;
}

void paging_map_page(page_directory_t* dir, uint32_t virt, uint32_t phys, uint32_t flags) {
    uint32_t dir_idx = (virt >> 22) & 0x3FF;
    uint32_t table_idx = (virt >> 12) & 0x3FF;
    
    if (!(dir->entries[dir_idx] & PAGE_PRESENT)) {
        page_table_t* table = (page_table_t*)kmalloc_aligned(sizeof(page_table_t), PAGE_SIZE);
        if (!table) {
            serial_puts("[PAGING] CRITICAL: Failed to allocate page table\n");
            return;
        }
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
    if (!dir) return virt;
    
    uint32_t dir_idx = (virt >> 22) & 0x3FF;
    uint32_t table_idx = (virt >> 12) & 0x3FF;
    
    if (!(dir->entries[dir_idx] & PAGE_PRESENT)) return 0;
    
    page_table_t* table = (page_table_t*)(dir->entries[dir_idx] & 0xFFFFF000);
    if (!(table->entries[table_idx] & PAGE_PRESENT)) return 0;
    
    return (table->entries[table_idx] & 0xFFFFF000) | (virt & 0xFFF);
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
    
    serial_puts("[PAGING] Mapping kernel memory (0x0-0x400000)...\n");
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
    
    serial_puts("[PAGING] Mapping IVT and BDA (0x0-0x500)...\n");
    for (uint32_t addr = 0x0; addr < 0x500; addr += PAGE_SIZE) {
        uint32_t dir_idx = (addr >> 22) & 0x3FF;
        uint32_t table_idx = (addr >> 12) & 0x3FF;
        
        if (!(kernel_directory->entries[dir_idx] & PAGE_PRESENT)) {
            page_table_t* table = (page_table_t*)kmalloc_aligned(sizeof(page_table_t), PAGE_SIZE);
            if (!table) {
                serial_puts("[PAGING] Failed to allocate page table for IVT\n");
                return;
            }
            memset(table->entries, 0, sizeof(table->entries));
            kernel_directory->entries[dir_idx] = (uint32_t)table | PAGE_PRESENT | PAGE_WRITABLE;
        }
        
        page_table_t* table = (page_table_t*)(kernel_directory->entries[dir_idx] & 0xFFFFF000);
        table->entries[table_idx] = addr | PAGE_PRESENT;
    }
    
    serial_puts("[PAGING] Mapping video memory (0xA0000-0xC0000)...\n");
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
    
    serial_puts("[PAGING] Mapping EBDA area (0x80000-0xA0000)...\n");
    for (uint32_t addr = 0x80000; addr < 0xA0000; addr += PAGE_SIZE) {
        uint32_t dir_idx = (addr >> 22) & 0x3FF;
        uint32_t table_idx = (addr >> 12) & 0x3FF;
        
        if (!(kernel_directory->entries[dir_idx] & PAGE_PRESENT)) {
            page_table_t* table = (page_table_t*)kmalloc_aligned(sizeof(page_table_t), PAGE_SIZE);
            if (!table) {
                serial_puts("[PAGING] Failed to allocate page table for EBDA\n");
                return;
            }
            memset(table->entries, 0, sizeof(table->entries));
            kernel_directory->entries[dir_idx] = (uint32_t)table | PAGE_PRESENT | PAGE_WRITABLE;
        }
        
        page_table_t* table = (page_table_t*)(kernel_directory->entries[dir_idx] & 0xFFFFF000);
        table->entries[table_idx] = addr | PAGE_PRESENT;
    }
    
    serial_puts("[PAGING] Mapping BIOS area (0xE0000-0x100000)...\n");
    for (uint32_t addr = 0xE0000; addr < 0x100000; addr += PAGE_SIZE) {
        uint32_t dir_idx = (addr >> 22) & 0x3FF;
        uint32_t table_idx = (addr >> 12) & 0x3FF;
        
        if (!(kernel_directory->entries[dir_idx] & PAGE_PRESENT)) {
            page_table_t* table = (page_table_t*)kmalloc_aligned(sizeof(page_table_t), PAGE_SIZE);
            if (!table) {
                serial_puts("[PAGING] Failed to allocate page table for BIOS\n");
                return;
            }
            memset(table->entries, 0, sizeof(table->entries));
            kernel_directory->entries[dir_idx] = (uint32_t)table | PAGE_PRESENT | PAGE_WRITABLE;
        }
        
        page_table_t* table = (page_table_t*)(kernel_directory->entries[dir_idx] & 0xFFFFF000);
        table->entries[table_idx] = addr | PAGE_PRESENT;
    }
    
    struct fb_info* fb = vesa_get_info();
    if (fb && fb->found && fb->address) {
        uint32_t fb_start = (uint32_t)fb->address;
        uint32_t fb_size = fb->height * fb->pitch;
        uint32_t fb_end = fb_start + fb_size;
        
        serial_puts("[PAGING] Mapping framebuffer: 0x");
        serial_puts_num_hex(fb_start);
        serial_puts(" - 0x");
        serial_puts_num_hex(fb_end);
        serial_puts("\n");
        
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
    
    serial_puts("[PAGING] Mapping heap: 0x");
    uint32_t heap_phys = (uint32_t)heap_start & 0xFFFFF000;
    uint32_t heap_end = ((uint32_t)heap_start + mem_info.heap_size + PAGE_SIZE - 1) & 0xFFFFF000;
    serial_puts_num_hex(heap_phys);
    serial_puts(" - 0x");
    serial_puts_num_hex(heap_end);
    serial_puts("\n");
    
    for (uint32_t addr = heap_phys; addr < heap_end; addr += PAGE_SIZE) {
        uint32_t dir_idx = (addr >> 22) & 0x3FF;
        uint32_t table_idx = (addr >> 12) & 0x3FF;
        
        if (!(kernel_directory->entries[dir_idx] & PAGE_PRESENT)) {
            page_table_t* table = (page_table_t*)kmalloc_aligned(sizeof(page_table_t), PAGE_SIZE);
            if (!table) {
                serial_puts("[PAGING] Failed to allocate page table for heap\n");
                return;
            }
            memset(table->entries, 0, sizeof(table->entries));
            kernel_directory->entries[dir_idx] = (uint32_t)table | PAGE_PRESENT | PAGE_WRITABLE;
        }
        
        page_table_t* table = (page_table_t*)(kernel_directory->entries[dir_idx] & 0xFFFFF000);
        table->entries[table_idx] = addr | PAGE_PRESENT | PAGE_WRITABLE;
    }
    
    serial_puts("[PAGING] Mapping fixed MMIO regions...\n");
    
    struct {
        uint32_t start;
        uint32_t end;
        const char* name;
    } fixed_mmio[] = {
        {0xFEC00000, 0xFEC01000, "APIC 1"},
        {0xFEE00000, 0xFEE01000, "APIC 2"},
        {0xFFFC0000, 0xFFFFFFFF, "BIOS ROM"},
    };
    
    uint32_t mmio_flags = PAGE_PRESENT | PAGE_WRITABLE | PAGE_CACHE_DISABLE | PAGE_WRITETHROUGH;
    
    for (int i = 0; i < sizeof(fixed_mmio)/sizeof(fixed_mmio[0]); i++) {
        uint32_t start = fixed_mmio[i].start & 0xFFFFF000;
        uint32_t end = (fixed_mmio[i].end + PAGE_SIZE - 1) & 0xFFFFF000;
        
        serial_puts("[PAGING]   ");
        serial_puts(fixed_mmio[i].name);
        serial_puts(": 0x");
        serial_puts_num_hex(start);
        serial_puts(" - 0x");
        serial_puts_num_hex(end);
        serial_puts("\n");
        
        for (uint32_t addr = start; addr < end; addr += PAGE_SIZE) {
            paging_map_page(kernel_directory, addr, addr, mmio_flags);
        }
    }
    
    if (memory_regions) {
        serial_puts("[PAGING] Scanning memory regions for reserved MMIO...\n");
        mem_region_t* region = memory_regions;
        while (region) {
            if (region->type != MEMORY_TYPE_AVAILABLE && region->base >= 0xC0000000) {
                uint32_t start = region->base & 0xFFFFF000;
                uint32_t end = (region->base + region->size + PAGE_SIZE - 1) & 0xFFFFF000;
                
                serial_puts("[PAGING]   Reserved region: 0x");
                serial_puts_num_hex(start);
                serial_puts(" - 0x");
                serial_puts_num_hex(end);
                serial_puts("\n");
                
                for (uint32_t addr = start; addr < end; addr += PAGE_SIZE) {
                    paging_map_page(kernel_directory, addr, addr, mmio_flags);
                }
            }
            region = region->next;
        }
    }
    
    serial_puts("[PAGING] Mapping PCI device BARs...\n");
    hw_device_t* dev = scanner_get_device_list();
    
    while (dev) {
        if (dev->bus == BUS_PCI) {
            for (int i = 0; i < 6; i++) {
                uint32_t new_bar = pci_read32(dev->pci.bus, dev->pci.device, 
                                             dev->pci.function, 0x10 + i * 4);
                if (new_bar != dev->pci.bars[i]) {
                    serial_puts("[PAGING]   BAR");
                    serial_puts_num(i);
                    serial_puts(" changed: 0x");
                    serial_puts_num_hex(dev->pci.bars[i]);
                    serial_puts(" -> 0x");
                    serial_puts_num_hex(new_bar);
                    serial_puts("\n");
                    dev->pci.bars[i] = new_bar;
                }
            }
        }
        dev = dev->next;
    }
    
    dev = scanner_get_device_list();
    int bar_count = 0;
    uint32_t total_mapped = 0;
    
    while (dev) {
        if (dev->bus == BUS_PCI) {
            for (int i = 0; i < 6; i++) {
                uint32_t bar = dev->pci.bars[i];
                if (bar && !(bar & 1)) {
                    uint32_t base = bar & 0xFFFFFFF0;
                    if (base == 0) continue;
                    
                    uint32_t size = get_bar_size(dev->pci.bus, dev->pci.device, 
                                                 dev->pci.function, i);
                    if (size == 0 || size > 0x10000000) {
                        size = 0x1000;
                    }
                    
                    uint32_t end = base + size;
                    uint32_t start_page = base & 0xFFFFF000;
                    uint32_t end_page = (end + PAGE_SIZE - 1) & 0xFFFFF000;
                    
                    serial_puts("[PAGING]   ");
                    serial_puts(dev->name);
                    serial_puts(" BAR");
                    serial_puts_num(i);
                    serial_puts(": 0x");
                    serial_puts_num_hex(start_page);
                    serial_puts(" - 0x");
                    serial_puts_num_hex(end_page);
                    serial_puts(" (");
                    serial_puts_num(size / 1024);
                    serial_puts(" KB)\n");
                    
                    for (uint32_t addr = start_page; addr < end_page; addr += PAGE_SIZE) {
                        paging_map_page(kernel_directory, addr, addr, mmio_flags);
                        total_mapped++;
                    }
                    bar_count++;
                }
            }
        }
        dev = dev->next;
    }
    
    serial_puts("[PAGING] Mapped ");
    serial_puts_num(bar_count);
    serial_puts(" PCI BARs (");
    serial_puts_num(total_mapped);
    serial_puts(" pages)\n");
    
    current_directory = kernel_directory;
    asm volatile("mov %0, %%cr3" : : "r"(current_directory));
    
    uint32_t cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;
    asm volatile("mov %0, %%cr0" : : "r"(cr0));
    
    extern void memory_paging_activated(void);
    memory_paging_activated();
    
    serial_puts("[PAGING] Initialized and enabled\n");
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
    
    serial_puts("\nSystem halted.\n");
    asm volatile("cli; hlt");
    for(;;);
}