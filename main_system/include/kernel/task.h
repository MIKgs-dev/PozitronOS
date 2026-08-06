#ifndef TASK_H
#define TASK_H

#include "core/isr.h"
#include <stdint.h>
#include "kernel/paging.h"
#include "fs/vfs.h"

#define TASK_STATE_READY    0
#define TASK_STATE_RUNNING  1
#define TASK_STATE_DEAD     2
#define TASK_STATE_SLEEPING 3

#define KERNEL_STACK_SIZE  8192
#define MAX_PROCESS_FILES   16

typedef struct task {
    uint32_t id;
    uint32_t esp;
    uint32_t kstack;
    uint32_t page_directory;
    uint8_t state;
    uint32_t sleep_ticks;
    uint8_t fpu_context_raw[512 + 16];
    struct vfs_file* file_table[MAX_PROCESS_FILES];
    char cwd[256];
    struct task* next;
} task_t;

void task_init(uint32_t kernel_cr3);
void task_create(uint32_t entry_point, uint32_t page_dir);
uint32_t task_switch(uint32_t current_esp);
void task_exit(void);
void task_kill_by_pid(uint32_t pid);

extern task_t* current_task;
extern page_directory_t* kernel_page_directory;

void task_update_timers(void);

#endif