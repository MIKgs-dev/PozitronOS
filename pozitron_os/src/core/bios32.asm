bits 32

global bios_int
extern gdt32
extern gdt32_end
extern gdt_ptr

section .text

; Структура для перехода в реальный режим
struc regs16_t
    .di: resw 1
    .si: resw 1
    .bp: resw 1
    .sp: resw 1
    .bx: resw 1
    .dx: resw 1
    .cx: resw 1
    .ax: resw 1
    .gs: resw 1
    .fs: resw 1
    .es: resw 1
    .ds: resw 1
    .eflags: resw 1
endstruc

; Временный GDT для реального режима
gdt16:
    dq 0x0000000000000000    ; NULL
    dq 0x00009a000000ffff    ; 16-bit code
    dq 0x000092000000ffff    ; 16-bit data
gdt16_end:

gdt16_ptr:
    dw gdt16_end - gdt16 - 1
    dd gdt16

; Переход в реальный режим и вызов BIOS
bios_int:
    pusha
    push es
    push ds
    
    ; Сохраняем текущий GDT
    sgdt [gdt_ptr]
    
    ; Выключаем прерывания
    cli
    
    ; Сохраняем ESP
    mov [.esp32], esp
    
    ; Переход в 16-bit защищённый режим
    jmp 0x08:.pm16

bits 16
.pm16:
    ; Загружаем 16-bit GDT
    lgdt [gdt16_ptr]
    
    ; Переход в реальный режим
    mov eax, cr0
    and al, 0xFE
    mov cr0, eax
    
    ; Дальний прыжок для обновления CS
    jmp 0x0000:.real

.real:
    ; Восстанавливаем сегменты
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    
    ; Включаем прерывания
    sti
    
    ; Подготавливаем регистры для BIOS
    mov ax, [esp + regs16_t.ax]
    mov bx, [esp + regs16_t.bx]
    mov cx, [esp + regs16_t.cx]
    mov dx, [esp + regs16_t.dx]
    mov si, [esp + regs16_t.si]
    mov di, [esp + regs16_t.di]
    mov bp, [esp + regs16_t.bp]
    
    ; Вызываем BIOS
    int 0x10
    
    ; Сохраняем результат
    pushf
    push ax
    push bx
    push cx
    push dx
    
    ; Выключаем прерывания
    cli
    
    ; Возвращаемся в защищённый режим
    mov eax, cr0
    or al, 1
    mov cr0, eax
    
    ; Дальний прыжок для обновления CS
    jmp 0x08:.pm32

bits 32
.pm32:
    ; Восстанавливаем 32-bit GDT
    lgdt [gdt_ptr]
    
    ; Обновляем сегменты
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    
    ; Восстанавливаем ESP
    mov esp, [.esp32]
    
    ; Восстанавливаем результаты
    pop edx
    pop ecx
    pop ebx
    pop eax
    popf
    
    ; Возвращаемся
    pop ds
    pop es
    popa
    ret

.esp32: dd 0

; Указатель на GDT
gdt_ptr: dd 0, 0