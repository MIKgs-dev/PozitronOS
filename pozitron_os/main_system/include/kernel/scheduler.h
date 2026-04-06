#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdint.h>

#define MAX_TASKS 16
#define TASK_STACK_SIZE 4096
#define TASK_TIME_SLICE 5  // Тиков на задачу (~50мс при 100Гц)

typedef enum {
    TASK_READY = 0,
    TASK_RUNNING,
    TASK_WAITING,
    TASK_TERMINATED
} task_state_t;

typedef struct {
    uint32_t id;
    task_state_t state;
    uint32_t esp;           // Указатель стека
    uint32_t eip;           // Точка входа
    void* stack;            // Стек задачи
    uint32_t stack_size;    // Размер стека
    uint32_t ticks_left;    // Сколько тиков осталось
    uint32_t total_ticks;   // Сколько всего отработала
    char name[32];
    void* arg;              // Аргумент задачи
} task_t;

// Основные функции
void scheduler_init(void);
int task_create(void (*entry)(void*), void* arg, const char* name);
void task_exit(void);
void task_yield(void);
void scheduler_tick(void);  // Вызывается из таймера

// Управление задачами
void task_sleep(uint32_t ticks);
void task_wake(task_t* task);
task_t* task_get_current(void);
void task_dump_all(void);

#endif