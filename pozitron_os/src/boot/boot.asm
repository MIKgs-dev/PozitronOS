bits 32

section .multiboot
align 4

multiboot_header:
    ; Multiboot 1 header
    dd 0x1BADB002              ; magic
    dd 0x00000007              ; flags: align + meminfo + graphics
    dd -(0x1BADB002 + 0x00000007) ; checksum
    
    ; Графические поля
    dd 0                       ; header_addr
    dd 0                       ; load_addr  
    dd 0                       ; load_end_addr
    dd 0                       ; bss_end_addr
    dd 0                       ; entry_addr
    
    ; ВАЖНО: Это должно быть 1 для линейного фреймбуфера!
    dd 0                       ; 1 = линейный фреймбуфер (Linear Framebuffer)
    ;dd 1024                    ; ширина
    ;dd 768                     ; высота
    ;dd 32                      ; глубина (бит на пиксель)

section .text
global _start
extern kernel_main

_start:
    ; Настраиваем стек
    mov esp, stack_top
    
    ; Передаем параметры в правильном порядке для C
    push ebx                    ; multiboot_info_t* (второй параметр)
    push eax                    ; magic (первый параметр)
    
    ; Вызываем ядро с параметрами
    call kernel_main
    
    ; Если вернулись - вечный цикл
    cli
.hang:
    hlt
    jmp .hang

section .bss
align 16
stack_bottom:
    resb 16384
stack_top: