#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>
#include "core/isr.h"

// Номера системных вызовов
#define SYS_EXIT        0
#define SYS_WRITE       1
#define SYS_READ        2
#define SYS_OPEN        3
#define SYS_CLOSE       4
#define SYS_GETPID      5
#define SYS_BRK         6
#define SYS_SLEEP       7
#define SYS_GETCWD      8
#define SYS_CHDIR       9
#define SYS_MKDIR       10
#define SYS_UNLINK      11
#define SYS_OPENDIR     12
#define SYS_READDIR     13
#define SYS_CLOSEDIR    14
#define SYS_STAT        15

// Инициализация системных вызовов
void syscall_init(void);

// Обработчик системных вызовов
void syscall_handler(registers_t* r);

#endif