#include "kernel/scheduler.h"
#include "kernel/memory.h"
#include "drivers/serial.h"
#include "core/isr.h"

static task_t tasks[MAX_TASKS];
static uint32_t task_count = 0;
static uint32_t current_task = 0;
static uint32_t next_task_id = 1;
static uint8_t scheduler_running = 0;

// Сохранение контекста при переключении
static void switch_task(uint32_t new_esp) {
    if (!scheduler_running) return;
    
    // Сохраняем ESP текущей задачи
    if (tasks[current_task].state == TASK_RUNNING) {
        tasks[current_task].state = TASK_READY;
        asm volatile("mov %%esp, %0" : "=r"(tasks[current_task].esp));
    }
    
    // Ищем следующую готовую задачу
    uint32_t next = (current_task + 1) % MAX_TASKS;
    uint32_t start = next;
    
    while (tasks[next].state != TASK_READY && tasks[next].state != TASK_RUNNING) {
        next = (next + 1) % MAX_TASKS;
        if (next == start) {
            // Нет готовых задач - возвращаемся к текущей
            next = current_task;
            break;
        }
    }
    
    current_task = next;
    tasks[current_task].state = TASK_RUNNING;
    tasks[current_task].ticks_left = TASK_TIME_SLICE;
    
    // Переключаемся на новую задачу
    asm volatile(
        "mov %0, %%esp\n"
        "popa\n"
        "sti\n"
        "ret"
        : : "r"(tasks[current_task].esp)
    );
}

// Инициализация планировщика
void scheduler_init(void) {
    for (int i = 0; i < MAX_TASKS; i++) {
        tasks[i].state = TASK_TERMINATED;
    }
    task_count = 1;  // Текущая задача (ядро)
    current_task = 0;
    scheduler_running = 1;
    
    serial_puts("[SCHED] Scheduler initialized\n");
}

// Создание новой задачи
int task_create(void (*entry)(void*), void* arg, const char* name) {
    if (task_count >= MAX_TASKS) {
        serial_puts("[SCHED] Max tasks reached\n");
        return -1;
    }
    
    // Находим свободный слот
    int slot = -1;
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_TERMINATED) {
            slot = i;
            break;
        }
    }
    if (slot == -1) return -1;
    
    // Выделяем стек
    void* stack = kmalloc(TASK_STACK_SIZE);
    if (!stack) return -1;
    
    // Настраиваем начальный контекст
    uint32_t* stack_top = (uint32_t*)((uint32_t)stack + TASK_STACK_SIZE - 4);
    
    // Имитируем стек после прерывания
    *(--stack_top) = 0x202;  // EFLAGS (IF=1)
    *(--stack_top) = 0x08;    // CS (сегмент кода)
    *(--stack_top) = (uint32_t)entry;  // EIP
    *(--stack_top) = 0;       // EAX
    *(--stack_top) = 0;       // ECX
    *(--stack_top) = 0;       // EDX
    *(--stack_top) = 0;       // EBX
    *(--stack_top) = 0;       // ESP (не используется)
    *(--stack_top) = 0;       // EBP
    *(--stack_top) = 0;       // ESI
    *(--stack_top) = 0;       // EDI
    
    tasks[slot].id = next_task_id++;
    tasks[slot].state = TASK_READY;
    tasks[slot].esp = (uint32_t)stack_top;
    tasks[slot].eip = (uint32_t)entry;
    tasks[slot].stack = stack;
    tasks[slot].stack_size = TASK_STACK_SIZE;
    tasks[slot].ticks_left = TASK_TIME_SLICE;
    tasks[slot].total_ticks = 0;
    tasks[slot].arg = arg;
    
    // Копируем имя
    const char* s = name;
    char* d = tasks[slot].name;
    while (*s && d < tasks[slot].name + 31) {
        *d++ = *s++;
    }
    *d = '\0';
    
    task_count++;
    
    serial_puts("[SCHED] Task created: ");
    serial_puts(name);
    serial_puts(" (ID: ");
    serial_puts_num(tasks[slot].id);
    serial_puts(")\n");
    
    return tasks[slot].id;
}

// Завершение текущей задачи
void task_exit(void) {
    uint32_t tid = tasks[current_task].id;
    
    // Освобождаем стек
    if (tasks[current_task].stack) {
        kfree(tasks[current_task].stack);
    }
    
    tasks[current_task].state = TASK_TERMINATED;
    task_count--;
    
    serial_puts("[SCHED] Task ");
    serial_puts_num(tid);
    serial_puts(" exited\n");
    
    // Переключаемся на другую задачу
    task_yield();
}

// Добровольная передача управления
void task_yield(void) {
    asm volatile("int $0x20");  // Используем таймер для переключения
}

// Обработчик таймера (вытесняющая многозадачность)
void scheduler_tick(void) {
    if (!scheduler_running) return;
    
    if (tasks[current_task].state == TASK_RUNNING) {
        tasks[current_task].ticks_left--;
        tasks[current_task].total_ticks++;
        
        if (tasks[current_task].ticks_left <= 0) {
            // Квант времени истёк - переключаем задачу
            tasks[current_task].ticks_left = TASK_TIME_SLICE;
            
            // Сохраняем контекст
            asm volatile(
                "pusha\n"
                "mov %%esp, %0\n"
                : "=r"(tasks[current_task].esp)
                :
                : "memory"
            );
            
            // Ищем следующую задачу
            uint32_t next = (current_task + 1) % MAX_TASKS;
            uint32_t start = next;
            
            while (tasks[next].state != TASK_READY) {
                next = (next + 1) % MAX_TASKS;
                if (next == start) {
                    next = current_task;
                    break;
                }
            }
            
            if (next != current_task) {
                tasks[current_task].state = TASK_READY;
                current_task = next;
                tasks[current_task].state = TASK_RUNNING;
                
                // Восстанавливаем контекст
                asm volatile(
                    "mov %0, %%esp\n"
                    "popa\n"
                    : : "r"(tasks[current_task].esp)
                    : "memory"
                );
            }
        }
    }
}

// Получить текущую задачу
task_t* task_get_current(void) {
    if (!scheduler_running) return NULL;
    return &tasks[current_task];
}

// Дамп всех задач
void task_dump_all(void) {
    serial_puts("\n=== TASK LIST ===\n");
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state != TASK_TERMINATED) {
            serial_puts("  [");
            serial_puts_num(i);
            serial_puts("] ");
            serial_puts(tasks[i].name);
            serial_puts(" (ID:");
            serial_puts_num(tasks[i].id);
            serial_puts(") ");
            
            switch(tasks[i].state) {
                case TASK_READY: serial_puts("READY"); break;
                case TASK_RUNNING: serial_puts("RUNNING"); break;
                case TASK_WAITING: serial_puts("WAITING"); break;
                default: break;
            }
            
            serial_puts(" ticks:");
            serial_puts_num(tasks[i].total_ticks);
            serial_puts("\n");
        }
    }
    serial_puts("================\n");
}