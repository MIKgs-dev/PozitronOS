; IDT загрузка
global idt_load

idt_load:
    mov eax, [esp + 4]  ; Получаем указатель на структуру idt_ptr
    lidt [eax]          ; Загружаем IDT
    ret