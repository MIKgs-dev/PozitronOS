#include "kernel/userspace.h"
#include "core/gdt.h"
#include "drivers/serial.h"
#include "kernel/paging.h"
#include "kernel/memory.h"

// Код пользователя в виде массива байт (машинный код)
static uint8_t user_code[] = {
    // mov eax, 0x1234
    0xB8, 0x34, 0x12, 0x00, 0x00,
    // inc eax
    0x40,
    // jmp back
    0xEB, 0xF9
};

// Стек пользователя
static uint8_t* user_stack;

void userspace_init(void) {
    serial_puts("[USER] Initializing userspace...\n");
    
    // Выделяем память для кода пользователя
    uint8_t* code_page = (uint8_t*)kmalloc_aligned(4096, 4096);
    if (!code_page) {
        serial_puts("[USER] Failed to allocate code page!\n");
        return;
    }
    
    // Копируем код
    for (int i = 0; i < sizeof(user_code); i++) {
        code_page[i] = user_code[i];
    }
    
    // Выделяем стек пользователя
    user_stack = (uint8_t*)kmalloc_aligned(4096, 4096);
    if (!user_stack) {
        serial_puts("[USER] Failed to allocate stack!\n");
        return;
    }
    
    serial_puts("[USER] Code at: 0x");
    serial_puts_num_hex((uint32_t)code_page);
    serial_puts("\n");
    
    serial_puts("[USER] Stack at: 0x");
    serial_puts_num_hex((uint32_t)user_stack);
    serial_puts("\n");
    
    // Отображаем страницы с флагом USER
    paging_map_page(current_directory, 
                   (uint32_t)code_page, 
                   (uint32_t)code_page, 
                   PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
    
    paging_map_page(current_directory, 
                   (uint32_t)user_stack, 
                   (uint32_t)user_stack, 
                   PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
    
    // ВАЖНО: Устанавливаем стек ядра для TSS
    // Используем текущий стек ядра или выделенный для этой цели
    uint32_t kernel_stack = 0;  // Нужно получить адрес стека ядра!
    tss_set_stack(0x10, kernel_stack);
    
    serial_puts("[USER] Jumping to Ring 3...\n");
    jump_to_userspace((void (*)(void))code_page);
}