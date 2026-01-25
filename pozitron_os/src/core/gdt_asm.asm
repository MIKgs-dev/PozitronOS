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