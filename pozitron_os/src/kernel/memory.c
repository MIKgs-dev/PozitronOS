#include "kernel/memory.h"
#include "drivers/serial.h"
#include "drivers/vga.h"
#include <stddef.h>
#include "lib/string.h"

#if USE_ADVANCED_ALLOCATOR

// ============ ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ ============
mem_block_t* heap_start = NULL;
mem_block_t* heap_end = NULL;
uint32_t heap_total = 0;
int heap_initialized = 0;

mem_region_t* memory_regions = NULL;
static mem_region_t* heap_region = NULL;
static memory_info_t mem_info = {0};
static kernel_info_t kernel_info = {0};

#define MAX_RESERVED_AREAS 32
static reserved_area_t reserved_areas[MAX_RESERVED_AREAS];
static uint32_t reserved_areas_count = 0;

#define MAX_MEM_REGIONS 64
static mem_region_t mem_regions_buffer[MAX_MEM_REGIONS];
static uint32_t mem_regions_count = 0;

static int paging_active = 0;

#else
static uint8_t heap[HEAP_SIZE];
static uint8_t heap_used[HEAP_SIZE / BLOCK_SIZE] = {0};
#endif

// ============ ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ ============
static void safe_strcpy(char* dest, const char* src, size_t max_len) {
    size_t i;
    for (i = 0; i < max_len - 1 && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    dest[i] = '\0';
}

static void detect_kernel_bounds(void) {
    extern uint8_t _start;
    extern uint8_t end;
    
    kernel_info.start = (uint32_t)&_start;
    kernel_info.end = (uint32_t)&end;
    kernel_info.size = kernel_info.end - kernel_info.start;
    
    serial_puts("[MEM] Kernel bounds: 0x");
    serial_puts_num_hex(kernel_info.start);
    serial_puts(" - 0x");
    serial_puts_num_hex(kernel_info.end);
    serial_puts(" (");
    serial_puts_num(kernel_info.size / 1024);
    serial_puts(" KB)\n");
}

static void add_reserved_area(uint32_t start, uint32_t end, const char* description) {
    if (reserved_areas_count >= MAX_RESERVED_AREAS) {
        serial_puts("[MEM] WARNING: Too many reserved areas\n");
        return;
    }
    
    reserved_area_t* area = &reserved_areas[reserved_areas_count++];
    area->start = start;
    area->end = end;
    area->description = description;
}

static void init_reserved_areas(void) {
    add_reserved_area(kernel_info.start, kernel_info.end, "Kernel");
    add_reserved_area(0x00000000, 0x00000500, "Interrupt Vector Table");
    add_reserved_area(0x00000500, 0x00007BFF, "BIOS Data Area");
    add_reserved_area(0x00007C00, 0x00007DFF, "MBR/Boot Sector");
    add_reserved_area(0x00007E00, 0x0009FBFF, "Conventional Memory");
    add_reserved_area(0x0009FC00, 0x0009FFFF, "Extended BIOS Data Area");
    add_reserved_area(0x000A0000, 0x000BFFFF, "Video Memory");
    add_reserved_area(0x000C0000, 0x000C7FFF, "Video BIOS");
    add_reserved_area(0x000C8000, 0x000EFFFF, "BIOS Extensions");
    add_reserved_area(0x000F0000, 0x000FFFFF, "System BIOS");
    
    serial_puts("[MEM] Reserved areas initialized: ");
    serial_puts_num(reserved_areas_count);
    serial_puts(" areas\n");
}

static uint8_t check_area_overlap(uint32_t start, uint32_t end) {
    for (uint32_t i = 0; i < reserved_areas_count; i++) {
        reserved_area_t* area = &reserved_areas[i];
        if (!(end <= area->start || start >= area->end)) {
            return 1;
        }
    }
    return 0;
}

void parse_memory_map(multiboot_info_t* mb_info) {
    if (!mb_info || !(mb_info->flags & (1 << 6))) {
        serial_puts("[MEM] No memory map available\n");
        return;
    }
    
    uint32_t mmap_addr = mb_info->mmap_addr;
    uint32_t mmap_length = mb_info->mmap_length;
    uint32_t original_mmap_end = mmap_addr + mmap_length;
    
    serial_puts("[MEM] Parsing memory map at 0x");
    serial_puts_num_hex(mmap_addr);
    serial_puts(", length: ");
    serial_puts_num(mmap_length);
    serial_puts(" bytes\n");
    
    uint64_t total_memory = 0;
    uint64_t available_memory = 0;
    uint64_t dma_memory = 0;
    uint64_t largest_block = 0;
    mem_regions_count = 0;
    
    while (mmap_addr < original_mmap_end && mem_regions_count < MAX_MEM_REGIONS) {
        memory_map_entry_t* entry = (memory_map_entry_t*)mmap_addr;
        
        uint64_t base = ((uint64_t)entry->base_addr_high << 32) | entry->base_addr_low;
        uint64_t length = ((uint64_t)entry->length_high << 32) | entry->length_low;
        uint32_t type = entry->type;
        
        if (base + length < base || length == 0) {
            mmap_addr += entry->size + sizeof(entry->size);
            continue;
        }
        
        if (length == 0) {
            mmap_addr += entry->size + sizeof(entry->size);
            continue;
        }
        
        mem_region_t* region = &mem_regions_buffer[mem_regions_count];
        mem_regions_count++;
        
        region->base = (uint32_t)base;
        region->size = (uint32_t)length;
        region->type = (uint8_t)type;
        region->used = 0;
        region->dma_capable = (base + length <= DMA_ZONE_LIMIT) ? 1 : 0;
        region->next = NULL;
        
        if (!memory_regions) {
            memory_regions = region;
        } else {
            mem_region_t* last = memory_regions;
            while (last->next) last = last->next;
            last->next = region;
        }
        
        if (type != MEMORY_TYPE_AVAILABLE) {
            char desc[32];
            switch(type) {
                case MEMORY_TYPE_RESERVED:
                    safe_strcpy(desc, "Reserved", sizeof(desc));
                    break;
                case MEMORY_TYPE_ACPI_RECLAIMABLE:
                    safe_strcpy(desc, "ACPI Reclaim", sizeof(desc));
                    break;
                case MEMORY_TYPE_ACPI_NVS:
                    safe_strcpy(desc, "ACPI NVS", sizeof(desc));
                    break;
                case MEMORY_TYPE_BAD:
                    safe_strcpy(desc, "Bad Memory", sizeof(desc));
                    break;
                default:
                    safe_strcpy(desc, "Unknown", sizeof(desc));
            }
            add_reserved_area(region->base, region->base + region->size, desc);
        }
        
        total_memory += length;
        
        if (type == MEMORY_TYPE_AVAILABLE) {
            available_memory += length;
            if (region->dma_capable) {
                dma_memory += length;
            }
            if (length > largest_block) {
                largest_block = length;
            }
        }
        
        mmap_addr += entry->size + sizeof(entry->size);
    }
    
    mem_info.total_memory = (uint32_t)total_memory;
    mem_info.available_memory = (uint32_t)available_memory;
    mem_info.dma_memory = (uint32_t)dma_memory;
    mem_info.largest_block = (uint32_t)largest_block;
    mem_info.region_count = mem_regions_count;
    
    serial_puts("[MEM] Memory map parsed: ");
    serial_puts_num(mem_regions_count);
    serial_puts(" regions, ");
    serial_puts_num(available_memory / (1024 * 1024));
    serial_puts(" MB available, DMA: ");
    serial_puts_num(dma_memory / (1024 * 1024));
    serial_puts(" MB\n");
}

void print_memory_map(void) {
    serial_puts("\n=== DETAILED MEMORY MAP ===\n");
    
    if (!memory_regions) {
        serial_puts("No memory regions found\n");
        return;
    }
    
    mem_region_t* region = memory_regions;
    uint32_t idx = 1;
    uint64_t total_available = 0;
    uint64_t total_reserved = 0;
    
    while (region) {
        uint64_t region_end = (uint64_t)region->base + region->size;
        
        serial_puts("Region ");
        serial_puts_num(idx);
        serial_puts(": 0x");
        serial_puts_num_hex(region->base);
        serial_puts(" - 0x");
        serial_puts_num_hex((uint32_t)region_end);
        serial_puts(" (");
        serial_puts_num(region->size / 1024);
        serial_puts(" KB, ");
        serial_puts_num(region->size / (1024 * 1024));
        serial_puts(" MB) ");
        
        if (region->dma_capable) {
            serial_puts("[DMA] ");
        }
        
        switch(region->type) {
            case MEMORY_TYPE_AVAILABLE:
                serial_puts("Available");
                total_available += region->size;
                break;
            case MEMORY_TYPE_RESERVED:
                serial_puts("Reserved");
                total_reserved += region->size;
                break;
            case MEMORY_TYPE_ACPI_RECLAIMABLE:
                serial_puts("ACPI Reclaim");
                total_reserved += region->size;
                break;
            case MEMORY_TYPE_ACPI_NVS:
                serial_puts("ACPI NVS");
                total_reserved += region->size;
                break;
            case MEMORY_TYPE_BAD:
                serial_puts("Bad Memory");
                total_reserved += region->size;
                break;
            default:
                serial_puts_num(region->type);
                serial_puts(" (Unknown)");
                total_reserved += region->size;
        }
        
        if (region->used) {
            serial_puts(" [IN USE]");
        }
        
        serial_puts("\n");
        region = region->next;
        idx++;
    }
    
    serial_puts("\n=== SUMMARY ===\n");
    serial_puts("Available: ");
    serial_puts_num((uint32_t)(total_available / (1024 * 1024)));
    serial_puts(" MB\n");
    
    serial_puts("DMA-safe:  ");
    serial_puts_num((uint32_t)(mem_info.dma_memory / (1024 * 1024)));
    serial_puts(" MB\n");
    
    serial_puts("Reserved:  ");
    serial_puts_num((uint32_t)(total_reserved / (1024 * 1024)));
    serial_puts(" MB\n");
    
    uint64_t total_memory = total_available + total_reserved;
    serial_puts("Total:     ");
    serial_puts_num((uint32_t)(total_memory / (1024 * 1024)));
    serial_puts(" MB\n");
    serial_puts("===========================\n");
}

static heap_config_t find_best_heap_region(void) {
    heap_config_t config = {0};
    config.min_size = 16 * 1024 * 1024;
    config.max_size = 1024 * 1024 * 1024;
    
    serial_puts("[MEM] Searching for heap region...\n");
    
    typedef struct {
        uint32_t start;
        uint32_t size;
        uint32_t score;
        uint8_t dma_safe;
    } candidate_t;
    
    candidate_t candidates[16];
    uint32_t candidate_count = 0;
    
    mem_region_t* region = memory_regions;
    
    while (region && candidate_count < 16) {
        if (region->type == MEMORY_TYPE_AVAILABLE && !region->used) {
            uint32_t region_start = region->base;
            uint32_t region_size = region->size;
            
            if (region_size < config.min_size) {
                region = region->next;
                continue;
            }
            
            if (region_start + region_size <= 0x100000) {
                region = region->next;
                continue;
            }
            
            uint32_t heap_start = PAGE_ALIGN(region_start);
            
            if (heap_start < 0x1000000) {
                heap_start = 0x1000000;
            }
            
            uint32_t available_size = (region_start + region_size) - heap_start;
            
            if (available_size < config.min_size) {
                region = region->next;
                continue;
            }
            
            uint32_t heap_size = available_size;
            
            if (heap_size > (2UL * 1024 * 1024 * 1024)) {
                heap_size = heap_size * 3 / 4;
            }
            
            heap_size = heap_size & ~(PAGE_SIZE - 1);
            
            if (heap_size >= config.min_size) {
                if (!check_area_overlap(heap_start, heap_start + heap_size)) {
                    uint32_t score = heap_size;
                    
                    if (heap_start > 0x8000000) score += 50 * 1024 * 1024;
                    if (heap_start > 0x40000000) score += 200 * 1024 * 1024;
                    
                    if (heap_size > (256 * 1024 * 1024)) {
                        score += (heap_size / (256 * 1024 * 1024)) * 100 * 1024 * 1024;
                    }
                    
                    candidates[candidate_count].start = heap_start;
                    candidates[candidate_count].size = heap_size;
                    candidates[candidate_count].score = score;
                    candidates[candidate_count].dma_safe = (heap_start + heap_size <= DMA_ZONE_LIMIT) ? 1 : 0;
                    
                    candidate_count++;
                }
            }
        }
        region = region->next;
    }
    
    uint32_t best_index = 0;
    uint32_t best_score = 0;
    
    for (uint32_t i = 0; i < candidate_count; i++) {
        if (candidates[i].dma_safe) {
            candidates[i].score += 500 * 1024 * 1024;
        }
        
        if (candidates[i].score > best_score) {
            best_score = candidates[i].score;
            best_index = i;
        }
    }
    
    if (candidate_count > 0) {
        config.base = candidates[best_index].start;
        config.size = candidates[best_index].size;
        config.valid = 1;
        
        serial_puts("[MEM] Selected: 0x");
        serial_puts_num_hex(config.base);
        serial_puts(" - 0x");
        serial_puts_num_hex(config.base + config.size);
        serial_puts(" (");
        serial_puts_num(config.size / (1024 * 1024));
        serial_puts(" MB) ");
        if (candidates[best_index].dma_safe) {
            serial_puts("[DMA SAFE]");
        }
        serial_puts("\n");
    } else {
        serial_puts("[MEM] No suitable heap candidates found\n");
    }
    
    return config;
}

static void heap_init_region(void* start, size_t size)
{
    heap_start = (mem_block_t*)start;
    heap_start->size = size - sizeof(mem_block_t);
    heap_start->next = NULL;
    heap_start->prev = NULL;
    heap_start->free = 1;
    heap_start->magic = MEM_BLOCK_MAGIC;
    
    heap_end = heap_start;
    heap_total = size;
    heap_initialized = 1;
    
    mem_info.heap_size = size;
    
    add_reserved_area((uint32_t)start, (uint32_t)start + size, "Heap");
}

static int setup_heap_in_region(heap_config_t config) {
    if (!config.valid || config.size < (1 * 1024 * 1024)) {
        serial_puts("[MEM] ERROR: Invalid heap configuration\n");
        return 0;
    }
    
    if ((config.base & (PAGE_SIZE - 1)) != 0) {
        serial_puts("[MEM] ERROR: Heap base not page-aligned\n");
        return 0;
    }
    
    if ((config.size & (PAGE_SIZE - 1)) != 0) {
        serial_puts("[MEM] ERROR: Heap size not page-aligned\n");
        return 0;
    }
    
    if (check_area_overlap(config.base, config.base + config.size)) {
        serial_puts("[MEM] ERROR: Heap overlaps with reserved areas\n");
        return 0;
    }
    
    heap_init_region((void*)config.base, config.size);
    
    mem_region_t* region = memory_regions;
    while (region) {
        if (config.base >= region->base && 
            config.base + config.size <= region->base + region->size) {
            region->used = 1;
            break;
        }
        region = region->next;
    }
    
    serial_puts("[MEM] Heap initialized successfully\n");
    return 1;
}

static int setup_fallback_heap(void) {
    serial_puts("[MEM] Setting up fallback heap...\n");
    
    uint32_t heap_start_addr = PAGE_ALIGN(kernel_info.end + 4 * 1024 * 1024);
    
    uint32_t heap_size = 0;
    
    if (mem_info.dma_memory > (512 * 1024 * 1024)) {
        heap_size = 256 * 1024 * 1024;
    } else if (mem_info.dma_memory > (128 * 1024 * 1024)) {
        heap_size = 64 * 1024 * 1024;
    } else {
        heap_size = mem_info.available_memory / 4;
        heap_size = heap_size & ~(PAGE_SIZE - 1);
        
        if (heap_size < (16 * 1024 * 1024)) {
            heap_size = 16 * 1024 * 1024;
        }
    }
    
    if (heap_size > mem_info.dma_memory / 2) {
        heap_size = mem_info.dma_memory / 2;
        heap_size = heap_size & ~(PAGE_SIZE - 1);
    }
    
    serial_puts("[MEM] Fallback heap size: ");
    serial_puts_num(heap_size / (1024 * 1024));
    serial_puts(" MB\n");
    
    if (check_area_overlap(heap_start_addr, heap_start_addr + heap_size)) {
        serial_puts("[MEM] ERROR: Fallback heap location overlaps reserved areas\n");
        return 0;
    }
    
    heap_config_t config = {
        .base = heap_start_addr,
        .size = heap_size,
        .valid = 1
    };
    
    return setup_heap_in_region(config);
}

static void split_block(mem_block_t* block, uint32_t size)
{
    uint32_t remaining = block->size - size - sizeof(mem_block_t);
    
    // Если остаток слишком мал - не делим, весь блок становится занятым
    if (remaining < sizeof(mem_block_t) + 16) {
        block->free = 0;
        return;
    }
    
    // Создаём новый свободный блок
    mem_block_t* new_block = (mem_block_t*)((uint8_t*)block + sizeof(mem_block_t) + size);
    new_block->size = remaining;
    new_block->free = 1;
    new_block->magic = MEM_BLOCK_MAGIC;
    new_block->next = block->next;
    new_block->prev = block;
    
    // Обновляем текущий блок
    block->size = size;
    block->free = 0;
    block->next = new_block;
    
    // Обновляем связи
    if (new_block->next) {
        new_block->next->prev = new_block;
    }
    
    if (block == heap_end) {
        heap_end = new_block;
    }
}

static void merge_block(mem_block_t* block)
{
    if (!block->free) return;
    
    // Объединяем со следующим
    if (block->next && block->next->free) {
        block->size += sizeof(mem_block_t) + block->next->size;
        block->next = block->next->next;
        
        if (block->next) {
            block->next->prev = block;
        } else {
            heap_end = block;
        }
        // После слияния со следующим, проверяем ещё раз (может быть цепочка)
        merge_block(block);
        return;
    }
    
    // Объединяем с предыдущим
    if (block->prev && block->prev->free) {
        block->prev->size += sizeof(mem_block_t) + block->size;
        block->prev->next = block->next;
        
        if (block->next) {
            block->next->prev = block->prev;
        } else {
            heap_end = block->prev;
        }
    }
}

static mem_block_t* find_free_block(uint32_t size)
{
    mem_block_t* current = heap_start;
    mem_block_t* best = NULL;
    uint32_t best_size = 0xFFFFFFFF;
    
    // Проходим весь список до конца
    while (current) {
        if (current->free && current->size >= size && current->magic == MEM_BLOCK_MAGIC) {
            if (current->size < best_size) {
                best = current;
                best_size = current->size;
                if (best_size == size) break;
            }
        }
        current = current->next;
    }
    
    return best;
}

void memory_init(void) {
    serial_puts("[MEM] Initializing memory system...\n");
    
    #if USE_ADVANCED_ALLOCATOR
    serial_puts("[MEM] Using advanced allocator\n");
    
    detect_kernel_bounds();
    init_reserved_areas();
    
    if (mem_info.region_count == 0) {
        serial_puts("[MEM] WARNING: No memory map information\n");
        if (!setup_fallback_heap()) {
            serial_puts("[MEM] ERROR: Cannot setup fallback heap\n");
            return;
        }
    } else {
        heap_config_t config = find_best_heap_region();
        
        if (config.valid) {
            if (!setup_heap_in_region(config)) {
                serial_puts("[MEM] WARNING: Cannot setup optimal heap\n");
                if (!setup_fallback_heap()) {
                    serial_puts("[MEM] ERROR: Cannot setup fallback heap\n");
                    return;
                }
            }
        } else {
            serial_puts("[MEM] WARNING: No suitable heap region found\n");
            if (!setup_fallback_heap()) {
                serial_puts("[MEM] ERROR: Cannot setup fallback heap\n");
                return;
            }
        }
    }
    
    if (heap_initialized) {
        serial_puts("[MEM] Heap initialization complete\n");
        heap_validate();
    } else {
        serial_puts("[MEM] ERROR: Heap not initialized\n");
    }
    
    #else
    serial_puts("[MEM] Using simple allocator\n");
    serial_puts("[MEM] Heap size: ");
    serial_puts_num(HEAP_SIZE);
    serial_puts(" bytes\n");
    #endif
}

void memory_init_multiboot(multiboot_info_t* mb_info) {
    if (!mb_info) {
        serial_puts("[MEM] No multiboot info\n");
        return;
    }
    
    serial_puts("[MEM] Initializing from Multiboot...\n");
    
    if (mb_info->flags & (1 << 0)) {
        uint32_t mem_lower = mb_info->mem_lower;
        uint32_t mem_upper = mb_info->mem_upper;
        
        uint32_t total_kb = mem_lower + mem_upper + 1024;
        mem_info.total_memory = total_kb * 1024;
        
        serial_puts("[MEM] Total available: ");
        serial_puts_num(total_kb / 1024);
        serial_puts(" MB\n");
    }
    
    #if USE_ADVANCED_ALLOCATOR
    if (mb_info->flags & (1 << 6)) {
        parse_memory_map(mb_info);
    }
    #endif
}

void* kmalloc(uint32_t size) {
    if (size == 0) return NULL;
    
    #if USE_ADVANCED_ALLOCATOR
    if (!heap_initialized) {
        serial_puts("[MEM] ERROR: Heap not initialized\n");
        return NULL;
    }
    
    size = (size + 3) & ~3;
    
    mem_block_t* block = find_free_block(size);
    
    if (!block) {
        serial_puts("[MEM] Out of memory! Requested ");
        serial_puts_num(size);
        serial_puts(" bytes\n");
        memory_stats();
        return NULL;
    }
    
    if (block->size > size + sizeof(mem_block_t) + 16) {
        split_block(block, size);
    } else {
        block->free = 0;
    }
    
    return (void*)((uint8_t*)block + sizeof(mem_block_t));
    
    #else
    uint32_t blocks_needed = (size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    if (blocks_needed == 0) blocks_needed = 1;
    
    for (uint32_t i = 0; i < (HEAP_SIZE / BLOCK_SIZE) - blocks_needed; i++) {
        uint8_t found = 1;
        
        for (uint32_t j = 0; j < blocks_needed; j++) {
            if (heap_used[i + j]) {
                found = 0;
                break;
            }
        }
        
        if (found) {
            for (uint32_t j = 0; j < blocks_needed; j++) {
                heap_used[i + j] = 1;
            }
            return &heap[i * BLOCK_SIZE];
        }
    }
    
    serial_puts("[MEM] ERROR: Out of memory\n");
    return NULL;
    #endif
}

void kfree(void* ptr) {
    if (!ptr) return;
    
    #if USE_ADVANCED_ALLOCATOR
    if (!heap_initialized) {
        serial_puts("[MEM] ERROR: Heap not initialized\n");
        return;
    }
    
    mem_block_t* block = (mem_block_t*)((uint8_t*)ptr - sizeof(mem_block_t));
    
    if (block->magic != MEM_BLOCK_MAGIC) {
        serial_puts("[MEM] ERROR: Invalid free - bad magic\n");
        return;
    }
    
    if (block->free) {
        serial_puts("[MEM] WARNING: Double free detected\n");
        return;
    }
    
    block->free = 1;
    merge_block(block);
    
    #else
    uint32_t offset = (uint32_t)ptr - (uint32_t)heap;
    if (offset >= HEAP_SIZE) {
        serial_puts("[MEM] ERROR: Invalid free\n");
        return;
    }
    
    uint32_t block_index = offset / BLOCK_SIZE;
    heap_used[block_index] = 0;
    #endif
}

void* krealloc(void* ptr, uint32_t size) {
    if (!ptr) return kmalloc(size);
    if (size == 0) {
        kfree(ptr);
        return NULL;
    }
    
    #if USE_ADVANCED_ALLOCATOR
    mem_block_t* block = (mem_block_t*)((uint8_t*)ptr - sizeof(mem_block_t));
    
    if (block->magic != MEM_BLOCK_MAGIC) {
        serial_puts("[MEM] ERROR: Invalid realloc\n");
        return NULL;
    }
    
    uint32_t old_size = block->size;
    uint32_t new_size = (size + 3) & ~3;
    
    if (new_size <= old_size) {
        if (block->size > new_size + sizeof(mem_block_t) + 16) {
            split_block(block, new_size);
        }
        return ptr;
    }
    
    if (block->next && block->next->free) {
        uint32_t combined = block->size + sizeof(mem_block_t) + block->next->size;
        if (combined >= new_size) {
            block->size += sizeof(mem_block_t) + block->next->size;
            block->next = block->next->next;
            
            if (block->next) {
                block->next->prev = block;
            } else {
                heap_end = block;
            }
            
            if (block->size > new_size + sizeof(mem_block_t) + 16) {
                split_block(block, new_size);
            }
            return ptr;
        }
    }
    
    void* new_ptr = kmalloc(size);
    if (!new_ptr) return NULL;
    
    uint32_t copy_size = old_size < size ? old_size : size;
    memcpy(new_ptr, ptr, copy_size);
    kfree(ptr);
    
    return new_ptr;
    
    #else
    serial_puts("[MEM] ERROR: realloc not supported\n");
    return NULL;
    #endif
}

void* kcalloc(uint32_t num, uint32_t size) {
    uint32_t total = num * size;
    void* ptr = kmalloc(total);
    if (ptr) memset(ptr, 0, total);
    return ptr;
}

void* kmalloc_aligned(uint32_t size, uint32_t align) {
    if (align < sizeof(void*)) align = sizeof(void*);
    
    uint32_t total = size + align + sizeof(void*);
    void* raw = kmalloc(total);
    if (!raw) return NULL;
    
    uintptr_t raw_addr = (uintptr_t)raw;
    uintptr_t aligned = (raw_addr + align - 1) & ~(align - 1);
    
    void** header = (void**)(aligned - sizeof(void*));
    *header = raw;
    
    memset((void*)aligned, 0, size);
    
    return (void*)aligned;
}

void kfree_aligned(void* ptr) {
    if (!ptr) return;
    void** header = (void**)((uintptr_t)ptr - sizeof(void*));
    void* raw = *header;
    kfree(raw);
}

void* kmalloc_flags(uint32_t size, uint8_t flags) {
    (void)flags;
    return kmalloc(size);
}

void* kmalloc_dma(uint32_t size) {
    return kmalloc(size);
}

void kfree_dma(void* ptr) {
    kfree(ptr);
}

void* kmalloc_dma_region(uint32_t size, uint32_t* phys_addr) {
    size = PAGE_ALIGN(size);
    void* virt = kmalloc_aligned(size, PAGE_SIZE);
    if (!virt) {
        serial_puts("[MEM] Failed to allocate DMA region\n");
        return NULL;
    }

    memset(virt, 0, size);
    *phys_addr = virt_to_phys(virt);
    if (*phys_addr > DMA_ZONE_LIMIT) {
        serial_puts("[MEM] WARNING: DMA region above 4GB\n");
    }
    
    return virt;
}

void kfree_dma_region(void* virt, uint32_t size) {
    (void)size;
    kfree_aligned(virt);
}

int is_dma_safe_region(void* virt, uint32_t size, uint32_t* phys_addr) {
    if (!virt || size == 0) return 0;
    uint32_t phys = virt_to_phys(virt);
    if (phys_addr) *phys_addr = phys;
    return 1;
}

uint32_t virt_to_phys(void* virt) {
    if (!paging_active || !current_directory) {
        return (uint32_t)virt;
    }
    return paging_get_physical(current_directory, (uint32_t)virt);
}

void* phys_to_virt(uint32_t phys) {
    if (!paging_active || !current_directory) {
        return (void*)phys;
    }
    return (void*)phys;
}

uint32_t get_phys_addr(void* virt) {
    return virt_to_phys(virt);
}

int is_dma_safe(void* ptr) {
    if (!ptr) return 0;
    uint32_t phys = virt_to_phys(ptr);
    return (phys <= DMA_ZONE_LIMIT);
}

memory_info_t get_memory_info(void) {
    #if USE_ADVANCED_ALLOCATOR
    if (heap_initialized) {
        mem_block_t* current = heap_start;
        uint32_t used = 0;
        uint32_t free = 0;
        uint32_t free_blocks = 0;
        
        while (current) {
            if (current->free) {
                free += current->size;
                free_blocks++;
            } else {
                used += current->size;
            }
            current = current->next;
        }
        
        mem_info.heap_used = used;
        mem_info.heap_free = free;
        
        if (free_blocks > 1) {
            mem_info.fragmentation = (free_blocks - 1) * 100 / free_blocks;
        } else {
            mem_info.fragmentation = 0;
        }
    }
    #endif
    
    return mem_info;
}

uint32_t get_total_memory(void) {
    #if USE_ADVANCED_ALLOCATOR
    return mem_info.total_memory;
    #else
    return HEAP_SIZE;
    #endif
}

uint32_t get_free_memory(void) {
    #if USE_ADVANCED_ALLOCATOR
    return mem_info.heap_free;
    #else
    uint32_t free_mem = 0;
    for (uint32_t i = 0; i < HEAP_SIZE / BLOCK_SIZE; i++) {
        if (!heap_used[i]) free_mem += BLOCK_SIZE;
    }
    return free_mem;
    #endif
}

uint32_t get_dma_memory(void) {
    #if USE_ADVANCED_ALLOCATOR
    return mem_info.dma_memory;
    #else
    return HEAP_SIZE;
    #endif
}

void memory_dump(void) {
    serial_puts("\n=== MEMORY INFORMATION ===\n");
    
    #if USE_ADVANCED_ALLOCATOR
    memory_info_t info = get_memory_info();
    
    serial_puts("System Memory:\n");
    serial_puts("  Total:      ");
    serial_puts_num(info.total_memory / (1024*1024));
    serial_puts(" MB\n");
    serial_puts("  Available:  ");
    serial_puts_num(info.available_memory / (1024*1024));
    serial_puts(" MB\n");
    serial_puts("  DMA-safe:   ");
    serial_puts_num(info.dma_memory / (1024*1024));
    serial_puts(" MB\n");
    serial_puts("  Largest:    ");
    serial_puts_num(info.largest_block / (1024*1024));
    serial_puts(" MB\n");
    
    serial_puts("Heap Memory:\n");
    serial_puts("  Size:       ");
    serial_puts_num(info.heap_size / (1024*1024));
    serial_puts(" MB\n");
    serial_puts("  Used:       ");
    serial_puts_num(info.heap_used / (1024*1024));
    serial_puts(" MB");
    if (info.heap_size > 0) {
        uint32_t used_percent = (info.heap_used * 100) / info.heap_size;
        serial_puts(" (");
        serial_puts_num(used_percent);
        serial_puts("%)");
    }
    serial_puts("\n");
    
    serial_puts("  Free:       ");
    serial_puts_num(info.heap_free / (1024*1024));
    serial_puts(" MB");
    if (info.heap_size > 0) {
        uint32_t free_percent = (info.heap_free * 100) / info.heap_size;
        serial_puts(" (");
        serial_puts_num(free_percent);
        serial_puts("%)");
    }
    serial_puts("\n");
    
    serial_puts("  Fragmentation: ");
    serial_puts_num(info.fragmentation);
    serial_puts("%\n");
    
    #else
    serial_puts("Simple Allocator:\n");
    serial_puts("  Heap size: ");
    serial_puts_num(HEAP_SIZE);
    serial_puts(" bytes\n");
    
    uint32_t used_blocks = 0;
    for (uint32_t i = 0; i < HEAP_SIZE / BLOCK_SIZE; i++) {
        if (heap_used[i]) used_blocks++;
    }
    
    serial_puts("  Used:      ");
    serial_puts_num(used_blocks * BLOCK_SIZE);
    serial_puts(" bytes (");
    if (HEAP_SIZE > 0) {
        serial_puts_num((used_blocks * BLOCK_SIZE * 100) / HEAP_SIZE);
    } else {
        serial_puts("0");
    }
    serial_puts("%)\n");
    
    uint32_t free_blocks = (HEAP_SIZE / BLOCK_SIZE) - used_blocks;
    serial_puts("  Free:      ");
    serial_puts_num(free_blocks * BLOCK_SIZE);
    serial_puts(" bytes (");
    if (HEAP_SIZE > 0) {
        serial_puts_num((free_blocks * BLOCK_SIZE * 100) / HEAP_SIZE);
    } else {
        serial_puts("0");
    }
    serial_puts("%)\n");
    #endif
    
    serial_puts("===========================\n");
}

void memory_stats(void) {
    #if USE_ADVANCED_ALLOCATOR
    if (!heap_initialized) {
        serial_puts("[MEM] Heap not initialized\n");
        return;
    }
    
    memory_info_t info = get_memory_info();
    
    serial_puts("[MEM] Statistics:\n");
    serial_puts("  Heap size: ");
    serial_puts_num(info.heap_size / 1024);
    serial_puts(" KB\n");
    
    serial_puts("  Used:      ");
    serial_puts_num(info.heap_used / 1024);
    serial_puts(" KB");
    if (info.heap_size > 0) {
        uint32_t used_percent = (info.heap_used * 100) / info.heap_size;
        serial_puts(" (");
        serial_puts_num(used_percent);
        serial_puts("%)");
    }
    serial_puts("\n");
    
    serial_puts("  Free:      ");
    serial_puts_num(info.heap_free / 1024);
    serial_puts(" KB");
    if (info.heap_size > 0) {
        uint32_t free_percent = (info.heap_free * 100) / info.heap_size;
        serial_puts(" (");
        serial_puts_num(free_percent);
        serial_puts("%)");
    }
    serial_puts("\n");
    
    mem_block_t* current = heap_start;
    uint32_t total_blocks = 0;
    uint32_t free_blocks = 0;
    uint32_t used_blocks = 0;
    
    while (current) {
        total_blocks++;
        if (current->free) {
            free_blocks++;
        } else {
            used_blocks++;
        }
        current = current->next;
    }
    
    serial_puts("  Blocks:    ");
    serial_puts_num(total_blocks);
    serial_puts(" (");
    serial_puts_num(used_blocks);
    serial_puts(" used, ");
    serial_puts_num(free_blocks);
    serial_puts(" free)\n");
    
    serial_puts("  Fragmentation: ");
    serial_puts_num(info.fragmentation);
    serial_puts("%\n");
    
    #else
    serial_puts("[MEM] Simple allocator stats not available\n");
    #endif
}

void heap_validate(void) {
    #if USE_ADVANCED_ALLOCATOR
    if (!heap_initialized) {
        serial_puts("[MEM] ERROR: Heap not initialized\n");
        return;
    }
    
    serial_puts("[MEM] Heap validation: ");
    
    mem_block_t* current = heap_start;
    uint32_t errors = 0;
    uint32_t total_size = 0;
    uint32_t block_count = 0;
    
    while (current) {
        block_count++;
        total_size += current->size;
        
        if (current->magic != MEM_BLOCK_MAGIC) {
            serial_puts("\n  ERROR: Bad magic at block ");
            serial_puts_num(block_count - 1);
            errors++;
        }
        
        if (current->size < sizeof(mem_block_t)) {
            serial_puts("\n  ERROR: Block too small at ");
            serial_puts_num_hex((uint32_t)current);
            errors++;
        }
        
        if (current->next && current->next->prev != current) {
            serial_puts("\n  WARNING: Broken prev link at ");
            serial_puts_num_hex((uint32_t)current->next);
            errors++;
        }
        
        current = current->next;
    }
    
    total_size += block_count * sizeof(mem_block_t);
    
    if (total_size != heap_total) {
        serial_puts("\n  ERROR: Size mismatch");
        errors++;
    }
    
    if (errors == 0) {
        serial_puts("PASS (");
        serial_puts_num(block_count);
        serial_puts(" blocks, ");
        serial_puts_num(heap_total / 1024);
        serial_puts(" KB)\n");
    } else {
        serial_puts("\n  FAILED: ");
        serial_puts_num(errors);
        serial_puts(" errors\n");
    }
    #else
    serial_puts("[MEM] Validation not supported for simple allocator\n");
    #endif
}

void debug_heap_layout(void) {
    #if USE_ADVANCED_ALLOCATOR
    if (!heap_initialized) {
        serial_puts("[MEM] Heap not initialized\n");
        return;
    }
    
    serial_puts("\n=== HEAP LAYOUT ===\n");
    
    mem_block_t* current = heap_start;
    uint32_t index = 0;
    uint32_t total_used = 0;
    uint32_t total_free = 0;
    
    while (current) {
        serial_puts("Block ");
        serial_puts_num(index);
        serial_puts(": 0x");
        serial_puts_num_hex((uint32_t)current);
        serial_puts(" - 0x");
        serial_puts_num_hex((uint32_t)current + sizeof(mem_block_t) + current->size);
        serial_puts(" (");
        serial_puts_num(current->size);
        serial_puts(" bytes) ");
        
        if (current->free) {
            serial_puts("[FREE]");
            total_free += current->size;
        } else {
            serial_puts("[USED]");
            total_used += current->size;
        }
        
        serial_puts("\n");
        
        current = current->next;
        index++;
    }
    
    serial_puts("\nSummary:\n");
    serial_puts("  Total blocks: ");
    serial_puts_num(index);
    serial_puts("\n");
    serial_puts("  Total heap:   ");
    serial_puts_num(heap_total);
    serial_puts(" bytes\n");
    serial_puts("  Used:         ");
    serial_puts_num(total_used);
    serial_puts(" bytes (");
    if (heap_total > 0) {
        serial_puts_num((total_used * 100) / heap_total);
    } else {
        serial_puts("0");
    }
    serial_puts("%)\n");
    serial_puts("  Free:         ");
    serial_puts_num(total_free);
    serial_puts(" bytes (");
    if (heap_total > 0) {
        serial_puts_num((total_free * 100) / heap_total);
    } else {
        serial_puts("0");
    }
    serial_puts("%)\n");
    serial_puts("===================\n");
    #endif
}

void memory_paging_activated(void) {
    paging_active = 1;
    serial_puts("[MEM] Paging activated, address translation enabled\n");
}

void* malloc(uint32_t size) {
    return kmalloc(size);
}

void free(void* ptr) {
    kfree(ptr);
}