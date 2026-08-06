#include "kernel/task.h"
#include "kernel/memory.h"
#include "kernel/paging.h"
#include "drivers/serial.h"
#include "lib/string.h"
#include "core/gdt.h"
#include "kernel/notif.h"

task_t* task_list_head = NULL;
task_t* current_task = NULL;
static uint32_t next_pid = 1;
page_directory_t* kernel_page_directory = NULL;

static inline void* get_fpu_aligned_ptr(task_t* task) {
    uint32_t addr = (uint32_t)task->fpu_context_raw;
    addr = (addr + 15) & ~15;
    return (void*)addr;
}

void task_init(uint32_t kernel_cr3) {
    serial_puts("[TASK] Initializing multitasking...\n");
    
    kernel_page_directory = (page_directory_t*)kernel_cr3;
    
    task_t* kernel_task = (task_t*)kmalloc(sizeof(task_t));
    if (!kernel_task) {
        notif_error("Task Mgr", "Critical: Failed to allocate memory for Kernel Task (PID 0)");
        while(1);
    }
    
    kernel_task->id = 0;
    kernel_task->esp = 0;
    kernel_task->kstack = 0;
    kernel_task->page_directory = kernel_cr3;
    kernel_task->state = TASK_STATE_RUNNING;
    
    for (int i = 0; i < MAX_PROCESS_FILES; i++) {
        kernel_task->file_table[i] = NULL;
    }
    
    kernel_task->next = kernel_task;
    
    task_list_head = kernel_task;
    current_task = kernel_task;
    
    serial_puts("[TASK] Multitasking initialized. Kernel task (PID 0) ready.\n");
}

void task_update_timers(void) {
    if (!task_list_head) return;

    task_t* curr = task_list_head;
    do {
        if (curr->state == TASK_STATE_SLEEPING) {
            if (curr->sleep_ticks > 0) {
                curr->sleep_ticks--;
            }
            
            if (curr->sleep_ticks == 0) {
                
                curr->state = TASK_STATE_READY;
            }
        }
        curr = curr->next;
    } while (curr != task_list_head);
}

void task_create(uint32_t entry_point, uint32_t page_dir) {
    task_t* new_task = (task_t*)kmalloc(sizeof(task_t));
    if (!new_task) {
        notif_printf(NOTIF_ERROR, "Task Mgr", "Failed to allocate TCB for entry point: 0x%08X", entry_point);
        return;
    }
    
    memset(new_task->fpu_context_raw, 0, 512 + 16);
    uint32_t fpu_addr = (uint32_t)new_task->fpu_context_raw;
    fpu_addr = (fpu_addr + 15) & ~15;
    void* new_fpu_ptr = (void*)fpu_addr;
    
    asm volatile(
        "fninit\n\t"
        "fxsave %0"
        : "=m"(*(char*)new_fpu_ptr)
    );

    new_task->id = next_pid++;
    new_task->page_directory = page_dir;
    new_task->state = TASK_STATE_READY;

    for (int i = 0; i < MAX_PROCESS_FILES; i++) {
        new_task->file_table[i] = NULL;
    }
    
    uint32_t kstack_mem = (uint32_t)kmalloc_aligned(KERNEL_STACK_SIZE, 16);
    if (!kstack_mem) {
        notif_printf(NOTIF_ERROR, "Task Mgr", "Failed to allocate K-Stack for PID: %d", new_task->id);
        kfree(new_task);
        return;
    }
    
    new_task->kstack = kstack_mem + KERNEL_STACK_SIZE;
    
    registers_t* regs = (registers_t*)(new_task->kstack - sizeof(registers_t));
    memset(regs, 0, sizeof(registers_t));
    
    regs->eip = entry_point;
    regs->cs = 0x1B;
    regs->ss = 0x23;
    regs->useresp = 0x40000000 + 0x100000;
    regs->eflags = 0x200; 
    
    regs->ds = 0x23;
    regs->es = 0x23;
    regs->fs = 0x23;
    regs->gs = 0x23;
    
    new_task->esp = (uint32_t)regs;
    
    new_task->next = task_list_head->next;
    task_list_head->next = new_task;
    
    serial_puts("[TASK] Created process PID: ");
    serial_puts_num(new_task->id);
    serial_puts("\n");
}

uint32_t task_switch(uint32_t current_esp) {
    if (!current_task) return current_esp;
    
    current_task->esp = current_esp;

    void* current_fpu = get_fpu_aligned_ptr(current_task);
    asm volatile("fxsave %0" : "=m"(*(char*)current_fpu));
    
    task_t* next_task = current_task->next;
    while (next_task->state != TASK_STATE_READY && next_task->state != TASK_STATE_RUNNING) {
        next_task = next_task->next;
    }
    
    if (next_task == current_task) {
        return current_esp;
    }
    
    if (current_task->state == TASK_STATE_RUNNING) {
        current_task->state = TASK_STATE_READY;
    }
    next_task->state = TASK_STATE_RUNNING;
    current_task = next_task;
    
    asm volatile("mov %0, %%cr3" : : "r"(current_task->page_directory));
    
    if (current_task->id != 0) {
        tss_set_stack(0x10, current_task->kstack);
    }

    void* next_fpu = get_fpu_aligned_ptr(current_task);
    asm volatile("fxrstor %0" : : "m"(*(char*)next_fpu));
    
    return current_task->esp;
}

void task_exit(void) {
    asm volatile("cli");
    
    serial_puts("[TASK] Process exited: PID ");
    serial_puts_num(current_task->id);
    serial_puts("\n");

    for (int i = 0; i < MAX_PROCESS_FILES; i++) {
        if (current_task->file_table[i] != NULL) {
            vfs_close(current_task->file_table[i]);
            current_task->file_table[i] = NULL;
        }
    }
    
    task_t* die_task = current_task;
    
    task_t* prev = task_list_head;
    while (prev->next != die_task) {
        prev = prev->next;
        if (prev == task_list_head && prev->next != die_task) {
            break; 
        }
    }
    
    prev->next = die_task->next;
    
    die_task->state = TASK_STATE_DEAD;

    asm volatile("int $0x20");
    
    while(1); 
}

void task_kill_by_pid(uint32_t pid) {
    unsigned long flags;
    asm volatile("pushf; pop %0; cli" : "=g"(flags));
    
    task_t* prev = task_list_head;
    task_t* curr = task_list_head->next;
    
    while (curr != task_list_head) {
        if (curr->id == pid) {
            serial_puts("[TASK] Killing PID: ");
            serial_puts_num(pid);
            serial_puts("\n");

            for (int i = 0; i < MAX_PROCESS_FILES; i++) {
                if (curr->file_table[i] != NULL) {
                    vfs_close(curr->file_table[i]);
                    curr->file_table[i] = NULL;
                }
            }
            
            prev->next = curr->next;
            curr->state = TASK_STATE_DEAD;
            
            if (curr != current_task) {
                kfree(curr);
            } else {
                asm volatile("pop %0; popf" : : "g"(flags));
                task_exit();
                return;
            }
            
            asm volatile("push %0; popf" : : "g"(flags));
            return;
        }
        prev = curr;
        curr = curr->next;
    }
    
    notif_printf(NOTIF_WARNING, "Task Mgr", "Failed to kill PID %d: Process not found", pid);
    
    asm volatile("push %0; popf" : : "g"(flags));
}