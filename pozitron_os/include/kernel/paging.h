#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>
#include "drivers/vesa.h"
#include "core/isr.h"


#define PAGE_SIZE 4096
#define PAGE_TABLE_ENTRIES 1024
#define PAGE_DIR_ENTRIES 1024

// Флаги страниц
#define PAGE_PRESENT   0x01
#define PAGE_WRITABLE  0x02
#define PAGE_USER      0x04
#define PAGE_WRITETHROUGH 0x08
#define PAGE_CACHE_DISABLE 0x10
#define PAGE_ACCESSED  0x20
#define PAGE_DIRTY     0x40
#define PAGE_SIZE_4MB  0x80
#define PAGE_GLOBAL    0x100

// Структура директории страниц
typedef struct {
    uint32_t entries[1024];
} page_directory_t;

// Структура таблицы страниц
typedef struct {
    uint32_t entries[1024];
} page_table_t;

// Инициализация paging
void paging_init(void);

// Создание нового адресного пространства
page_directory_t* paging_create_directory(void);

// Переключение на директорию
void paging_switch_directory(page_directory_t* dir);

// Отображение виртуального адреса на физический
void paging_map_page(page_directory_t* dir, uint32_t virt, uint32_t phys, uint32_t flags);

// Удаление отображения
void paging_unmap_page(page_directory_t* dir, uint32_t virt);

// Получить физический адрес по виртуальному
uint32_t paging_get_physical(page_directory_t* dir, uint32_t virt);

// Копировать память между адресными пространствами
void paging_copy_to_user(page_directory_t* user_dir, void* user_ptr, void* kernel_ptr, uint32_t size);
void paging_copy_from_user(void* kernel_ptr, page_directory_t* user_dir, void* user_ptr, uint32_t size);

// Обработчик page fault (С ПРАВИЛЬНОЙ СИГНАТУРОЙ!)
void page_fault_handler(registers_t* r);

// Текущая директория (extern)
extern page_directory_t* current_directory;

#endif