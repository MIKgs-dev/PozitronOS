#include "kernel/memory.h"
#include "drivers/serial.h"
#include "drivers/vga.h"
#include <stddef.h>
#include "lib/string.h"

// === Глобальные переменные ===

#if USE_ADVANCED_ALLOCATOR
// Для нового аллокатора
static mem_block_t* heap_start = NULL;
static mem_block_t* heap_end = NULL;
static uint32_t heap_total_size = 0;
static uint32_t heap_initialized = 0;

// ММАП информация
static mem_region_t* memory_regions = NULL;
static mem_region_t* heap_region = NULL;
static memory_info_t mem_info = {0};

// Информация о ядре
static kernel_info_t kernel_info = {0};

// Зарезервированные области (используем статический массив для простоты)
#define MAX_RESERVED_AREAS 32
static reserved_area_t reserved_areas[MAX_RESERVED_AREAS];
static uint32_t reserved_areas_count = 0;

// Временная память для структур регионов (используем статический массив)
#define MAX_MEM_REGIONS 64
static mem_region_t mem_regions_buffer[MAX_MEM_REGIONS];
static uint32_t mem_regions_count = 0;

#else
// Для старого аллокатора
static uint8_t heap[HEAP_SIZE];
static uint8_t heap_used[HEAP_SIZE / BLOCK_SIZE] = {0};
#endif

// === Вспомогательные функции для нового аллокатора ===
#if USE_ADVANCED_ALLOCATOR

// Безопасное копирование строк
static void safe_strcpy(char* dest, const char* src, size_t max_len) {
    size_t i;
    for (i = 0; i < max_len - 1 && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    dest[i] = '\0';
}

// Определение границ ядра
static void detect_kernel_bounds(void) {
    extern uint8_t _start;  // Начало ядра из boot.asm
    extern uint8_t end;     // Конец ядра
    
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

// Добавление зарезервированной области
static void add_reserved_area(uint32_t start, uint32_t end, const char* description) {
    if (reserved_areas_count >= MAX_RESERVED_AREAS) {
        serial_puts("[MEM] WARNING: Too many reserved areas\n");
        return;
    }
    
    reserved_area_t* area = &reserved_areas[reserved_areas_count++];
    area->start = start;
    area->end = end;
    area->description = description;
    
    // Простой вывод для отладки
    serial_puts("[MEM] Reserved: ");
    if (description) {
        const char* p = description;
        while (*p && *p >= 32 && *p <= 126) {
            serial_write(*p);
            p++;
        }
    }
    serial_puts(" (0x");
    serial_puts_num_hex(start);
    serial_puts(" - 0x");
    serial_puts_num_hex(end);
    serial_puts(")\n");
}

// Инициализация известных зарезервированных областей
static void init_reserved_areas(void) {
    // Добавляем само ядро
    add_reserved_area(kernel_info.start, kernel_info.end, "Kernel");
    
    // Известные системные области (стандартные для PC)
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

// Проверка пересечения с зарезервированными областями
static uint8_t check_area_overlap(uint32_t start, uint32_t end) {
    for (uint32_t i = 0; i < reserved_areas_count; i++) {
        reserved_area_t* area = &reserved_areas[i];
        
        // Проверяем пересечение: если не (end <= area->start || start >= area->end)
        if (!(end <= area->start || start >= area->end)) {
            serial_puts("[MEM] Overlap detected with ");
            if (area->description) {
                const char* p = area->description;
                while (*p && *p >= 32 && *p <= 126) {
                    serial_write(*p);
                    p++;
                }
            }
            serial_puts(" (0x");
            serial_puts_num_hex(area->start);
            serial_puts(" - 0x");
            serial_puts_num_hex(area->end);
            serial_puts(")\n");
            return 1; // Есть пересечение
        }
    }
    
    return 0; // Нет пересечений
}

// Парсинг карты памяти
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
    uint64_t largest_block = 0;
    mem_regions_count = 0;
    
    // Парсим каждую запись
    while (mmap_addr < original_mmap_end && mem_regions_count < MAX_MEM_REGIONS) {
        memory_map_entry_t* entry = (memory_map_entry_t*)mmap_addr;
        
        uint64_t base = ((uint64_t)entry->base_addr_high << 32) | entry->base_addr_low;
        uint64_t length = ((uint64_t)entry->length_high << 32) | entry->length_low;
        uint32_t type = entry->type;
        
        // Проверяем на переполнение
        if (base + length < base || length == 0) {
            mmap_addr += entry->size + sizeof(entry->size);
            continue;  // Пропускаем, не добавляем!
        }
        
        // Проверяем нулевую длину
        if (length == 0) {
            serial_puts("  Region ");
            serial_puts_num(mem_regions_count + 1);
            serial_puts(": INVALID - ZERO LENGTH\n");
            mmap_addr += entry->size + sizeof(entry->size);
            continue;
        }
        
        // Сохраняем регион в буфер
        mem_region_t* region = &mem_regions_buffer[mem_regions_count];
        mem_regions_count++;
        
        region->base = (uint32_t)base;
        region->size = (uint32_t)length;
        region->type = (uint8_t)type;
        region->used = 0;
        region->next = NULL;
        
        // Добавляем в связанный список
        if (!memory_regions) {
            memory_regions = region;
        } else {
            mem_region_t* last = memory_regions;
            while (last->next) last = last->next;
            last->next = region;
        }
        
        // Добавляем зарезервированные области в список
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
            if (length > largest_block) {
                largest_block = length;
            }
        }
        
        // Вывод информации о регионе
        serial_puts("  Region ");
        serial_puts_num(mem_regions_count);
        serial_puts(": 0x");
        serial_puts_num_hex((uint32_t)base);
        serial_puts(" - 0x");
        serial_puts_num_hex((uint32_t)(base + length));
        serial_puts(" (");
        serial_puts_num(length / 1024);
        serial_puts(" KB) Type=");
        serial_puts_num(type);
        
        if (type == MEMORY_TYPE_AVAILABLE) {
            serial_puts(" (Available)");
        } else {
            serial_puts(" (Reserved)");
        }
        serial_puts("\n");
        
        // Переход к следующей записи
        mmap_addr += entry->size + sizeof(entry->size);
    }
    
    // Сохраняем информацию
    mem_info.total_memory = (uint32_t)total_memory;
    mem_info.available_memory = (uint32_t)available_memory;
    mem_info.largest_block = (uint32_t)largest_block;
    mem_info.region_count = mem_regions_count;
    
    serial_puts("[MEM] Memory map parsed: ");
    serial_puts_num(mem_regions_count);
    serial_puts(" regions, ");
    serial_puts_num(available_memory / (1024 * 1024));
    serial_puts(" MB available, largest block: ");
    serial_puts_num(largest_block / (1024 * 1024));
    serial_puts(" MB\n");
}

// Вывод карты памяти
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
        serial_puts(" MB) Type=");
        
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
    
    serial_puts("Reserved:  ");
    serial_puts_num((uint32_t)(total_reserved / (1024 * 1024)));
    serial_puts(" MB\n");
    
    uint64_t total_memory = total_available + total_reserved;
    serial_puts("Total:     ");
    serial_puts_num((uint32_t)(total_memory / (1024 * 1024)));
    serial_puts(" MB\n");
    
    serial_puts("\n=== RESERVED AREAS ===\n");
    for (uint32_t i = 0; i < reserved_areas_count; i++) {
        serial_puts("0x");
        serial_puts_num_hex(reserved_areas[i].start);
        serial_puts(" - 0x");
        serial_puts_num_hex(reserved_areas[i].end);
        serial_puts(": ");
        
        // Безопасный вывод описания
        const char* desc = reserved_areas[i].description;
        if (desc) {
            while (*desc && *desc >= 32 && *desc <= 126) {
                serial_write(*desc);
                desc++;
            }
        }
        serial_puts("\n");
    }
    
    serial_puts("===========================\n");
}

// Поиск лучшего региона для кучи
static heap_config_t find_best_heap_region(void) {
    heap_config_t config = {0};
    config.min_size = 16 * 1024 * 1024;   // Минимум 16MB
    config.max_size = 1024 * 1024 * 1024; // Максимум 1GB для 32-бит системы
    
    serial_puts("[MEM] Searching for heap region...\n");
    
    // Собираем информацию о всех подходящих регионах
    typedef struct {
        uint32_t start;
        uint32_t size;
        uint32_t score;
        uint8_t is_high_mem; // 1 = выше 4GB (для PAE/64-bit)
    } candidate_t;
    
    candidate_t candidates[16];
    uint32_t candidate_count = 0;
    
    mem_region_t* region = memory_regions;
    
    while (region && candidate_count < 16) {
        if (region->type == MEMORY_TYPE_AVAILABLE && !region->used) {
            uint32_t region_start = region->base;
            uint32_t region_size = region->size;
            
            // Пропускаем слишком маленькие регионы
            if (region_size < config.min_size) {
                region = region->next;
                continue;
            }
            
            // Пропускаем регионы ниже 1MB (там загрузчик и т.д.)
            if (region_start + region_size <= 0x100000) {
                region = region->next;
                continue;
            }
            
            // Вычисляем потенциальный старт кучи
            uint32_t heap_start = PAGE_ALIGN(region_start);
            
            // Если регион начинается до 16MB, начинаем с 16MB
            if (heap_start < 0x1000000) {
                heap_start = 0x1000000;
            }
            
            // Вычисляем доступный размер
            uint32_t available_size = (region_start + region_size) - heap_start;
            
            // Пропускаем если после выравнивания слишком мало
            if (available_size < config.min_size) {
                region = region->next;
                continue;
            }
            
            // Берем разумный размер (не весь регион, оставляем запас)
            uint32_t heap_size = available_size;
            
            // Для очень больших регионов (>2GB) берем 75%
            if (heap_size > (2UL * 1024 * 1024 * 1024)) {
                heap_size = heap_size * 3 / 4;
            }
            
            // Выравниваем
            heap_size = heap_size & ~(PAGE_SIZE - 1);
            
            // Проверяем минимальный размер
            if (heap_size >= config.min_size) {
                // Проверяем пересечения
                if (!check_area_overlap(heap_start, heap_start + heap_size)) {
                    // Вычисляем score
                    uint32_t score = heap_size;
                    
                    // Бонусы:
                    // 1. Регионы выше 128MB получают бонус
                    if (heap_start > 0x8000000) score += 50 * 1024 * 1024;
                    
                    // 2. Регионы выше 1GB получают большой бонус
                    if (heap_start > 0x40000000) score += 200 * 1024 * 1024;
                    
                    // 3. Большие регионы получают дополнительный бонус
                    if (heap_size > (256 * 1024 * 1024)) {
                        score += (heap_size / (256 * 1024 * 1024)) * 100 * 1024 * 1024;
                    }
                    
                    // Сохраняем кандидата
                    candidates[candidate_count].start = heap_start;
                    candidates[candidate_count].size = heap_size;
                    candidates[candidate_count].score = score;
                    candidates[candidate_count].is_high_mem = (heap_start > 0xFFFFFFFF) ? 1 : 0;
                    
                    serial_puts("[MEM] Candidate ");
                    serial_puts_num(candidate_count);
                    serial_puts(": 0x");
                    serial_puts_num_hex(heap_start);
                    serial_puts(" - 0x");
                    serial_puts_num_hex(heap_start + heap_size);
                    serial_puts(" (");
                    serial_puts_num(heap_size / (1024 * 1024));
                    serial_puts(" MB), Score=");
                    serial_puts_num(score);
                    serial_puts("\n");
                    
                    candidate_count++;
                }
            }
        }
        region = region->next;
    }
    
    // Выбираем лучшего кандидата
    uint32_t best_index = 0;
    uint32_t best_score = 0;
    
    for (uint32_t i = 0; i < candidate_count; i++) {
        // Предпочитаем низкую память для 32-бит систем
        if (!candidates[i].is_high_mem) {
            candidates[i].score += 300 * 1024 * 1024;
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
        serial_puts(" MB)\n");
    } else {
        serial_puts("[MEM] No suitable heap candidates found\n");
    }
    
    return config;
}

// Настройка кучи в выбранном регионе
static int setup_heap_in_region(heap_config_t config) {
    if (!config.valid || config.size < (1 * 1024 * 1024)) {
        serial_puts("[MEM] ERROR: Invalid heap configuration\n");
        return 0;
    }
    
    // Проверяем выравнивание
    if ((config.base & (PAGE_SIZE - 1)) != 0) {
        serial_puts("[MEM] ERROR: Heap base not page-aligned\n");
        return 0;
    }
    
    if ((config.size & (PAGE_SIZE - 1)) != 0) {
        serial_puts("[MEM] ERROR: Heap size not page-aligned\n");
        return 0;
    }
    
    // Проверяем пересечения
    if (check_area_overlap(config.base, config.base + config.size)) {
        serial_puts("[MEM] ERROR: Heap overlaps with reserved areas\n");
        return 0;
    }
    
    // Проверяем, что куча не слишком близко к ядру
    if (config.base < kernel_info.end + (2 * 1024 * 1024)) {
        serial_puts("[MEM] WARNING: Heap is close to kernel\n");
    }
    
    // Инициализируем кучу
    heap_start = (mem_block_t*)config.base;
    heap_end = (mem_block_t*)(config.base + config.size);
    
    heap_start->magic = MEM_BLOCK_MAGIC;
    heap_start->size = config.size;
    heap_start->free = 1;
    heap_start->next = NULL;
    heap_start->prev = NULL;
    
    heap_total_size = config.size;
    heap_initialized = 1;
    mem_info.heap_size = config.size;
    
    // Помечаем регион как использованный
    mem_region_t* region = memory_regions;
    while (region) {
        if (config.base >= region->base && 
            config.base + config.size <= region->base + region->size) {
            region->used = 1;
            heap_region = region;
            break;
        }
        region = region->next;
    }
    
    // Добавляем кучу в зарезервированные области
    add_reserved_area(config.base, config.base + config.size, "Heap");
    
    serial_puts("[MEM] Heap initialized successfully\n");
    return 1;
}

// Fallback: куча после ядра
static int setup_fallback_heap(void) {
    serial_puts("[MEM] Setting up fallback heap...\n");
    
    // Старт после ядра + 4MB запас
    uint32_t heap_start_addr = PAGE_ALIGN(kernel_info.end + 4 * 1024 * 1024);
    
    // Определяем разумный размер на основе доступной памяти
    uint32_t heap_size = 0;
    
    if (mem_info.available_memory > (512 * 1024 * 1024)) {
        // Если больше 512MB доступно, берем 256MB
        heap_size = 256 * 1024 * 1024;
    } else if (mem_info.available_memory > (128 * 1024 * 1024)) {
        // Если больше 128MB, берем 64MB
        heap_size = 64 * 1024 * 1024;
    } else {
        // Иначе берем 25% от доступной, но не менее 16MB
        heap_size = mem_info.available_memory / 4;
        heap_size = heap_size & ~(PAGE_SIZE - 1);
        
        if (heap_size < (16 * 1024 * 1024)) {
            heap_size = 16 * 1024 * 1024;
        }
    }
    
    // Проверяем, что не превышаем доступную память
    if (heap_size > mem_info.available_memory / 2) {
        heap_size = mem_info.available_memory / 2;
        heap_size = heap_size & ~(PAGE_SIZE - 1);
    }
    
    serial_puts("[MEM] Fallback heap size: ");
    serial_puts_num(heap_size / (1024 * 1024));
    serial_puts(" MB\n");
    
    // Проверяем, что выбранный регион не пересекается с зарезервированными областями
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

// Поиск свободного блока
static mem_block_t* find_free_block(uint32_t size) {
    mem_block_t* current = heap_start;
    
    while (current) {
        if (current->free && current->size >= size) {
            return current;
        }
        current = current->next;
    }
    
    return NULL;
}

// Разделение блока
static void split_block(mem_block_t* block, uint32_t size) {
    if (!block) return;
    
    // Минимальный размер для нового блока
    uint32_t min_new_block_size = sizeof(mem_block_t) + MEM_ALIGNMENT;
    
    if (block->size < size + min_new_block_size) {
        // Не хватает места для нового блока
        block->free = 0;  // Используем весь блок
        return;
    }
    
    // Создаем новый свободный блок
    mem_block_t* new_block = (mem_block_t*)((uint8_t*)block + size);
    
    new_block->magic = MEM_BLOCK_MAGIC;
    new_block->size = block->size - size;
    new_block->free = 1;
    new_block->next = block->next;
    new_block->prev = block;
    
    // Обновляем связи
    block->size = size;
    block->free = 0;  // Исходный блок теперь занят
    block->next = new_block;
    
    if (new_block->next) {
        new_block->next->prev = new_block;
    }
    
    // Если этот блок был в конце
    if (block == heap_end) {
        heap_end = new_block;
    }
    
    #ifdef DEBUG_MEMORY
    serial_puts("[MEM] Split block: old=");
    serial_puts_num_hex((uint32_t)block);
    serial_puts(" (");
    serial_puts_num(size);
    serial_puts("), new=");
    serial_puts_num_hex((uint32_t)new_block);
    serial_puts(" (");
    serial_puts_num(new_block->size);
    serial_puts(")\n");
    #endif
}

// Слияние блоков
static void merge_blocks(mem_block_t* block) {
    if (!block || !block->free) return;
    
    // Слияние с правым блоком
    if (block->next && block->next->free) {
        block->size += block->next->size;
        block->next = block->next->next;
        
        if (block->next) {
            block->next->prev = block;
        }
        
        if (!block->next) {
            heap_end = block;
        }
    }
    
    // Слияние с левым блоком
    if (block->prev && block->prev->free) {
        block->prev->size += block->size;
        block->prev->next = block->next;
        
        if (block->next) {
            block->next->prev = block->prev;
        }
        
        if (!block->prev->next) {
            heap_end = block->prev;
        }
        
        block = block->prev;
    }
}

// Получение информации о памяти
memory_info_t get_memory_info(void) {
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
        
        // Вычисляем фрагментацию
        if (free_blocks > 1) {
            mem_info.fragmentation = (free_blocks - 1) * 100 / free_blocks;
        } else {
            mem_info.fragmentation = 0;
        }
    }
    
    return mem_info;
}

#endif // USE_ADVANCED_ALLOCATOR

// === Общедоступные функции ===

// Инициализация памяти
void memory_init(void) {
    serial_puts("[MEM] Initializing memory system...\n");
    
    #if USE_ADVANCED_ALLOCATOR
    serial_puts("[MEM] Using advanced allocator\n");
    
    // Определяем границы ядра
    detect_kernel_bounds();
    
    // Инициализируем зарезервированные области
    init_reserved_areas();
    
    // Проверяем информацию о памяти
    if (mem_info.region_count == 0) {
        serial_puts("[MEM] WARNING: No memory map information\n");
        if (!setup_fallback_heap()) {
            serial_puts("[MEM] ERROR: Cannot setup fallback heap\n");
            return;
        }
    } else {
        // Ищем лучший регион для кучи
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

// Инициализация из Multiboot
void memory_init_multiboot(multiboot_info_t* mb_info) {
    if (!mb_info) {
        serial_puts("[MEM] No multiboot info\n");
        return;
    }
    
    serial_puts("[MEM] Initializing from Multiboot...\n");
    
    if (mb_info->flags & (1 << 0)) {
        uint32_t mem_lower = mb_info->mem_lower;
        uint32_t mem_upper = mb_info->mem_upper;
        
        serial_puts("[MEM] Lower memory: ");
        serial_puts_num(mem_lower);
        serial_puts(" KB\n");
        
        serial_puts("[MEM] Upper memory: ");
        serial_puts_num(mem_upper);
        serial_puts(" KB\n");
        
        uint32_t total_kb = mem_lower + mem_upper + 1024;
        mem_info.total_memory = total_kb * 1024;
        
        serial_puts("[MEM] Total available: ");
        serial_puts_num(total_kb / 1024);
        serial_puts(" MB\n");
    } else {
        serial_puts("[MEM] No basic memory info from Multiboot\n");
    }
    
    #if USE_ADVANCED_ALLOCATOR
    if (mb_info->flags & (1 << 6)) {
        parse_memory_map(mb_info);
    }
    #endif
}

// Выделение памяти
void* kmalloc(uint32_t size) {
    if (size == 0) return NULL;
    
    #if USE_ADVANCED_ALLOCATOR
    if (!heap_initialized) {
        serial_puts("[MEM] ERROR: Heap not initialized\n");
        return NULL;
    }
    
    uint32_t total_size = ALIGN(size + sizeof(mem_block_t));
    mem_block_t* block = find_free_block(total_size);
    
    if (!block) {
        serial_puts("[MEM] ERROR: Out of memory! Requested ");
        serial_puts_num(size);
        serial_puts(" bytes\n");
        
        // Детальная диагностика
        serial_puts("[MEM] Debug: total_size=");
        serial_puts_num(total_size);
        serial_puts(", heap_total=");
        serial_puts_num(heap_total_size);
        serial_puts(", largest_free=");
        
        // Находим самый большой свободный блок
        uint32_t largest_free = 0;
        mem_block_t* curr = heap_start;
        while (curr) {
            if (curr->free && curr->size > largest_free) {
                largest_free = curr->size;
            }
            curr = curr->next;
        }
        serial_puts_num(largest_free);
        serial_puts("\n");
        
        memory_stats();
        return NULL;
    }
    
    // ВАЖНОЕ ИСПРАВЛЕНИЕ: Принудительно делим большие блоки
    // Даже если не хватает места для идеального разделения,
    // нужно оставить хоть что-то свободное
    
    if (block->size >= total_size + sizeof(mem_block_t)) {
        // Есть место для разделения
        split_block(block, total_size);
    } else if (block->size > total_size * 2) {
        // Блок очень большой - делим пополам
        uint32_t half_size = block->size / 2;
        half_size = ALIGN(half_size);
        split_block(block, half_size);
        block->free = 0;
    } else {
        // Блок не намного больше запроса - используем целиком
        block->free = 0;
    }
    
    void* ptr = (void*)((uint8_t*)block + sizeof(mem_block_t));
    
    #ifdef DEBUG_MEMORY
    serial_puts("[MEM] Allocated ");
    serial_puts_num(size);
    serial_puts(" bytes at 0x");
    serial_puts_num_hex((uint32_t)ptr);
    serial_puts(", block size=");
    serial_puts_num(block->size);
    serial_puts("\n");
    #endif
    
    return ptr;
    
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
            
            void* ptr = &heap[i * BLOCK_SIZE];
            
            #ifdef DEBUG_MEMORY
            serial_puts("[MEM] Allocated ");
            serial_puts_num(size);
            serial_puts(" bytes at 0x");
            serial_puts_num_hex((uint32_t)ptr);
            serial_puts("\n");
            #endif
            
            return ptr;
        }
    }
    
    serial_puts("[MEM] ERROR: Out of memory\n");
    return NULL;
    #endif
}

// Освобождение памяти
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
    
    block->free = 1;
    merge_blocks(block);
    
    #ifdef DEBUG_MEMORY
    serial_puts("[MEM] Freed memory at 0x");
    serial_puts_num_hex((uint32_t)ptr);
    serial_puts("\n");
    #endif
    
    #else
    uint32_t offset = (uint32_t)ptr - (uint32_t)heap;
    if (offset >= HEAP_SIZE) {
        serial_puts("[MEM] ERROR: Invalid free\n");
        return;
    }
    
    uint32_t block_index = offset / BLOCK_SIZE;
    heap_used[block_index] = 0;
    
    #ifdef DEBUG_MEMORY
    serial_puts("[MEM] Freed block ");
    serial_puts_num(block_index);
    serial_puts("\n");
    #endif
    #endif
}

// Перераспределение памяти
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
    
    uint32_t old_size = block->size - sizeof(mem_block_t);
    
    if (size <= old_size) {
        uint32_t new_total_size = ALIGN(size + sizeof(mem_block_t));
        if (block->size >= new_total_size + sizeof(mem_block_t) + MEM_ALIGNMENT) {
            split_block(block, new_total_size);
        }
        return ptr;
    }
    
    // Пробуем расширить
    if (block->next && block->next->free && 
        (block->size + block->next->size) >= ALIGN(size + sizeof(mem_block_t))) {
        
        block->size += block->next->size;
        block->next = block->next->next;
        
        if (block->next) {
            block->next->prev = block;
        }
        
        uint32_t new_total_size = ALIGN(size + sizeof(mem_block_t));
        if (block->size >= new_total_size + sizeof(mem_block_t) + MEM_ALIGNMENT) {
            split_block(block, new_total_size);
        }
        
        return ptr;
    }
    
    // Выделяем новый блок
    void* new_ptr = kmalloc(size);
    if (!new_ptr) return NULL;
    
    uint32_t copy_size = (old_size < size) ? old_size : size;
    uint8_t* src = (uint8_t*)ptr;
    uint8_t* dst = (uint8_t*)new_ptr;
    
    for (uint32_t i = 0; i < copy_size; i++) {
        dst[i] = src[i];
    }
    
    kfree(ptr);
    return new_ptr;
    
    #else
    serial_puts("[MEM] ERROR: realloc not supported\n");
    return NULL;
    #endif
}

// Выделение с обнулением
void* kcalloc(uint32_t num, uint32_t size) {
    uint32_t total = num * size;
    void* ptr = kmalloc(total);
    
    if (ptr) {
        uint8_t* p = (uint8_t*)ptr;
        for (uint32_t i = 0; i < total; i++) {
            p[i] = 0;
        }
    }
    
    return ptr;
}

// Общий объем памяти
uint32_t get_total_memory(void) {
    #if USE_ADVANCED_ALLOCATOR
    return mem_info.total_memory;
    #else
    return HEAP_SIZE;
    #endif
}

// Свободная память
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

// Дамп информации
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
    
    // ИСПРАВЛЕННЫЙ РАСЧЕТ ПРОЦЕНТОВ:
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

// Статистика памяти
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
    
    // Подсчет блоков
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

// Проверка целостности
void heap_validate(void) {
    #if USE_ADVANCED_ALLOCATOR
    if (!heap_initialized) {
        serial_puts("[MEM] ERROR: Heap not initialized\n");
        return;
    }
    
    serial_puts("[MEM] Heap validation: ");
    
    mem_block_t* current = heap_start;
    uint32_t errors = 0;
    uint32_t warnings = 0;
    uint32_t total_size = 0;
    uint32_t block_count = 0;
    
    while (current) {
        block_count++;
        total_size += current->size;
        
        // Проверка 1: Магическое число
        if (current->magic != MEM_BLOCK_MAGIC) {
            serial_puts("\n  ERROR: Bad magic at block ");
            serial_puts_num(block_count - 1);
            errors++;
        }
        
        // Проверка 2: Минимальный размер
        if (current->size < sizeof(mem_block_t)) {
            serial_puts("\n  ERROR: Block too small (");
            serial_puts_num(current->size);
            serial_puts(" bytes) at ");
            serial_puts_num_hex((uint32_t)current);
            errors++;
        }
        
        // Проверка 3: Корректность связей
        if (current->next) {
            if ((uint8_t*)current->next < (uint8_t*)current) {
                serial_puts("\n  ERROR: Next pointer goes backward at ");
                serial_puts_num_hex((uint32_t)current);
                errors++;
            }
            
            if (current->next->prev != current) {
                serial_puts("\n  WARNING: Broken prev link at ");
                serial_puts_num_hex((uint32_t)current->next);
                warnings++;
            }
        }
        
        // Проверка 4: Free блоки не должны быть слишком маленькими
        if (current->free && current->size < (sizeof(mem_block_t) * 2)) {
            serial_puts("\n  WARNING: Small free block (");
            serial_puts_num(current->size);
            serial_puts(" bytes) at ");
            serial_puts_num_hex((uint32_t)current);
            warnings++;
        }
        
        current = current->next;
    }
    
    // Проверка общего размера
    if (total_size != heap_total_size) {
        serial_puts("\n  ERROR: Size mismatch: expected ");
        serial_puts_num(heap_total_size);
        serial_puts(", calculated ");
        serial_puts_num(total_size);
        errors++;
    }
    
    if (errors == 0 && warnings == 0) {
        serial_puts("PASS (");
        serial_puts_num(block_count);
        serial_puts(" blocks, ");
        serial_puts_num(total_size / 1024);
        serial_puts(" KB)\n");
    } else {
        serial_puts("\n  FAILED: ");
        serial_puts_num(errors);
        serial_puts(" errors, ");
        serial_puts_num(warnings);
        serial_puts(" warnings\n");
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
        serial_puts_num_hex((uint32_t)current + current->size);
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
    serial_puts_num(heap_total_size);
    serial_puts(" bytes\n");
    serial_puts("  Used:         ");
    serial_puts_num(total_used);
    serial_puts(" bytes (");
    if (heap_total_size > 0) {
        serial_puts_num((total_used * 100) / heap_total_size);
    } else {
        serial_puts("0");
    }
    serial_puts("%)\n");
    serial_puts("  Free:         ");
    serial_puts_num(total_free);
    serial_puts(" bytes (");
    if (heap_total_size > 0) {
        serial_puts_num((total_free * 100) / heap_total_size);
    } else {
        serial_puts("0");
    }
    serial_puts("%)\n");
    serial_puts("===================\n");
    #endif
}

// Совместимость
void* malloc(uint32_t size) {
    return kmalloc(size);
}

void free(void* ptr) {
    kfree(ptr);
}

void* kmalloc_aligned(uint32_t size, uint32_t align) {
    void *ptr = kmalloc(size + align + sizeof(void*));
    if (!ptr) return NULL;
    
    uintptr_t raw = (uintptr_t)ptr;
    uintptr_t aligned = (raw + align - 1) & ~(align - 1);
    uintptr_t *header = (uintptr_t*)(aligned - sizeof(void*));
    *header = raw;
    
    return (void*)aligned;
}

void kfree_aligned(void *ptr) {
    if (!ptr) return;
    uintptr_t *header = (uintptr_t*)((uintptr_t)ptr - sizeof(void*));
    void *raw = (void*)*header;
    kfree(raw);
}