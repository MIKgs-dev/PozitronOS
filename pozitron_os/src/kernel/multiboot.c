#include "kernel/multiboot.h"
#include "drivers/serial.h"
#include "drivers/vesa.h"
#include <stddef.h>

// Проверяем, загружены ли мы GRUB
int multiboot_check(uint32_t magic) {
    if (magic == MULTIBOOT_BOOTLOADER_MAGIC) {
        serial_puts("[MB] Multiboot 1 detected\n");
        return 1;
    }
    if (magic == MULTIBOOT2_BOOTLOADER_MAGIC) {
        serial_puts("[MB] Multiboot 2 detected\n");
        return 2;
    }
    
    serial_puts("[MB] ERROR: Invalid multiboot magic: 0x");
    serial_puts_num_hex(magic);
    serial_puts("\n");
    return 0;
}

// Получаем информацию о фреймбуфере из Multiboot 1
void* multiboot_get_framebuffer(multiboot_info_t* mb_info) {
    if (!mb_info) return NULL;
    
    // Проверяем флаг фреймбуфера
    if (!(mb_info->flags & (1 << 12))) {
        serial_puts("[MB] No framebuffer info from GRUB\n");
        return NULL;
    }
    
    serial_puts("[MB] Framebuffer from GRUB:\n");
    serial_puts("  Addr: 0x");
    serial_puts_num_hex((uint32_t)mb_info->framebuffer_addr);
    serial_puts("\n");
    serial_puts("  Size: ");
    serial_puts_num(mb_info->framebuffer_width);
    serial_puts("x");
    serial_puts_num(mb_info->framebuffer_height);
    serial_puts("x");
    serial_puts_num(mb_info->framebuffer_bpp);
    serial_puts("\n");
    serial_puts("  Pitch: ");
    serial_puts_num(mb_info->framebuffer_pitch);
    serial_puts("\n");
    
    return (void*)(uintptr_t)mb_info->framebuffer_addr;
}

// Получаем разрешение экрана
void multiboot_get_resolution(multiboot_info_t* mb_info, uint32_t* width, uint32_t* height, uint32_t* bpp) {
    if (!mb_info || !(mb_info->flags & (1 << 12))) {
        // Нет информации - возвращаем значения по умолчанию
        if (width) *width = 1024;
        if (height) *height = 768;
        if (bpp) *bpp = 32;
        return;
    }
    
    if (width) *width = mb_info->framebuffer_width;
    if (height) *height = mb_info->framebuffer_height;
    if (bpp) *bpp = mb_info->framebuffer_bpp;
}

// Демп информации о Multiboot
void multiboot_dump_info(multiboot_info_t* mb_info) {
    if (!mb_info) {
        serial_puts("[MB] No multiboot info\n");
        return;
    }
    
    serial_puts("\n=== MULTIBOOT INFORMATION ===\n");
    serial_puts("Flags: 0x");
    serial_puts_num_hex(mb_info->flags);
    serial_puts("\n");
    
    if (mb_info->flags & (1 << 0)) {
        serial_puts("Memory: ");
        serial_puts_num(mb_info->mem_lower);
        serial_puts("KB lower, ");
        serial_puts_num(mb_info->mem_upper);
        serial_puts("KB upper\n");
    }
    
    if (mb_info->flags & (1 << 1)) {
        serial_puts("Boot device: 0x");
        serial_puts_num_hex(mb_info->boot_device);
        serial_puts("\n");
    }
    
    if (mb_info->flags & (1 << 2)) {
        serial_puts("Command line: ");
        if (mb_info->cmdline) {
            serial_puts((char*)(uintptr_t)mb_info->cmdline);
        }
        serial_puts("\n");
    }
    
    if (mb_info->flags & (1 << 3)) {
        serial_puts("Modules: ");
        serial_puts_num(mb_info->mods_count);
        serial_puts(" at 0x");
        serial_puts_num_hex(mb_info->mods_addr);
        serial_puts("\n");
    }
    
    if (mb_info->flags & (1 << 4)) {
        serial_puts("ELF symbols\n");
    }
    
    if (mb_info->flags & (1 << 5)) {
        serial_puts("Memory map: ");
        serial_puts_num(mb_info->mmap_length);
        serial_puts(" bytes at 0x");
        serial_puts_num_hex(mb_info->mmap_addr);
        serial_puts("\n");
    }
    
    if (mb_info->flags & (1 << 6)) {
        serial_puts("Drives info\n");
    }
    
    if (mb_info->flags & (1 << 7)) {
        serial_puts("Config table\n");
    }
    
    if (mb_info->flags & (1 << 8)) {
        serial_puts("Boot loader name: ");
        if (mb_info->boot_loader_name) {
            serial_puts((char*)(uintptr_t)mb_info->boot_loader_name);
        }
        serial_puts("\n");
    }
    
    if (mb_info->flags & (1 << 9)) {
        serial_puts("APM table\n");
    }
    
    if (mb_info->flags & (1 << 10)) {
        serial_puts("VBE info\n");
    }
    
    if (mb_info->flags & (1 << 11)) {
        serial_puts("Framebuffer info (legacy)\n");
    }
    
    if (mb_info->flags & (1 << 12)) {
        serial_puts("Framebuffer info:\n");
        serial_puts("  Addr: 0x");
        serial_puts_num_hex((uint32_t)mb_info->framebuffer_addr);
        serial_puts("\n");
        serial_puts("  Size: ");
        serial_puts_num(mb_info->framebuffer_width);
        serial_puts("x");
        serial_puts_num(mb_info->framebuffer_height);
        serial_puts("\n");
        serial_puts("  BPP: ");
        serial_puts_num(mb_info->framebuffer_bpp);
        serial_puts("\n");
        serial_puts("  Pitch: ");
        serial_puts_num(mb_info->framebuffer_pitch);
        serial_puts("\n");
    }
    
    serial_puts("================================\n\n");
}