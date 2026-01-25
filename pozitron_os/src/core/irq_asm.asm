bits 32

%macro IRQ 2
global irq%1
irq%1:
    cli
    push byte 0          ; Код ошибки
    push byte %2         ; Номер прерывания
    jmp irq_common_stub
%endmacro

extern irq_handler

irq_common_stub:
    ; Сохраняем регистры процессора (pusha)
    pusha
    
    ; Сохраняем сегментные регистры
    push ds
    push es
    push fs
    push gs
    
    ; Загружаем сегмент данных ядра
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    ; ESP сейчас указывает на начало registers_t
    ; Передаем указатель в C
    push esp
    call irq_handler
    
    ; Восстанавливаем стек (убираем указатель)
    add esp, 4
    
    ; Восстанавливаем сегментные регистры
    pop gs
    pop fs
    pop es
    pop ds
    
    ; Восстанавливаем регистры процессора (popa)
    popa
    
    ; Очищаем код ошибки и номер прерывания
    add esp, 8
    
    ; Включаем прерывания и возвращаемся
    sti
    iret

; IRQ 0-15 -> прерывания 32-47
IRQ 0,  32
IRQ 1,  33
IRQ 2,  34
IRQ 3,  35
IRQ 4,  36
IRQ 5,  37
IRQ 6,  38
IRQ 7,  39
IRQ 8,  40
IRQ 9,  41
IRQ 10, 42
IRQ 11, 43
IRQ 12, 44
IRQ 13, 45
IRQ 14, 46
IRQ 15, 47