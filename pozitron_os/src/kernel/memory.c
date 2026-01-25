#include "kernel/memory.h"
#include "drivers/serial.h"
#include "drivers/vga.h"
#include <stddef.h>

// === Глобальные переменные ===

#if USE_ADVANCED_ALLOCATOR
// Для нового аллокатора
static mem_block_t* heap_start = NULL;
static mem_block_t* heap_end = NULL;
static uint32_t heap_total_size = 0;
static uint32_t heap_initialized = 0;

#else
// Для старого аллокатора
static uint8_t heap[HEAP_SIZE];
static uint8_t heap_used[HEAP_SIZE / BLOCK_SIZE] = {0};
#endif

// === Вспомогательные функции для нового аллокатора ===
#if USE_ADVANCED_ALLOCATOR

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
    if (!block || block->size < size + sizeof(mem_block_t) + MEM_ALIGNMENT) {
        return;
    }
    
    // Создаем новый свободный блок из оставшегося пространства
    mem_block_t* new_block = (mem_block_t*)((uint8_t*)block + size);
    
    new_block->magic = MEM_BLOCK_MAGIC;
    new_block->size = block->size - size;
    new_block->free = 1;
    new_block->next = block->next;
    new_block->prev = block;
    
    // Обновляем связи
    block->size = size;
    block->free = 0;
    block->next = new_block;
    
    if (new_block->next) {
        new_block->next->prev = new_block;
    }
    
    // Если этот блок был в конце, обновляем heap_end
    if (block == heap_end) {
        heap_end = new_block;
    }
}

// Слияние свободных блоков
static void merge_blocks(mem_block_t* block) {
    if (!block || !block->free) return;
    
    // Слияние с правым блоком
    if (block->next && block->next->free) {
        block->size += block->next->size;
        block->next = block->next->next;
        
        if (block->next) {
            block->next->prev = block;
        }
        
        // Если удалили конечный блок
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
        
        // Если удалили конечный блок
        if (!block->prev->next) {
            heap_end = block->prev;
        }
        
        block = block->prev;
    }
}

// Получение статистики
static void get_heap_stats_internal(heap_stats_t* stats) {
    if (!stats) return;
    
    stats->total_size = heap_total_size;
    stats->used_size = 0;
    stats->free_size = 0;
    stats->block_count = 0;
    stats->free_blocks = 0;
    stats->used_blocks = 0;
    
    mem_block_t* current = heap_start;
    while (current) {
        stats->block_count++;
        
        if (current->free) {
            stats->free_size += current->size;
            stats->free_blocks++;
        } else {
            stats->used_size += current->size;
            stats->used_blocks++;
        }
        
        current = current->next;
    }
    
    // Вычисляем фрагментацию
    if (stats->free_blocks > 1) {
        stats->fragmentation = (stats->free_blocks - 1) * 100 / stats->free_blocks;
    } else {
        stats->fragmentation = 0;
    }
}

#endif // USE_ADVANCED_ALLOCATOR

// === Общедоступные функции ===

// Инициализация памяти
void memory_init(void) {
    serial_puts("[MEM] Initializing memory system...\n");
    
    #if USE_ADVANCED_ALLOCATOR
    serial_puts("[MEM] Using ADVANCED allocator\n");
    
    // Инициализируем кучу по адресу после BSS
    extern uint8_t end;  // Символ из linker.ld - конец ядра
    uint32_t heap_addr = (uint32_t)&end;
    
    // Выравниваем адрес
    heap_addr = (heap_addr + 4095) & ~4095;  // Выравнивание по 4KB
    
    // Размер кучи - 32MB
    uint32_t heap_size = 32 * 1024 * 1024;
    
    heap_start = (mem_block_t*)heap_addr;
    heap_end = (mem_block_t*)(heap_addr + heap_size);
    
    // Инициализируем первый блок
    heap_start->magic = MEM_BLOCK_MAGIC;
    heap_start->size = heap_size;
    heap_start->free = 1;
    heap_start->next = NULL;
    heap_start->prev = NULL;
    
    heap_total_size = heap_size;
    heap_initialized = 1;
    
    serial_puts("[MEM] Advanced heap at 0x");
    uint32_t addr = heap_addr;
    char hex[] = "0123456789ABCDEF";
    for(int j = 28; j >= 0; j -= 4) {
        if(addr >> j) {
            serial_write(hex[(addr >> j) & 0xF]);
        }
    }
    serial_puts(", size=");
    serial_puts_num(heap_size);
    serial_puts(" bytes\n");
    
    #else
    serial_puts("[MEM] Using SIMPLE allocator\n");
    serial_puts("[MEM] Fixed-size heap: ");
    serial_puts_num(HEAP_SIZE);
    serial_puts(" bytes\n");
    #endif
}

// Выделение памяти (основная функция)
void* kmalloc(uint32_t size) {
    if (size == 0) return NULL;
    
    #if USE_ADVANCED_ALLOCATOR
    // === Новый аллокатор ===
    if (!heap_initialized) {
        serial_puts("[MEM] ERROR: Heap not initialized!\n");
        return NULL;
    }
    
    // Выравниваем размер и добавляем размер заголовка
    uint32_t total_size = ALIGN(size + sizeof(mem_block_t));
    
    // Ищем свободный блок
    mem_block_t* block = find_free_block(total_size);
    
    if (!block) {
        serial_puts("[MEM] ERROR: Out of memory! Requested ");
        serial_puts_num(size);
        serial_puts(" bytes\n");
        return NULL;
    }
    
    // Разделяем блок, если есть достаточно места
    if (block->size >= total_size + sizeof(mem_block_t) + MEM_ALIGNMENT) {
        split_block(block, total_size);
    } else {
        block->free = 0;
    }
    
    // Возвращаем указатель на данные
    void* ptr = (void*)((uint8_t*)block + sizeof(mem_block_t));
    
    // Отладочный вывод
    #ifdef DEBUG_MEMORY
    serial_puts("[MEM] Allocated ");
    serial_puts_num(size);
    serial_puts(" bytes at 0x");
    uint32_t addr = (uint32_t)ptr;
    char hex[] = "0123456789ABCDEF";
    for(int j = 28; j >= 0; j -= 4) {
        if(addr >> j) {
            serial_write(hex[(addr >> j) & 0xF]);
        }
    }
    serial_puts("\n");
    #endif
    
    return ptr;
    
    #else
    // === Старый аллокатор ===
    uint32_t blocks_needed = (size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    if(blocks_needed == 0) blocks_needed = 1;
    
    for(uint32_t i = 0; i < (HEAP_SIZE / BLOCK_SIZE) - blocks_needed; i++) {
        uint8_t found = 1;
        
        for(uint32_t j = 0; j < blocks_needed; j++) {
            if(heap_used[i + j]) {
                found = 0;
                break;
            }
        }
        
        if(found) {
            for(uint32_t j = 0; j < blocks_needed; j++) {
                heap_used[i + j] = 1;
            }
            
            void* ptr = &heap[i * BLOCK_SIZE];
            
            #ifdef DEBUG_MEMORY
            serial_puts("[MEM] Allocated ");
            serial_puts_num(size);
            serial_puts(" bytes at 0x");
            uint32_t addr = (uint32_t)ptr;
            char hex[] = "0123456789ABCDEF";
            for(int j = 28; j >= 0; j -= 4) {
                if(addr >> j) {
                    serial_write(hex[(addr >> j) & 0xF]);
                }
            }
            serial_puts("\n");
            #endif
            
            return ptr;
        }
    }
    
    serial_puts("[MEM] ERROR: Out of memory! Requested ");
    serial_puts_num(size);
    serial_puts(" bytes\n");
    return NULL;
    #endif
}

// Освобождение памяти
void kfree(void* ptr) {
    if (!ptr) return;
    
    #if USE_ADVANCED_ALLOCATOR
    // === Новый аллокатор ===
    if (!heap_initialized) {
        serial_puts("[MEM] ERROR: Heap not initialized!\n");
        return;
    }
    
    // Получаем указатель на заголовок
    mem_block_t* block = (mem_block_t*)((uint8_t*)ptr - sizeof(mem_block_t));
    
    // Проверяем магическое число
    if (block->magic != MEM_BLOCK_MAGIC) {
        serial_puts("[MEM] ERROR: Invalid free! Bad magic number at 0x");
        uint32_t addr = (uint32_t)ptr;
        char hex[] = "0123456789ABCDEF";
        for(int j = 28; j >= 0; j -= 4) {
            if(addr >> j) {
                serial_write(hex[(addr >> j) & 0xF]);
            }
        }
        serial_puts("\n");
        return;
    }
    
    // Помечаем как свободный
    block->free = 1;
    
    // Сливаем с соседними свободными блоками
    merge_blocks(block);
    
    #ifdef DEBUG_MEMORY
    serial_puts("[MEM] Freed memory at 0x");
    uint32_t addr = (uint32_t)ptr;
    char hex[] = "0123456789ABCDEF";
    for(int j = 28; j >= 0; j -= 4) {
        if(addr >> j) {
            serial_write(hex[(addr >> j) & 0xF]);
        }
    }
    serial_puts("\n");
    #endif
    
    #else
    // === Старый аллокатор ===
    uint32_t offset = (uint32_t)ptr - (uint32_t)heap;
    if(offset >= HEAP_SIZE) {
        serial_puts("[MEM] ERROR: Invalid free! Not in heap\n");
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

// Перераспределение памяти (только для нового аллокатора)
void* krealloc(void* ptr, uint32_t size) {
    #if USE_ADVANCED_ALLOCATOR
    if (!ptr) {
        return kmalloc(size);
    }
    
    if (size == 0) {
        kfree(ptr);
        return NULL;
    }
    
    // Получаем текущий блок
    mem_block_t* block = (mem_block_t*)((uint8_t*)ptr - sizeof(mem_block_t));
    
    if (block->magic != MEM_BLOCK_MAGIC) {
        serial_puts("[MEM] ERROR: krealloc on invalid pointer\n");
        return NULL;
    }
    
    uint32_t old_size = block->size - sizeof(mem_block_t);
    
    // Если размер уменьшается или остается таким же
    if (size <= old_size) {
        uint32_t new_total_size = ALIGN(size + sizeof(mem_block_t));
        if (block->size >= new_total_size + sizeof(mem_block_t) + MEM_ALIGNMENT) {
            split_block(block, new_total_size);
        }
        return ptr;
    }
    
    // Пробуем расширить текущий блок за счет соседнего свободного
    if (block->next && block->next->free && 
        (block->size + block->next->size) >= ALIGN(size + sizeof(mem_block_t))) {
        
        block->size += block->next->size;
        block->next = block->next->next;
        
        if (block->next) {
            block->next->prev = block;
        }
        
        // Разделяем, если есть лишнее место
        uint32_t new_total_size = ALIGN(size + sizeof(mem_block_t));
        if (block->size >= new_total_size + sizeof(mem_block_t) + MEM_ALIGNMENT) {
            split_block(block, new_total_size);
        }
        
        return ptr;
    }
    
    // Не можем расширить - выделяем новый блок
    void* new_ptr = kmalloc(size);
    if (!new_ptr) {
        return NULL;
    }
    
    // Копируем данные
    uint32_t copy_size = (old_size < size) ? old_size : size;
    uint8_t* src = (uint8_t*)ptr;
    uint8_t* dst = (uint8_t*)new_ptr;
    
    for (uint32_t i = 0; i < copy_size; i++) {
        dst[i] = src[i];
    }
    
    // Освобождаем старый блок
    kfree(ptr);
    
    return new_ptr;
    
    #else
    // Старый аллокатор не поддерживает realloc
    serial_puts("[MEM] ERROR: krealloc not supported in simple allocator\n");
    return NULL;
    #endif
}

// Выделение и обнуление памяти
void* kcalloc(uint32_t num, uint32_t size) {
    uint32_t total = num * size;
    void* ptr = kmalloc(total);
    
    if (ptr) {
        // Обнуляем память
        uint8_t* p = (uint8_t*)ptr;
        for (uint32_t i = 0; i < total; i++) {
            p[i] = 0;
        }
    }
    
    return ptr;
}

// Получение общего объема памяти
uint32_t get_total_memory(void) {
    #if USE_ADVANCED_ALLOCATOR
    return heap_total_size;
    #else
    return HEAP_SIZE;
    #endif
}

// Получение свободной памяти
uint32_t get_free_memory(void) {
    #if USE_ADVANCED_ALLOCATOR
    heap_stats_t stats;
    get_heap_stats_internal(&stats);
    return stats.free_size;
    #else
    uint32_t free_mem = 0;
    for(uint32_t i = 0; i < HEAP_SIZE / BLOCK_SIZE; i++) {
        if(!heap_used[i]) free_mem += BLOCK_SIZE;
    }
    return free_mem;
    #endif
}

// Дамп информации о памяти
void memory_dump(void) {
    serial_puts("\n=== MEMORY DUMP ===\n");
    
    #if USE_ADVANCED_ALLOCATOR
    if (!heap_initialized) {
        serial_puts("Heap not initialized\n");
        return;
    }
    
    mem_block_t* current = heap_start;
    uint32_t index = 0;
    
    while (current) {
        serial_puts("Block ");
        serial_puts_num(index);
        serial_puts(": Addr=0x");
        
        uint32_t addr = (uint32_t)current;
        char hex[] = "0123456789ABCDEF";
        for(int j = 28; j >= 0; j -= 4) {
            if(addr >> j) {
                serial_write(hex[(addr >> j) & 0xF]);
            }
        }
        
        serial_puts(", Size=");
        serial_puts_num(current->size);
        
        serial_puts(", ");
        if (current->free) {
            serial_puts("FREE");
        } else {
            serial_puts("USED");
        }
        
        serial_puts("\n");
        
        current = current->next;
        index++;
    }
    
    heap_stats_t stats;
    get_heap_stats_internal(&stats);
    
    serial_puts("\n=== STATISTICS ===\n");
    serial_puts("Total: "); serial_puts_num(stats.total_size); serial_puts(" bytes\n");
    serial_puts("Used: "); serial_puts_num(stats.used_size); serial_puts(" bytes ("); 
    serial_puts_num(stats.used_blocks); serial_puts(" blocks)\n");
    serial_puts("Free: "); serial_puts_num(stats.free_size); serial_puts(" bytes ("); 
    serial_puts_num(stats.free_blocks); serial_puts(" blocks)\n");
    serial_puts("Fragmentation: "); serial_puts_num(stats.fragmentation); serial_puts("%\n");
    
    #else
    serial_puts("[SIMPLE ALLOCATOR]\n");
    serial_puts("Heap size: "); serial_puts_num(HEAP_SIZE); serial_puts(" bytes\n");
    
    uint32_t used_blocks = 0;
    for(uint32_t i = 0; i < HEAP_SIZE / BLOCK_SIZE; i++) {
        if(heap_used[i]) used_blocks++;
    }
    
    serial_puts("Used: "); serial_puts_num(used_blocks * BLOCK_SIZE); 
    serial_puts(" bytes ("); serial_puts_num(used_blocks); serial_puts(" blocks)\n");
    
    uint32_t free_blocks = (HEAP_SIZE / BLOCK_SIZE) - used_blocks;
    serial_puts("Free: "); serial_puts_num(free_blocks * BLOCK_SIZE); 
    serial_puts(" bytes ("); serial_puts_num(free_blocks); serial_puts(" blocks)\n");
    #endif
    
    serial_puts("==================\n");
}

// Проверка целостности кучи (только для нового аллокатора)
void heap_validate(void) {
    #if USE_ADVANCED_ALLOCATOR
    if (!heap_initialized) {
        serial_puts("[MEM] Heap not initialized\n");
        return;
    }
    
    serial_puts("[MEM] Validating heap... ");
    
    mem_block_t* current = heap_start;
    uint32_t errors = 0;
    
    while (current) {
        if (current->magic != MEM_BLOCK_MAGIC) {
            errors++;
        }
        
        if (current->size < sizeof(mem_block_t)) {
            errors++;
        }
        
        if (current->next && current->next->prev != current) {
            errors++;
        }
        
        current = current->next;
    }
    
    if (errors == 0) {
        serial_puts("OK\n");
    } else {
        serial_puts("\n[MEM] Found ");
        serial_puts_num(errors);
        serial_puts(" errors\n");
    }
    #else
    serial_puts("[MEM] Heap validation not supported in simple allocator\n");
    #endif
}

// === Старые функции для совместимости ===
void* malloc(uint32_t size) {
    return kmalloc(size);
}

void free(void* ptr) {
    kfree(ptr);
}