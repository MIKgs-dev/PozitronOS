#include "drivers/power.h"
#include "drivers/serial.h"
#include "drivers/ports.h"
#include "drivers/vesa.h"
#include "kernel/memory.h"
#include <stddef.h>

// ============ АПМ (Advanced Power Management) ============
static uint8_t apm_check(void) {
    serial_puts("[POWER] Checking APM...\n");
    
    // Проверяем наличие APM
    outb(0x70, 0x53);
    uint8_t apm_ver = inb(0x71);
    
    if (apm_ver != 0x01) {
        serial_puts("[POWER] APM not available\n");
        return 0;
    }
    
    serial_puts("[POWER] APM version: ");
    serial_puts_num(apm_ver >> 4);
    serial_puts(".");
    serial_puts_num(apm_ver & 0x0F);
    serial_puts("\n");
    return 1;
}

static void apm_shutdown(void) {
    if (!apm_check()) return;
    
    serial_puts("[POWER] Attempting APM shutdown...\n");
    
    // Отключаем все устройства через APM
    outb(0x70, 0x07);
    outb(0x71, 0x00);
    
    // Отправляем команду выключения
    asm volatile(
        "movw $0x1000, %%ax\n"
        "movw $0x2001, %%bx\n"
        "movw $0x0000, %%cx\n"
        "int $0x15\n"
        ::: "eax", "ebx", "ecx"
    );
}

// ============ ACPI ============
static void acpi_shutdown(void) {
    serial_puts("[POWER] Attempting ACPI shutdown...\n");
    
    // Попытка найти таблицу ACPI
    uint8_t found = 0;
    
    for (uint32_t addr = 0x000E0000; addr < 0x000FFFFF; addr += 16) {
        if (*(uint8_t*)addr == 'R' && 
            *(uint8_t*)(addr + 1) == 'S' &&
            *(uint8_t*)(addr + 2) == 'D' &&
            *(uint8_t*)(addr + 3) == ' ' &&
            *(uint8_t*)(addr + 4) == 'P' &&
            *(uint8_t*)(addr + 5) == 'T' &&
            *(uint8_t*)(addr + 6) == 'R' &&
            *(uint8_t*)(addr + 7) == ' ') {
            
            serial_puts("[POWER] Found ACPI RSDP at 0x");
            uint32_t a = addr;
            char hex[] = "0123456789ABCDEF";
            for(int j = 28; j >= 0; j -= 4) {
                if(a >> j) serial_write(hex[(a >> j) & 0xF]);
            }
            serial_puts("\n");
            found = 1;
            break;
        }
    }
    
    if (!found) {
        serial_puts("[POWER] ACPI not found\n");
        return;
    }
    
    // Попытка выключения через порт ACPI
    outw(0xB004, 0x2000);  // Для QEMU
    outw(0x604, 0x2000);   // Для VirtualBox
    outw(0x4004, 0x3400);  // Для Bochs
}

// ============ Порты выключения ============
static void port_shutdown(void) {
    serial_puts("[POWER] Attempting port shutdown...\n");
    
    // QEMU
    outw(0xB004, 0x2000);
    
    // VirtualBox/VMware
    outw(0x604, 0x2000);
    
    // Bochs
    outw(0x4004, 0x3400);
    
    // Стандартный порт
    outw(0xCF9, 0x0E);
}

// ============ PS/2 контроллер ============
static void ps2_shutdown(void) {
    serial_puts("[POWER] Attempting PS/2 shutdown...\n");
    
    // Отключаем клавиатуру
    outb(0x64, 0xAD);
    outb(0x64, 0xA7);
    
    // Команда выключения через PS/2
    outb(0x64, 0xFE);
}

// ============ EFI эмуляция ============
static void efi_shutdown(void) {
    serial_puts("[POWER] Attempting EFI shutdown...\n");
    
    // Системный сброс через порт 0xCF9
    outb(0xCF9, 0x06);
    
    // Альтернативный способ
    outb(0x64, 0xFE);
}

// ============ ФИНАЛЬНАЯ ФУНКЦИЯ ВЫКЛЮЧЕНИЯ ============
void shutdown_computer(void) {
    serial_puts("\n=== SHUTDOWN SEQUENCE STARTED ===\n");
    
    // 1. Сохраняем время RTC перед выключением
    serial_puts("[POWER] Saving RTC time...\n");
    
    // 2. Пробуем все методы выключения
    apm_shutdown();
    
    // Небольшая задержка между попытками
    for(int i = 0; i < 1000000; i++) asm volatile("nop");
    
    acpi_shutdown();
    
    for(int i = 0; i < 1000000; i++) asm volatile("nop");
    
    port_shutdown();
    
    for(int i = 0; i < 1000000; i++) asm volatile("nop");
    
    ps2_shutdown();
    
    for(int i = 0; i < 1000000; i++) asm volatile("nop");
    
    efi_shutdown();
    
    // 3. Если все методы не сработали, показываем сообщение
    serial_puts("[POWER] All shutdown methods failed\n");
    serial_puts("[POWER] It is now safe to turn off your computer\n");
    
    // Очищаем экран черным цветом
    vesa_fill(0x000000);
    
    // Показываем сообщение
    vesa_draw_text(320, 350, "It is now safe to turn off your computer", 0xFFFFFF, 0x000000);
    
    // Вечный цикл ожидания
    serial_puts("[POWER] Entering infinite loop...\n");
    asm volatile("cli");
    while(1) {
        asm volatile("hlt");
    }
}