#ifndef KERNEL_MEMORY_H
#define KERNEL_MEMORY_H

#include <stdint.h>
#include <stddef.h>

// === Конфигурация ===
#define USE_ADVANCED_ALLOCATOR 1  // 1 = новый аллокатор, 0 = старый

// === Общие настройки ===
#define HEAP_SIZE 65536           // Размер статической кучи
#define BLOCK_SIZE 256            // Размер блока в простом аллокаторе
#define MEM_ALIGNMENT 16          // Выравнивание памяти
#define MEM_BLOCK_MAGIC 0xDEADBEEF // Магическое число для проверки

// Выравнивание
#define ALIGN(size) (((size) + (MEM_ALIGNMENT-1)) & ~(MEM_ALIGNMENT-1))

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
    uint32_t total_size;      // Общий размер кучи
    uint32_t used_size;       // Используемая память
    uint32_t free_size;       // Свободная память
    uint32_t block_count;     // Всего блоков
    uint32_t free_blocks;     // Свободных блоков
    uint32_t used_blocks;     // Занятых блоков
    uint32_t fragmentation;   // Фрагментация (в процентах)
} heap_stats_t;

// === Интерфейс памяти ===

// Инициализация памяти
void memory_init(void);

// Основные функции
void* kmalloc(uint32_t size);
void kfree(void* ptr);
void* krealloc(void* ptr, uint32_t size);
void* kcalloc(uint32_t num, uint32_t size);

// Утилиты
uint32_t get_total_memory(void);
uint32_t get_free_memory(void);
void memory_dump(void);
void heap_validate(void);

// === Старые функции для совместимости ===
void* malloc(uint32_t size);
void free(void* ptr);

#endif