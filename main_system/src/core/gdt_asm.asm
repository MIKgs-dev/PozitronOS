bits 32
section .text

global gdt_flush
global tss_flush
global jump_to_userspace

gdt_flush:
    mov eax, [esp+4]
    lgdt [eax]
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    jmp 0x08:.flush
.flush:
    ret

tss_flush:
    mov ax, 0x28
    ltr ax
    ret

global jump_to_userspace

jump_to_userspace:
    mov eax, [esp+4]
    mov ecx, [esp+8]
    mov edx, [esp+12]
    
    cli
    
    test edx, edx
    jz .no_cr3
    mov cr3, edx
.no_cr3:
    
    mov bx, 0x23
    mov ds, bx
    mov es, bx
    mov fs, bx
    mov gs, bx
    
    push dword 0x23
    push dword ecx
    
    pushfd
    pop ebx
    or ebx, 0x200
    push ebx
    
    push dword 0x1B
    push dword eax
    
    iret