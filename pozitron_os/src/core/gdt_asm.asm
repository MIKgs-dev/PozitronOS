; GDT загрузка
global gdt_load

gdt_load:
    mov eax, [esp + 4]  ; Получаем указатель на структуру gdt_ptr
    lgdt [eax]          ; Загружаем GDT
    
    ; Перезагружаем сегментные регистры
    mov ax, 0x10        ; Селектор сегмента данных
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    
    ; Дальний переход для обновления CS
    jmp 0x08:.flush     ; 0x08 - селектор сегмента кода
.flush:
    ret

; Загрузка TSS
global tss_flush
tss_flush:
    mov ax, 0x2B        ; GDT_TSS_SELECTOR
    ltr ax
    ret

; Переход в пользовательский режим
; gdt_asm.asm - исправленная версия

global jump_to_userspace
jump_to_userspace:
    cli
    mov eax, [esp + 4]     ; Адрес функции
    
    ; Сегменты данных пользователя (Ring 3)
    mov ax, 0x23           ; Селектор данных пользователя (Ring 3)
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    ; Стек пользователя (убедитесь, что адрес правильный)
    push 0x23              ; SS пользователя
    push 0x8000            ; ESP пользователя (должен быть валидный адрес!)
    
    ; EFLAGS: включаем прерывания, IOPL=0
    pushfd
    pop ebx
    or ebx, 0x200          ; Set IF (прерывания)
    and ebx, ~0x3000       ; Clear IOPL (биты 12-13)
    push ebx
    popfd
    push ebx               ; Сохраняем EFLAGS для iret
    
    push 0x1B              ; CS пользователя (Ring 3)
    push eax               ; EIP пользователя
    
    iret