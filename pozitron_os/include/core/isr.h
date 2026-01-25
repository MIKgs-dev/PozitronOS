#ifndef ISR_H
#define ISR_H

#include "../kernel/types.h"

// Номера исключений процессора
#define ISR_DIVISION_ERROR 0
#define ISR_DEBUG 1
#define ISR_NMI 2
#define ISR_BREAKPOINT 3
#define ISR_OVERFLOW 4
#define ISR_BOUND_RANGE 5
#define ISR_INVALID_OPCODE 6
#define ISR_DEVICE_NOT_AVAILABLE 7
#define ISR_DOUBLE_FAULT 8
#define ISR_COPROCESSOR_SEGMENT 9
#define ISR_INVALID_TSS 10
#define ISR_SEGMENT_NOT_PRESENT 11
#define ISR_STACK_SEGMENT_FAULT 12
#define ISR_GENERAL_PROTECTION_FAULT 13
#define ISR_PAGE_FAULT 14
#define ISR_RESERVED 15
#define ISR_X87_FPU 16
#define ISR_ALIGNMENT_CHECK 17
#define ISR_MACHINE_CHECK 18
#define ISR_SIMD_FPU 19
#define ISR_VIRTUALIZATION 20
#define ISR_CONTROL_PROTECTION 21
#define ISR_RESERVED_START 22
#define ISR_RESERVED_END 31

// Структура для сохранения регистров при прерывании
typedef struct {
    // Регистры, сохраненные pusha
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    
    // Сегментные регистры
    uint32_t gs, fs, es, ds;
    
    // Номер прерывания и код ошибки (заталкиваются перед переходом в stub)
    uint32_t int_no, err_code;
    
    // Регистры процессора, сохраненные процессором автоматически
    uint32_t eip, cs, eflags, useresp, ss;
} registers_t;

// Тип обработчика прерываний
typedef void (*isr_handler_t)(registers_t*);

// Функции
void isr_init(void);
void isr_install_handler(uint8_t num, isr_handler_t handler);
void isr_uninstall_handler(uint8_t num);
extern void isr_install(void);

// Обработчики по умолчанию
void isr_default_handler(registers_t* r);

#endif