bits 32

section .multiboot
align 4

multiboot_header:
    ; Магическое число + флаги
    dd 0x1BADB002              ; Multiboot 1 magic
    dd 0x00000007              ; Флаги: 
                               ; бит 0 = выравнивание модулей
                               ; бит 1 = информация о памяти  
                               ; бит 2 = графическая информация
    dd -(0x1BADB002 + 0x00000007) ; Контрольная сумма
    
    ; Графические поля (обязательны при бите 2!)
    dd 0                       ; header_addr
    dd 0                       ; load_addr
    dd 0                       ; load_end_addr
    dd 0                       ; bss_end_addr
    dd 0                       ; entry_addr
    
    ; Графический режим
    dd 0                       ; 0 = графический режим
    dd 1024                    ; ширина
    dd 768                     ; высота
    dd 32                      ; глубина (бит на пиксель)

section .text
global _start
extern kernel_main

_start:
    ; Настраиваем стек
    mov esp, stack_top
    
    ; Сохраняем указатель на структуру multiboot
    push ebx
    
    ; Вызываем ядро
    call kernel_main
    
    ; Если вернулись - вечный цикл
    cli
.hang:
    hlt
    jmp .hang

section .bss
align 16
stack_bottom:
    resb 16384                 ; 16 КБ стек
stack_top: