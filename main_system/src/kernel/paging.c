#include "kernel/paging.h"
#include "kernel/memory.h"
#include "drivers/serial.h"
#include "lib/string.h"
#include "core/isr.h"
#include "hw/scanner.h"
#include "drivers/pci.h"

extern mem_region_t* memory_regions;
extern uint32_t heap_start;
extern uint32_t acpi_tables_phys_addr;
extern uint32_t acpi_tables_size;

#define ACPI_VIRT_BASE 0x70000000

page_directory_t* current_directory = NULL;
page_directory_t* kernel_directory = NULL;
static uint32_t* page_frames = NULL;
static uint32_t total_frames = 0;
static uint32_t last_alloc_frame = 0;

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
    uint32_t total_entries = (total_frames + 31) / 32;
    
    for (uint32_t i = last_alloc_frame; i < total_entries; i++) {
        if (page_frames[i] != 0xFFFFFFFF) {
            for (uint32_t j = 0; j < 32; j++) {
                uint32_t frame = i * 32 + j;
                if (frame < total_frames && !(page_frames[i] & (1 << j))) {
                    last_alloc_frame = i;
                    return frame;
                }
            }
        }
    }
    
    for (uint32_t i = 0; i < last_alloc_frame && i < total_entries; i++) {
        if (page_frames[i] != 0xFFFFFFFF) {
            for (uint32_t j = 0; j < 32; j++) {
                uint32_t frame = i * 32 + j;
                if (frame < total_frames && !(page_frames[i] & (1 << j))) {
                    last_alloc_frame = i;
                    return frame;
                }
            }
        }
    }
    
    return 0xFFFFFFFF;
}

uint32_t alloc_frame(void) {
    uint32_t frame = find_free_frame();
    if (frame == 0xFFFFFFFF) return 0;
    set_frame(frame);
    return FRAME_TO_ADDR(frame);
}

void free_frame(uint32_t phys) {
    uint32_t frame = ADDR_TO_FRAME(phys);
    if (frame < total_frames) clear_frame(frame);
}

static void mark_frames_used(uint32_t start, uint32_t end) {
    uint32_t start_frame = ADDR_TO_FRAME(start);
    uint32_t end_frame = ADDR_TO_FRAME(end + PAGE_SIZE - 1);
    for (uint32_t frame = start_frame; frame <= end_frame && frame < total_frames; frame++) {
        set_frame(frame);
    }
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
    uint32_t dir_idx = virt >> 22;
    uint32_t table_idx = (virt >> 12) & 0x3FF;
    
    if (!(dir->entries[dir_idx] & PAGE_PRESENT)) {
        page_table_t* table = (page_table_t*)kmalloc_aligned(sizeof(page_table_t), PAGE_SIZE);
        if (!table) return;
        memset(table, 0, sizeof(page_table_t));
        dir->entries[dir_idx] = ((uint32_t)table) | flags | PAGE_PRESENT;
    }
    
    page_table_t* table = (page_table_t*)(dir->entries[dir_idx] & 0xFFFFF000);
    table->entries[table_idx] = (phys & 0xFFFFF000) | (flags & 0xFFF) | PAGE_PRESENT;
    
    asm volatile("invlpg (%0)" : : "r"(virt) : "memory");
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
    extern uint8_t _start, end;
    memory_info_t mem_info = get_memory_info();
    struct fb_info* fb = vesa_get_info();
    
    total_frames = mem_info.total_memory / PAGE_SIZE;
    
    uint32_t frames_size = ((total_frames + 31) / 32) * 4;
    page_frames = (uint32_t*)kmalloc(frames_size);
    if (!page_frames) {
        serial_puts("[PAGING] Failed to allocate page frames\n");
        return;
    }
    memset(page_frames, 0, frames_size);
    
    mark_frames_used(0x00000000, 0x0009FFFF);
    mark_frames_used(0x000A0000, 0x000BFFFF);
    mark_frames_used(0x000C0000, 0x000FFFFF);
    mark_frames_used(0x00100000, 0x00400000);
    mark_frames_used((uint32_t)&_start, (uint32_t)&end);
    mark_frames_used((uint32_t)heap_start, (uint32_t)heap_start + mem_info.heap_size);
    
    if (fb && fb->found && fb->address) {
        mark_frames_used((uint32_t)fb->address, (uint32_t)fb->address + fb->height * fb->pitch);
    }
    
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

    if (acpi_tables_phys_addr != 0 && acpi_tables_size != 0) {
        serial_puts("[PAGING] Explicitly mapping ACPI regions to 0x70000000...\n");
        
        uint32_t pages = (acpi_tables_size + PAGE_SIZE - 1) / PAGE_SIZE;
        
        for (uint32_t i = 0; i < pages; i++) {
            uint32_t v_addr = ACPI_VIRT_BASE + (i * PAGE_SIZE);
            uint32_t p_addr = acpi_tables_phys_addr + (i * PAGE_SIZE);
            
            paging_map_page(kernel_directory, v_addr, p_addr, mmio_flags);
        }
        
        serial_puts("[PAGING] ACPI Mapping complete.\n");
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
    
    for (int i = 0; i < 768; i++) {
        dir->entries[i] = kernel_directory->entries[i];
    }
    for (int i = 768; i < 1024; i++) {
        dir->entries[i] = 0;
    }
    
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
    serial_puts("Address: 0x");
    serial_puts_num_hex(fault_addr);
    serial_puts("\n");
    
    serial_puts("Error code: 0x");
    serial_puts_num_hex(r->err_code);
    serial_puts(" [");
    
    if (r->err_code & PF_PRESENT) {
        serial_puts("PROTECTION ");
    } else {
        serial_puts("NOT_PRESENT ");
    }
    
    if (r->err_code & PF_WRITE) {
        serial_puts("WRITE ");
    } else {
        serial_puts("READ ");
    }
    
    if (r->err_code & PF_USER) {
        serial_puts("USER ");
    } else {
        serial_puts("SUPERVISOR ");
    }
    
    if (r->err_code & PF_INSTR) {
        serial_puts("INSTR_FETCH");
    }
    
    serial_puts("]\n");
    
    serial_puts("EIP: 0x");
    serial_puts_num_hex(r->eip);
    serial_puts("\n");
    
    serial_puts("CS: 0x");
    serial_puts_num_hex(r->cs);
    serial_puts("\n");
    
    serial_puts("EFLAGS: 0x");
    serial_puts_num_hex(r->eflags);
    serial_puts("\n");
    
    uint32_t dir_idx = (fault_addr >> 22) & 0x3FF;
    uint32_t table_idx = (fault_addr >> 12) & 0x3FF;
    uint32_t cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    
    serial_puts("CR3: 0x");
    serial_puts_num_hex(cr3);
    serial_puts("\n");
    
    uint32_t* page_dir = (uint32_t*)cr3;
    uint32_t dir_entry = page_dir[dir_idx];
    
    serial_puts("Page directory entry: 0x");
    serial_puts_num_hex(dir_entry);
    serial_puts("\n");
    
    if (dir_entry & PAGE_PRESENT) {
        uint32_t* page_table = (uint32_t*)(dir_entry & 0xFFFFF000);
        uint32_t pte = page_table[table_idx];
        serial_puts("Page table entry: 0x");
        serial_puts_num_hex(pte);
        serial_puts("\n");
        
        if (pte & PAGE_PRESENT) {
            serial_puts("Physical frame: 0x");
            serial_puts_num_hex(pte & 0xFFFFF000);
            serial_puts("\n");
            serial_puts("Flags: ");
            if (pte & PAGE_WRITABLE) serial_puts("WRITABLE ");
            if (pte & PAGE_USER) serial_puts("USER ");
            if (pte & PAGE_CACHE_DISABLE) serial_puts("CACHE_DISABLE ");
            serial_puts("\n");
        }
    }
    
    serial_puts("\nSystem halted.\n");
    asm volatile("cli; hlt");
    for(;;);
}