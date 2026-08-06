#include "drivers/power.h"
#include "drivers/serial.h"
#include "drivers/ports.h"
#include "drivers/vesa.h"
#include "drivers/acpi.h"
#include "kernel/memory.h"
#include <stddef.h>

static void sys_delay(void) {
    for(volatile int i = 0; i < 4000000; i++) asm volatile("nop");
}

void acpi_shutdown(void) {
    acpi_poweroff();
}

void sys_acpi_reboot(void) {
    acpi_reboot(); 

    serial_puts("[POWER] ACPI Reset failed. Trying PCI reset (0xCF9)...\n");
    outb(0xCF9, 0x02);
    sys_delay();
    outb(0xCF9, 0x06); 
    sys_delay();

    serial_puts("[POWER] Trying PS/2 Controller reset (0x64)...\n");
    uint8_t good = 0x02;
    while (good & 0x02) {
        good = inb(0x64);
    }
    outb(0x64, 0xFE); 
    sys_delay();

    while(1) {
        asm volatile("hlt");
    }
}

void shutdown_computer(void) {
    asm volatile("cli");
    serial_puts("\n=== SHUTDOWN SEQUENCE STARTED ===\n");
    
    serial_puts("[POWER] Triggering sub-system ACPI poweroff...\n");
    
    acpi_poweroff();
    sys_delay();

    serial_puts("[POWER] Fallback: Trying emulator magic ports...\n");
    outw(0xB004, 0x2000);
    sys_delay();
    outw(0x604, 0x2000);
    sys_delay();
    outw(0x4004, 0x3400);
    sys_delay();
    
    vesa_fill(0x000000);
    vesa_draw_text(150, 300, "PozitronOS: It is now safe to turn off your computer.", 0xFFFFFF, 0x000000);
    if (vesa_is_double_buffer_enabled()) vesa_swap_buffers();
    
    while(1) {
        asm volatile("hlt");
    }
}