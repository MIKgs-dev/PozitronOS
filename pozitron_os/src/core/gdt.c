#include "core/gdt.h"

// GDT таблица
static struct gdt_entry gdt[6];
static struct gdt_ptr gp;

// Внешняя функция из gdt_asm.asm
extern void gdt_load(struct gdt_ptr* ptr);

// Установка записи в GDT
void gdt_set_entry(int index, uint32_t base, uint32_t limit, uint8_t access, uint8_t granularity) {
    gdt[index].base_low = (base & 0xFFFF);
    gdt[index].base_middle = (base >> 16) & 0xFF;
    gdt[index].base_high = (base >> 24) & 0xFF;
    
    gdt[index].limit_low = (limit & 0xFFFF);
    gdt[index].granularity = ((limit >> 16) & 0x0F) | (granularity & 0xF0);
    
    gdt[index].access = access;
}

// Инициализация GDT
void gdt_init(void) {
    // Устанавливаем лимит GDT
    gp.limit = (sizeof(struct gdt_entry) * 6) - 1;
    gp.base = (uint32_t)&gdt;
    
    // 1. Нулевой дескриптор (обязателен)
    gdt_set_entry(GDT_NULL, 0, 0, 0, 0);
    
    // 2. Сегмент кода ядра
    gdt_set_entry(GDT_CODE,
                  0,                         // База = 0
                  0xFFFFFFFF,                // Лимит = 4GB
                  GDT_ACCESS_PRESENT | GDT_ACCESS_RING0 | GDT_ACCESS_CODE_SEG | GDT_ACCESS_CODE_READ,
                  GDT_GRAN_4KB | GDT_GRAN_32BIT);
    
    // 3. Сегмент данных ядра
    gdt_set_entry(GDT_DATA,
                  0,
                  0xFFFFFFFF,
                  GDT_ACCESS_PRESENT | GDT_ACCESS_RING0 | GDT_ACCESS_DATA_SEG | GDT_ACCESS_DATA_WRITE,
                  GDT_GRAN_4KB | GDT_GRAN_32BIT);
    
    // 4. Сегмент кода пользователя (ring 3)
    gdt_set_entry(GDT_USER_CODE,
                  0,
                  0xFFFFFFFF,
                  GDT_ACCESS_PRESENT | GDT_ACCESS_RING3 | GDT_ACCESS_CODE_SEG | GDT_ACCESS_CODE_READ,
                  GDT_GRAN_4KB | GDT_GRAN_32BIT);
    
    // 5. Сегмент данных пользователя (ring 3)
    gdt_set_entry(GDT_USER_DATA,
                  0,
                  0xFFFFFFFF,
                  GDT_ACCESS_PRESENT | GDT_ACCESS_RING3 | GDT_ACCESS_DATA_SEG | GDT_ACCESS_DATA_WRITE,
                  GDT_GRAN_4KB | GDT_GRAN_32BIT);
    
    // 6. TSS (пока заглушка, реализуем позже)
    gdt_set_entry(GDT_TSS,
                  0,
                  0,
                  0,
                  0);
    
    // Загружаем GDT
    gdt_load(&gp);
}