#ifndef KERNEL_MEMORY_H
#define KERNEL_MEMORY_H

#include <stdint.h>
#include <stddef.h>
#include "kernel/multiboot.h"

// === Конфигурация ===
#define USE_ADVANCED_ALLOCATOR 1  // 1 = новый аллокатор, 0 = старый

// === Общие настройки ===
#define HEAP_SIZE 65536           // Размер статической кучи (для простого аллокатора)
#define BLOCK_SIZE 256            // Размер блока в простом аллокаторе
#define MEM_ALIGNMENT 16          // Выравнивание памяти
#define MEM_BLOCK_MAGIC 0xDEADBEEF // Магическое число для проверки
#define PAGE_SIZE 4096            // Размер страницы

// Выравнивание
#define ALIGN(size) (((size) + (MEM_ALIGNMENT-1)) & ~(MEM_ALIGNMENT-1))
#define PAGE_ALIGN(addr) (((uint32_t)(addr) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))

// === Структуры для парсинга mmap ===
typedef struct memory_map_entry {
    uint32_t size;
    uint32_t base_addr_low;
    uint32_t base_addr_high;
    uint32_t length_low;
    uint32_t length_high;
    uint32_t type;
} __attribute__((packed)) memory_map_entry_t;

// Типы регионов памяти
#define MEMORY_TYPE_AVAILABLE 1
#define MEMORY_TYPE_RESERVED 2
#define MEMORY_TYPE_ACPI_RECLAIMABLE 3
#define MEMORY_TYPE_ACPI_NVS 4
#define MEMORY_TYPE_BAD 5

// Регион памяти
typedef struct mem_region {
    uint32_t base;
    uint32_t size;
    uint8_t type;
    uint8_t used;           // 0 = свободен, 1 = используется
    struct mem_region* next;
} mem_region_t;

// Зарезервированная область
typedef struct reserved_area {
    uint32_t start;
    uint32_t end;
    const char* description;
} reserved_area_t;

// Информация о ядре
typedef struct kernel_info {
    uint32_t start;
    uint32_t end;
    uint32_t size;
} kernel_info_t;

// Конфигурация кучи
typedef struct heap_config {
    uint32_t base;
    uint32_t size;
    uint32_t min_size;
    uint32_t max_size;
    uint8_t valid;
} heap_config_t;

// === Структуры для нового аллокатора ===
typedef struct mem_block {
    uint32_t magic;           // Магическое число
    uint32_t size;            // Размер блока (включая заголовок)
    uint8_t free;             // 1 = свободен, 0 = занят
    struct mem_block* next;   // Следующий блок
    struct mem_block* prev;   // Предыдущий блок
} mem_block_t;

// Статистика памяти
typedef struct {
    uint32_t total_memory;     // Общая память системы
    uint32_t available_memory; // Доступная память
    uint32_t largest_block;    // Самый большой свободный блок
    uint32_t region_count;     // Количество регионов
    uint32_t heap_size;        // Размер кучи
    uint32_t heap_used;        // Используется в куче
    uint32_t heap_free;        // Свободно в куче
    uint32_t fragmentation;    // Фрагментация (в процентах)
} memory_info_t;

// === Интерфейс памяти ===

// Инициализация памяти
void memory_init(void);
void memory_init_multiboot(multiboot_info_t* mb_info);

// Основные функции аллокатора
void* kmalloc(uint32_t size);
void kfree(void* ptr);
void* krealloc(void* ptr, uint32_t size);
void* kcalloc(uint32_t num, uint32_t size);
void* kmalloc_aligned(uint32_t size, uint32_t align);
void kfree_aligned(void* ptr);

// Функции для работы с mmap
void parse_memory_map(multiboot_info_t* mb_info);
void print_memory_map(void);
memory_info_t get_memory_info(void);

// Утилиты
uint32_t get_total_memory(void);
uint32_t get_free_memory(void);
void memory_dump(void);
void memory_stats(void);
void heap_validate(void);

// === Старые функции для совместимости ===
void* malloc(uint32_t size);
void free(void* ptr);

static inline uint32_t virt_to_phys(void* virt) {
    return (uint32_t)virt;
}

static inline void* phys_to_virt(uint32_t phys) {
    return (void*)phys;
}

#endif