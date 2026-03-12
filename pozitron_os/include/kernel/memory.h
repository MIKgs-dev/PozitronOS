#ifndef KERNEL_MEMORY_H
#define KERNEL_MEMORY_H

#include <stdint.h>
#include <stddef.h>
#include "kernel/multiboot.h"
#include "kernel/paging.h"

// === Конфигурация ===
#define USE_ADVANCED_ALLOCATOR 1
#define HEAP_SIZE 65536
#define BLOCK_SIZE 256
#define MEM_ALIGNMENT 16
#define MEM_BLOCK_MAGIC 0xDEADBEEF
#define PAGE_SIZE 4096
#define DMA_ZONE_LIMIT 0xFFFFFFFF

#define ALIGN(size) (((size) + (MEM_ALIGNMENT-1)) & ~(MEM_ALIGNMENT-1))
#define PAGE_ALIGN(addr) (((uint32_t)(addr) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))

#define KMALLOC_NORMAL   0x00
#define KMALLOC_DMA      0x01
#define KMALLOC_ZERO     0x02

#define MEMORY_TYPE_AVAILABLE 1
#define MEMORY_TYPE_RESERVED 2
#define MEMORY_TYPE_ACPI_RECLAIMABLE 3
#define MEMORY_TYPE_ACPI_NVS 4
#define MEMORY_TYPE_BAD 5

typedef struct memory_map_entry {
    uint32_t size;
    uint32_t base_addr_low;
    uint32_t base_addr_high;
    uint32_t length_low;
    uint32_t length_high;
    uint32_t type;
} __attribute__((packed)) memory_map_entry_t;

typedef struct mem_region {
    uint32_t base;
    uint32_t size;
    uint8_t type;
    uint8_t used;
    uint8_t dma_capable;
    struct mem_region* next;
} mem_region_t;

typedef struct reserved_area {
    uint32_t start;
    uint32_t end;
    const char* description;
} reserved_area_t;

typedef struct kernel_info {
    uint32_t start;
    uint32_t end;
    uint32_t size;
} kernel_info_t;

typedef struct heap_config {
    uint32_t base;
    uint32_t size;
    uint32_t min_size;
    uint32_t max_size;
    uint8_t valid;
} heap_config_t;

typedef struct mem_block {
    uint32_t magic;
    uint32_t size;
    uint8_t free;
    uint8_t dma_safe;
    struct mem_block* next;
    struct mem_block* prev;
} mem_block_t;

typedef struct {
    uint32_t total_memory;
    uint32_t available_memory;
    uint32_t dma_memory;
    uint32_t largest_block;
    uint32_t region_count;
    uint32_t heap_size;
    uint32_t heap_used;
    uint32_t heap_free;
    uint32_t heap_dma_safe;
    uint32_t fragmentation;
} memory_info_t;

void memory_init(void);
void memory_init_multiboot(multiboot_info_t* mb_info);

void* kmalloc(uint32_t size);
void* kmalloc_flags(uint32_t size, uint8_t flags);
void kfree(void* ptr);
void* krealloc(void* ptr, uint32_t size);
void* kcalloc(uint32_t num, uint32_t size);
void* kmalloc_aligned(uint32_t size, uint32_t align);
void kfree_aligned(void* ptr);

void* kmalloc_dma(uint32_t size);
void kfree_dma(void* ptr);

uint32_t virt_to_phys(void* virt);
void* phys_to_virt(uint32_t phys);
uint32_t get_phys_addr(void* virt);
int is_dma_safe(void* ptr);

void parse_memory_map(multiboot_info_t* mb_info);
void print_memory_map(void);
memory_info_t get_memory_info(void);

uint32_t get_total_memory(void);
uint32_t get_free_memory(void);
uint32_t get_dma_memory(void);
void memory_dump(void);
void memory_stats(void);
void heap_validate(void);
void debug_heap_layout(void);
void memory_paging_activated(void);

void* malloc(uint32_t size);
void free(void* ptr);

extern mem_region_t* memory_regions;

#endif