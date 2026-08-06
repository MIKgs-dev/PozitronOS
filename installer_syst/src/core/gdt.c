#include "core/gdt.h"
#include "drivers/serial.h"
#include "kernel/memory.h"
#include "kernel/paging.h"

// GDT таблица
static struct gdt_entry gdt[6];
static struct gdt_ptr gp;
static struct tss_entry tss;

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

// Установка TSS (переименовано)
static void install_tss(void) {
    serial_puts("[GDT] Installing TSS...\n");
    
    // Очищаем TSS
    uint8_t* tss_ptr = (uint8_t*)&tss;
    for (int i = 0; i < sizeof(tss); i++) {
        tss_ptr[i] = 0;
    }
    
    // Устанавливаем стек ядра (ВАЖНО: нужно указать реальный стек!)
    tss.ss0 = GDT_DATA_SELECTOR;  // 0x10
    tss.esp0 = 0;  // Позже установим через tss_set_stack
    
    // Добавляем TSS в GDT
    uint32_t tss_base = (uint32_t)&tss;
    uint32_t tss_limit = sizeof(tss) - 1;
    
    serial_puts("[GDT] TSS at: 0x");
    serial_puts_num_hex(tss_base);
    serial_puts("\n");
    
    // ВАЖНО: TSS должен быть доступен из Ring 3!
    gdt_set_entry(GDT_TSS, tss_base, tss_limit, 0x89, 0x40);  // Изменено: добавили бит доступности
}

// Инициализация GDT
void gdt_init(void) {
    serial_puts("[GDT] Initializing...\n");
    
    gp.limit = (sizeof(struct gdt_entry) * 6) - 1;
    gp.base = (uint32_t)&gdt;
    
    // 1. Нулевой дескриптор
    gdt_set_entry(GDT_NULL, 0, 0, 0, 0);
    
    // 2. Сегмент кода ядра
    gdt_set_entry(GDT_CODE,
                  0, 0xFFFFFFFF,
                  GDT_ACCESS_PRESENT | GDT_ACCESS_RING0 | GDT_ACCESS_CODE_SEG | GDT_ACCESS_CODE_READ,
                  GDT_GRAN_4KB | GDT_GRAN_32BIT);
    
    // 3. Сегмент данных ядра
    gdt_set_entry(GDT_DATA,
                  0, 0xFFFFFFFF,
                  GDT_ACCESS_PRESENT | GDT_ACCESS_RING0 | GDT_ACCESS_DATA_SEG | GDT_ACCESS_DATA_WRITE,
                  GDT_GRAN_4KB | GDT_GRAN_32BIT);
    
    // 4. Сегмент кода пользователя
    gdt_set_entry(GDT_USER_CODE,
                  0, 0xFFFFFFFF,
                  GDT_ACCESS_PRESENT | GDT_ACCESS_RING3 | GDT_ACCESS_CODE_SEG | GDT_ACCESS_CODE_READ,
                  GDT_GRAN_4KB | GDT_GRAN_32BIT);
    
    // 5. Сегмент данных пользователя
    gdt_set_entry(GDT_USER_DATA,
                  0, 0xFFFFFFFF,
                  GDT_ACCESS_PRESENT | GDT_ACCESS_RING3 | GDT_ACCESS_DATA_SEG | GDT_ACCESS_DATA_WRITE,
                  GDT_GRAN_4KB | GDT_GRAN_32BIT);
    
    // === ТУТ ВАЖНО: СНАЧАЛА ИНИЦИАЛИЗИРУЕМ TSS ===
    serial_puts("[GDT] Installing TSS...\n");
    
    // Очищаем TSS
    uint8_t* tss_ptr = (uint8_t*)&tss;
    for (int i = 0; i < sizeof(tss); i++) {
        tss_ptr[i] = 0;
    }
    
    tss.ss0 = GDT_DATA_SELECTOR;  // 0x10
    tss.esp0 = 0;
    
    // Добавляем TSS в GDT
    uint32_t tss_base = (uint32_t)&tss;
    uint32_t tss_limit = sizeof(tss) - 1;
    
    serial_puts("[GDT] TSS at: 0x");
    serial_puts_num_hex(tss_base);
    serial_puts("\n");
    
    gdt_set_entry(GDT_TSS, tss_base, tss_limit, 0x89, 0x00);
    
    // === ТЕПЕРЬ ЗАГРУЖАЕМ GDT (УЖЕ С TSS) ===
    serial_puts("[GDT] Loading GDT with TSS...\n");
    gdt_load(&gp);
    
    // Загружаем TSS в TR
    tss_flush();
    
    serial_puts("[GDT] Initialized with Ring 3 support\n");
}

// Установка стека для TSS (отдельно)
void tss_set_stack(uint32_t ss0, uint32_t esp0) {
    tss.ss0 = ss0;
    tss.esp0 = esp0;
}