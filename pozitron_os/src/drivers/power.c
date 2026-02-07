#include "drivers/power.h"
#include "drivers/serial.h"
#include "drivers/ports.h"
#include "drivers/vesa.h"
#include "kernel/memory.h"
#include <stddef.h>

// ============ ДОПОЛНИТЕЛЬНЫЕ МЕТОДЫ ВЫКЛЮЧЕНИЯ ============

// 1. Метод через PCI (для современных систем)
static void pci_shutdown(void) {
    serial_puts("[POWER] Attempting PCI shutdown...\n");
    
    // Ищем PCI устройство управления питанием
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t device = 0; device < 32; device++) {
            uint32_t id = 0x80000000 | (bus << 16) | (device << 11) | (0 << 8);
            outl(0xCF8, id);
            uint32_t vendor_device = inl(0xCFC);
            
            if (vendor_device != 0xFFFFFFFF) {
                // Проверяем на устройство управления питанием
                uint8_t class_code = inb(0xCFC + 0x0B);
                uint8_t subclass = inb(0xCFC + 0x0A);
                
                if (class_code == 0x0C && subclass == 0x05) { // Power Management Controller
                    serial_puts("[POWER] Found PCI PM controller\n");
                    outl(0xCF8, id + 0x40);
                    outw(0xCFC, 0x8000); // Команда выключения
                    return;
                }
            }
        }
    }
}

// 2. Метод через PIIX3 (для старых материнских плат)
static void piix3_shutdown(void) {
    serial_puts("[POWER] Attempting PIIX3 shutdown...\n");
    
    // PIIX3 Power Management Registers
    outb(0x5100, 0x01);  // PMBASE + 0x00 = PM1a_CNT
    outw(0x5104, 0x3400); // SLP_TYP = 5h, SLP_EN = 1
}

// 3. Метод через SMBIOS (System Management BIOS)
static void smbios_shutdown(void) {
    serial_puts("[POWER] Attempting SMBIOS shutdown...\n");
    
    // Ищем SMBIOS entry point
    for (uint32_t addr = 0xF0000; addr < 0xFFFFF; addr += 16) {
        if (*(uint8_t*)addr == '_' &&
            *(uint8_t*)(addr + 1) == 'S' &&
            *(uint8_t*)(addr + 2) == 'M' &&
            *(uint8_t*)(addr + 3) == '_') {
            
            serial_puts("[POWER] Found SMBIOS entry point\n");
            
            // Пытаемся вызвать SMI (System Management Interrupt)
            asm volatile(
                "mov $0x5307, %%ax\n"
                "mov $0x0001, %%bx\n"
                "mov $0x0003, %%cx\n"
                "int $0x15\n"
                ::: "eax", "ebx", "ecx"
            );
            return;
        }
    }
}

// 4. Метод через Intel ICH (I/O Controller Hub)
static void ich_shutdown(void) {
    serial_puts("[POWER] Attempting ICH shutdown...\n");
    
    // ICH Power Management
    outb(0x0B2, 0x01);  // ICH APM_CNT
    outb(0x0B3, 0x31);  // Выключение
}

// 5. Метод через AMD Magic Packet
static void amd_shutdown(void) {
    serial_puts("[POWER] Attempting AMD shutdown...\n");
    
    // AMD PM регистры
    outb(0xCD6, 0x03);  // SB_ACPI_CNTL
    outb(0xCD7, 0x01);  // SMI_CMD
    outb(0xB2, 0x01);   // APM_CNT
    outb(0xB3, 0x31);   // Выключение
}

// 6. Метод через UEFI Runtime Services (если есть)
static void uefi_shutdown(void) {
    serial_puts("[POWER] Attempting UEFI shutdown...\n");
    
    // Пытаемся найти UEFI System Table
    uint32_t* rsdp_ptr = (uint32_t*)0xE0000;
    for (int i = 0; i < 0x20000 / 4; i++) {
        if (rsdp_ptr[i] == 0x2052545020445352) { // "RSD PTR "
            serial_puts("[POWER] Found UEFI system table\n");
            
            // Системный сброс через порт 0xCF9
            outb(0xCF9, 0x06);
            return;
        }
    }
}

// 7. Метод через Watchdog Timer
static void watchdog_shutdown(void) {
    serial_puts("[POWER] Attempting Watchdog shutdown...\n");
    
    // Настраиваем watchdog на немедленное срабатывание
    outb(0x443, 0x01);  // Запускаем watchdog
    outb(0x442, 0x00);  // Минимальное время
}

// 8. Метод через Keyboard Controller Reset
static void kbc_reset_shutdown(void) {
    serial_puts("[POWER] Attempting KBC reset shutdown...\n");
    
    // Сбрасываем через контроллер клавиатуры
    outb(0x64, 0xFE);  // Команда сброса
    
    // Ждем немного
    for (volatile int i = 0; i < 10000; i++);
    
    // Пробуем снова с другим кодом
    outb(0x64, 0x01);
    outb(0x60, 0xFE);
}

// 9. Метод через CPU Triple Fault (экстренный)
static void triple_fault_shutdown(void) {
    serial_puts("[POWER] Attempting triple fault shutdown...\n");
    
    // Создаем тройную ошибку для перезагрузки
    asm volatile(
        "cli\n"
        "lidt 0\n"
        "int $0xFF\n"
    );
}

// 10. Метод через Port 0xEE (некоторые ноутбуки)
static void port_ee_shutdown(void) {
    serial_puts("[POWER] Attempting port 0xEE shutdown...\n");
    
    outb(0xEE, 0x01);  // Некоторые ноутбуки Acer/Compaq
    outb(0xEE, 0x02);
    outb(0xEE, 0x03);
    outb(0xEE, 0x04);
}

// ============ УЛУЧШЕННЫЕ ОСНОВНЫЕ МЕТОДЫ ============

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
    
    // Подключаемся к APM
    asm volatile(
        "mov $0x5301, %%ax\n"
        "xor %%bx, %%bx\n"
        "int $0x15\n"
        ::: "eax", "ebx"
    );
    
    // Устанавливаем версию
    asm volatile(
        "mov $0x530E, %%ax\n"
        "xor %%bx, %%bx\n"
        "mov $0x0102, %%cx\n"  // Версия 1.2
        "int $0x15\n"
        ::: "eax", "ebx", "ecx"
    );
    
    // Выключаем
    asm volatile(
        "mov $0x5307, %%ax\n"
        "mov $0x0001, %%bx\n"
        "mov $0x0003, %%cx\n"  // Выключение питания
        "int $0x15\n"
        ::: "eax", "ebx", "ecx"
    );
}

static void acpi_shutdown(void) {
    serial_puts("[POWER] Attempting ACPI shutdown...\n");
    
    // Попытка найти таблицу ACPI
    uint8_t found = 0;
    uint32_t rsdp_addr = 0;
    
    // Ищем RSDP в области памяти
    for (uint32_t addr = 0x000E0000; addr < 0x000FFFFF; addr += 16) {
        if (*(uint8_t*)addr == 'R' && 
            *(uint8_t*)(addr + 1) == 'S' &&
            *(uint8_t*)(addr + 2) == 'D' &&
            *(uint8_t*)(addr + 3) == ' ' &&
            *(uint8_t*)(addr + 4) == 'P' &&
            *(uint8_t*)(addr + 5) == 'T' &&
            *(uint8_t*)(addr + 6) == 'R' &&
            *(uint8_t*)(addr + 7) == ' ') {
            
            rsdp_addr = addr;
            found = 1;
            serial_puts("[POWER] Found ACPI RSDP\n");
            break;
        }
    }
    
    if (!found) {
        serial_puts("[POWER] ACPI not found, trying common ports\n");
        // Пробуем стандартные порты
        outw(0xB004, 0x2000);  // QEMU
        outw(0x604, 0x2000);   // VirtualBox
        outw(0x4004, 0x3400);  // Bochs
        return;
    }
    
    // Если нашли RSDP, используем S5 состояние
    // PM1a_CNT порт обычно 0x1004 для QEMU
    outw(0x1004, 0x3400);  // SLP_TYP = 5, SLP_EN = 1
    
    // Альтернативные порты
    outw(0xB004, 0x2000);
    outw(0x604, 0x2000);
}

static void port_shutdown(void) {
    serial_puts("[POWER] Attempting comprehensive port shutdown...\n");
    
    // Попробуем все известные порты выключения в правильном порядке
    
    // 1. Стандартный ACPI
    outw(0xB004, 0x2000);  // QEMU
    for(volatile int i = 0; i < 1000; i++);
    
    // 2. VirtualBox/VMware
    outw(0x604, 0x2000);
    for(volatile int i = 0; i < 1000; i++);
    
    // 3. Bochs
    outw(0x4004, 0x3400);
    for(volatile int i = 0; i < 1000; i++);
    
    // 4. Стандартный порт сброса
    outw(0xCF9, 0x0E);
    for(volatile int i = 0; i < 1000; i++);
    
    // 5. Порт для некоторых ноутбуков
    outb(0xEE, 0x01);
}

// ============ ФИНАЛЬНАЯ ФУНКЦИЯ ВЫКЛЮЧЕНИЯ ============

void shutdown_computer(void) {
    serial_puts("\n=== SHUTDOWN SEQUENCE STARTED ===\n");
    
    // Отключаем прерывания
    asm volatile("cli");
    
    // Пробуем все методы выключения с задержками между ними
    serial_puts("[POWER] Trying method 1: ACPI...\n");
    acpi_shutdown();
    
    // Задержка между попытками
    for(volatile int i = 0; i < 500000; i++) asm volatile("nop");
    
    serial_puts("[POWER] Trying method 2: APM...\n");
    apm_shutdown();
    
    for(volatile int i = 0; i < 500000; i++) asm volatile("nop");
    
    serial_puts("[POWER] Trying method 3: Comprehensive ports...\n");
    port_shutdown();
    
    for(volatile int i = 0; i < 500000; i++) asm volatile("nop");
    
    serial_puts("[POWER] Trying method 4: PCI...\n");
    pci_shutdown();
    
    for(volatile int i = 0; i < 500000; i++) asm volatile("nop");
    
    serial_puts("[POWER] Trying method 5: PIIX3...\n");
    piix3_shutdown();
    
    for(volatile int i = 0; i < 500000; i++) asm volatile("nop");
    
    serial_puts("[POWER] Trying method 6: SMBIOS...\n");
    smbios_shutdown();
    
    for(volatile int i = 0; i < 500000; i++) asm volatile("nop");
    
    serial_puts("[POWER] Trying method 7: Intel ICH...\n");
    ich_shutdown();
    
    for(volatile int i = 0; i < 500000; i++) asm volatile("nop");
    
    serial_puts("[POWER] Trying method 8: AMD...\n");
    amd_shutdown();
    
    for(volatile int i = 0; i < 500000; i++) asm volatile("nop");
    
    serial_puts("[POWER] Trying method 9: UEFI...\n");
    uefi_shutdown();
    
    for(volatile int i = 0; i < 500000; i++) asm volatile("nop");
    
    serial_puts("[POWER] Trying method 10: Watchdog...\n");
    watchdog_shutdown();
    
    for(volatile int i = 0; i < 500000; i++) asm volatile("nop");
    
    serial_puts("[POWER] Trying method 11: KBC reset...\n");
    kbc_reset_shutdown();
    
    for(volatile int i = 0; i < 500000; i++) asm volatile("nop");
    
    serial_puts("[POWER] Trying method 12: Port 0xEE...\n");
    port_ee_shutdown();
    
    // Если все методы не сработали, показываем сообщение
    serial_puts("[POWER] All shutdown methods failed\n");
    serial_puts("[POWER] It is now safe to turn off your computer\n");
    
    // Очищаем экран черным цветом
    vesa_fill(0x000000);
    
    // Показываем сообщение
    uint32_t width = vesa_get_width();
    uint32_t height = vesa_get_height();
    
    if (width > 640 && height > 480) {
        vesa_draw_text(width/2 - 200, height/2, 
                      "It is now safe to turn off your computer", 
                      0xFFFFFF, 0x000000);
    }
    
    // Окончательный метод: тройная ошибка (экстренный сброс)
    serial_puts("[POWER] Trying emergency triple fault...\n");
    for(volatile int i = 0; i < 1000000; i++) asm volatile("nop");
    triple_fault_shutdown();
    
    // Вечный цикл ожидания (запасной вариант)
    serial_puts("[POWER] Entering infinite loop...\n");
    while(1) {
        asm volatile("hlt");
    }
}