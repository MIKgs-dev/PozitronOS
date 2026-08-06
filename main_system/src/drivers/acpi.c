#include "drivers/acpi.h"
#include "kernel/memory.h"
#include "kernel/paging.h"
#include "kernel/types.h"
#include "core/event.h"
#include "drivers/pic.h"
#include "drivers/timer.h"
#include "drivers/serial.h"
#include "string.h"
#include "drivers/pci.h"
#include <uacpi/sleep.h>
#include <uacpi/uacpi.h>
#include <uacpi/event.h>

#define ACPI_VIRT_BASE          0x70000000
#define ACPI_SLOTS_COUNT        8
#define ACPI_SLOT_SIZE          (64 * 1024)
#define ACPI_POOL_START         0x7F000000

extern uint32_t acpi_tables_phys_addr;
extern uint32_t acpi_tables_size;
extern page_directory_t* kernel_directory;

typedef struct {
    uint32_t virt_addr;
    uint32_t phys_addr;
    uint32_t size;
    int is_used;
} acpi_mem_slot_t;

typedef struct {
    volatile uint32_t lock;
} osl_spinlock_t;

static acpi_mem_slot_t acpi_slots[ACPI_SLOTS_COUNT];
static int acpi_slots_initialized = 0;

/* ====================================================================
 * 1. MEMORY MANAGEMENT
 * ==================================================================== */

void* uacpi_kernel_alloc(uacpi_size size) {
    return kmalloc(size);
}

void uacpi_kernel_free(void* mem) {
    if (mem) kfree(mem);
}

void* uacpi_kernel_map(uacpi_phys_addr physical_address, uacpi_size length) {
    uint32_t phys = (uint32_t)physical_address;

    if (acpi_tables_size > 0 && phys >= acpi_tables_phys_addr && phys < (acpi_tables_phys_addr + acpi_tables_size)) {
        return (void*)(ACPI_VIRT_BASE + (phys - acpi_tables_phys_addr));
    }
    if (phys < 0x100000) {
        return (void*)phys;
    }

    if (!acpi_slots_initialized) {
        for (int i = 0; i < ACPI_SLOTS_COUNT; i++) {
            acpi_slots[i].virt_addr = ACPI_POOL_START + (i * ACPI_SLOT_SIZE);
            acpi_slots[i].is_used = 0;
        }
        acpi_slots_initialized = 1;
    }

    int slot_idx = -1;
    for (int i = 0; i < ACPI_SLOTS_COUNT; i++) {
        if (!acpi_slots[i].is_used) {
            slot_idx = i;
            break;
        }
    }

    if (slot_idx == -1) return NULL;

    uint32_t phys_page = phys & 0xFFFFF000;
    uint32_t offset    = phys & 0x00000FFF;
    uint32_t pages     = (length + offset + 4095) / 4096;

    uint32_t slot_virt = acpi_slots[slot_idx].virt_addr;
    for (uint32_t i = 0; i < pages; i++) {
        paging_map_page(kernel_directory, slot_virt + (i * 4096), phys_page + (i * 4096), 1 | 2);
    }

    acpi_slots[slot_idx].phys_addr = phys_page;
    acpi_slots[slot_idx].size = pages * 4096;
    acpi_slots[slot_idx].is_used = 1;

    for (uint32_t i = 0; i < pages; i++) {
        uint32_t addr = slot_virt + (i * 4096);
        asm volatile("invlpg (%0)" :: "r"(addr) : "memory");
    }

    return (void*)(slot_virt + offset);
}

void uacpi_kernel_unmap(void* virtual_address, uacpi_size length) {
    uint32_t virt = (uint32_t)virtual_address;

    if (virt < 0x100000 || (acpi_tables_size > 0 && virt >= ACPI_VIRT_BASE && virt < ACPI_VIRT_BASE + acpi_tables_size)) {
        return;
    }

    for (int i = 0; i < ACPI_SLOTS_COUNT; i++) {
        if (acpi_slots[i].is_used && virt >= acpi_slots[i].virt_addr && virt < (acpi_slots[i].virt_addr + ACPI_SLOT_SIZE)) {
            uint32_t pages = acpi_slots[i].size / 4096;
            for (uint32_t j = 0; j < pages; j++) {
                paging_unmap_page(kernel_directory, acpi_slots[i].virt_addr + (j * 4096));
            }
            acpi_slots[i].is_used = 0;
            asm volatile("mov %%cr3, %%eax; mov %%eax, %%cr3" ::: "eax");
            return;
        }
    }
}

/* ====================================================================
 * 2. PORT I/O INTERFACES
 * ==================================================================== */

uacpi_status uacpi_kernel_io_map(uacpi_phys_addr base, uacpi_size size, uacpi_handle *out_handle) {
    *out_handle = (uacpi_handle)(uintptr_t)base;
    (void)size;
    return UACPI_STATUS_OK;
}

void uacpi_kernel_io_unmap(uacpi_handle handle) {
    (void)handle;
}

uacpi_status uacpi_kernel_io_read8(uacpi_handle handle, uacpi_size offset, uacpi_u8 *out_value) {
    *out_value = inb((uint16_t)(uintptr_t)handle + offset);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_read16(uacpi_handle handle, uacpi_size offset, uacpi_u16 *out_value) {
    *out_value = inw((uint16_t)(uintptr_t)handle + offset);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_read32(uacpi_handle handle, uacpi_size offset, uacpi_u32 *out_value) {
    *out_value = inl((uint16_t)(uintptr_t)handle + offset);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_write8(uacpi_handle handle, uacpi_size offset, uacpi_u8 value) {
    outb((uint16_t)(uintptr_t)handle + offset, value);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_write16(uacpi_handle handle, uacpi_size offset, uacpi_u16 value) {
    outw((uint16_t)(uintptr_t)handle + offset, value);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_write32(uacpi_handle handle, uacpi_size offset, uacpi_u32 value) {
    outl((uint16_t)(uintptr_t)handle + offset, value);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_raw_io_read(uacpi_io_addr address, uacpi_u8 width, uacpi_u64 *value) {
    if (!value) return UACPI_STATUS_INVALID_ARGUMENT;
    switch (width) {
        case 1: *value = inb(address); break;
        case 2: *value = inw(address); break;
        case 4: *value = inl(address); break;
        default: return UACPI_STATUS_INVALID_ARGUMENT;
    }
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_raw_io_write(uacpi_io_addr address, uacpi_u8 width, uacpi_u64 value) {
    switch (width) {
        case 1: outb(address, (uint8_t)value); break;
        case 2: outw(address, (uint16_t)value); break;
        case 4: outl(address, (uint32_t)value); break;
        default: return UACPI_STATUS_INVALID_ARGUMENT;
    }
    return UACPI_STATUS_OK;
}

/* ====================================================================
 * 3. PCI CONFIGURATION INTERFACES
 * ==================================================================== */

uacpi_status uacpi_kernel_pci_device_open(uacpi_pci_address address, uacpi_handle *out_handle) {
    uacpi_pci_address *addr_copy = kmalloc(sizeof(uacpi_pci_address));
    if (!addr_copy) return UACPI_STATUS_OUT_OF_MEMORY;
    *addr_copy = address;
    *out_handle = (uacpi_handle)addr_copy;
    return UACPI_STATUS_OK;
}

void uacpi_kernel_pci_device_close(uacpi_handle handle) {
    if (handle) kfree(handle);
}

uacpi_status uacpi_kernel_pci_read8(uacpi_handle handle, uacpi_size offset, uacpi_u8 *out_val) {
    uacpi_pci_address *addr = (uacpi_pci_address*)handle;
    *out_val = pci_read8(addr->bus, addr->device, addr->function, offset);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_read16(uacpi_handle handle, uacpi_size offset, uacpi_u16 *out_val) {
    uacpi_pci_address *addr = (uacpi_pci_address*)handle;
    *out_val = pci_read16(addr->bus, addr->device, addr->function, offset);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_read32(uacpi_handle handle, uacpi_size offset, uacpi_u32 *out_val) {
    uacpi_pci_address *addr = (uacpi_pci_address*)handle;
    *out_val = pci_read32(addr->bus, addr->device, addr->function, offset);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_write8(uacpi_handle handle, uacpi_size offset, uacpi_u8 val) {
    uacpi_pci_address *addr = (uacpi_pci_address*)handle;
    pci_write8(addr->bus, addr->device, addr->function, offset, val);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_write16(uacpi_handle handle, uacpi_size offset, uacpi_u16 val) {
    uacpi_pci_address *addr = (uacpi_pci_address*)handle;
    pci_write16(addr->bus, addr->device, addr->function, offset, val);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_write32(uacpi_handle handle, uacpi_size offset, uacpi_u32 val) {
    uacpi_pci_address *addr = (uacpi_pci_address*)handle;
    pci_write32(addr->bus, addr->device, addr->function, offset, val);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_read(uacpi_pci_address address, uacpi_size offset, uacpi_u8 width, uacpi_u64 *value) {
    if (!value) return UACPI_STATUS_INVALID_ARGUMENT;
    switch (width) {
        case 1: *value = pci_read8(address.bus, address.device, address.function, offset); break;
        case 2: *value = pci_read16(address.bus, address.device, address.function, offset); break;
        case 4: *value = pci_read32(address.bus, address.device, address.function, offset); break;
        default: return UACPI_STATUS_INVALID_ARGUMENT;
    }
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_write(uacpi_pci_address address, uacpi_size offset, uacpi_u8 width, uacpi_u64 value) {
    switch (width) {
        case 1: pci_write8(address.bus, address.device, address.function, offset, (uint8_t)value); break;
        case 2: pci_write16(address.bus, address.device, address.function, offset, (uint16_t)value); break;
        case 4: pci_write32(address.bus, address.device, address.function, offset, (uint32_t)value); break;
        default: return UACPI_STATUS_INVALID_ARGUMENT;
    }
    return UACPI_STATUS_OK;
}

/* ====================================================================
 * 4. INTERRUPTS & SPINLOCKS
 * ==================================================================== */

static uacpi_interrupt_handler osl_sci_handler = NULL;
static uacpi_handle osl_sci_context = NULL;

static void osl_sci_bridge(registers_t* r) {
    if (osl_sci_handler) {
        osl_sci_handler(osl_sci_context);
    }
    pic_send_eoi(r->int_no - IRQ0);
}

uacpi_status uacpi_kernel_install_interrupt_handler(
    uacpi_u32 irq, uacpi_interrupt_handler handler, uacpi_handle ctx, uacpi_handle *out_irq_handle
) {
    osl_sci_handler = handler;
    osl_sci_context = ctx;
    irq_install_handler(IRQ0 + irq, osl_sci_bridge);
    
    *out_irq_handle = (uacpi_handle)handler; 
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_uninstall_interrupt_handler(
    uacpi_interrupt_handler handler, uacpi_handle irq_handle
) {
    (void)irq_handle;
    if (osl_sci_handler == handler) {
        osl_sci_handler = NULL;
        osl_sci_context = NULL;
    }
    return UACPI_STATUS_OK;
}

uacpi_handle uacpi_kernel_create_spinlock(void) {
    osl_spinlock_t *lock = kmalloc(sizeof(osl_spinlock_t));
    if (lock) lock->lock = 0;
    return (uacpi_handle)lock;
}

void uacpi_kernel_free_spinlock(uacpi_handle handle) {
    if (handle) kfree(handle);
}

uacpi_cpu_flags uacpi_kernel_lock_spinlock(uacpi_handle handle) {
    (void)handle;
    uacpi_cpu_flags flags;
    asm volatile("pushf; pop %0; cli" : "=rm"(flags) :: "memory");
    return flags;
}

void uacpi_kernel_unlock_spinlock(uacpi_handle handle, uacpi_cpu_flags flags) {
    (void)handle;
    asm volatile("push %0; popf" :: "rm"(flags) : "memory");
}

uacpi_interrupt_state uacpi_kernel_disable_interrupts(void) {
    uacpi_interrupt_state flags;
    asm volatile("pushf; pop %0; cli" : "=rm"(flags) :: "memory");
    return flags;
}

void uacpi_kernel_restore_interrupts(uacpi_cpu_flags flags) {
    asm volatile("push %0; popf" :: "rm"(flags) : "memory");
}

/* ====================================================================
 * 5. MUTEXES & EVENTS
 * ==================================================================== */

uacpi_thread_id uacpi_kernel_get_thread_id(void) {
    return (uacpi_thread_id)1; 
}

uacpi_handle uacpi_kernel_create_mutex(void) {
    uint32_t *dummy_mut = kmalloc(sizeof(uint32_t));
    if (dummy_mut) *dummy_mut = 1;
    return (uacpi_handle)dummy_mut;
}

void uacpi_kernel_free_mutex(uacpi_handle handle) {
    if (handle) kfree(handle);
}

uacpi_status uacpi_kernel_acquire_mutex(uacpi_handle handle, uacpi_u16 timeout) {
    (void)handle; (void)timeout;
    return UACPI_STATUS_OK;
}

void uacpi_kernel_release_mutex(uacpi_handle handle) {
    (void)handle;
}

void uacpi_kernel_lock_mutex(uacpi_handle handle) {
    (void)handle;
}

void uacpi_kernel_unlock_mutex(uacpi_handle handle) {
    (void)handle;
}

uacpi_handle uacpi_kernel_create_event(void) {
    return uacpi_kernel_create_mutex(); 
}

void uacpi_kernel_free_event(uacpi_handle handle) {
    uacpi_kernel_free_mutex(handle);
}

void uacpi_kernel_reset_event(uacpi_handle handle) {
    (void)handle;
}

uacpi_bool uacpi_kernel_wait_for_event(uacpi_handle handle, uacpi_u16 timeout) {
    (void)handle; (void)timeout;
    return UACPI_TRUE;
}

void uacpi_kernel_signal_event(uacpi_handle handle) {
    (void)handle;
}

/* ====================================================================
 * 6. TIMING, WORK & LOGGING
 * ==================================================================== */

static uacpi_u64 tsc_ticks_per_100ns = 0;

static inline uacpi_u64 read_tsc(void) {
    uint32_t lo, hi;
    asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uacpi_u64)hi << 32) | lo;
}

static void calibrate_tsc(void) {
    uint32_t start_ticks = timer_get_ticks();
    while (timer_get_ticks() == start_ticks) {
        asm volatile("pause");
    }

    uacpi_u64 tsc_start = read_tsc();

    uint32_t target_ticks = start_ticks + 3; 
    while (timer_get_ticks() < target_ticks) {
        asm volatile("pause");
    }

    uacpi_u64 tsc_end = read_tsc();
    uacpi_u64 tsc_delta = tsc_end - tsc_start;

    tsc_ticks_per_100ns = tsc_delta / 200000;

    if (tsc_ticks_per_100ns == 0) tsc_ticks_per_100ns = 1;

    serial_printf("[ACPI] TSC Calibrated: %u ticks per 100ns\n", (uint32_t)tsc_ticks_per_100ns);
}

uacpi_u64 uacpi_kernel_get_ticks(void) {
    if (uacpi_unlikely(tsc_ticks_per_100ns == 0)) {
        calibrate_tsc();
    }
    return read_tsc() / tsc_ticks_per_100ns;
}

void uacpi_kernel_sleep(uacpi_u64 msec) {
    timer_sleep_ms((uint32_t)msec);
}

void uacpi_kernel_stall(uacpi_u8 usec) {
    for (volatile uint32_t i = 0; i < (uint32_t)usec * 50; i++) {
        asm volatile("nop");
    }
}

void uacpi_kernel_log(uacpi_log_level level, const uacpi_char* str) {
    (void)level;
    serial_puts(str);
}

uacpi_status uacpi_kernel_schedule_work(uacpi_work_type type, uacpi_work_handler handler, uacpi_handle ctx) {
    (void)type;
    handler(ctx); 
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_wait_for_work_completion(void) {
    return UACPI_STATUS_OK;
}

uacpi_u64 uacpi_kernel_get_nanoseconds_since_boot(void) {
    if (uacpi_unlikely(tsc_ticks_per_100ns == 0)) {
        calibrate_tsc();
    }
    
    return (read_tsc() / tsc_ticks_per_100ns) * 100ULL;
}

/* ====================================================================
 * 7. ACPI SUBSYSTEM INITIALIZATION & ACTIONS
 * ==================================================================== */

uacpi_status uacpi_kernel_get_rsdp(uacpi_phys_addr *out_rsdp_address) {
    uint16_t ebda_window = *(volatile uint16_t *)0x0000040E;
    uint32_t ebda_phys = (uint32_t)ebda_window << 4;
    
    if (ebda_phys >= 0x80000 && ebda_phys < 0xA0000) {
        volatile uint8_t *ebda_mem = (volatile uint8_t *)ebda_phys;
        for (uint32_t i = 0; i < 1024; i += 16) {
            if (ebda_mem[i] == 'R' && ebda_mem[i+1] == 'S' && ebda_mem[i+2] == 'D' && ebda_mem[i+3] == ' ' &&
                ebda_mem[i+4] == 'P' && ebda_mem[i+5] == 'T' && ebda_mem[i+6] == 'R' && ebda_mem[i+7] == ' ') {
                
                *out_rsdp_address = (uacpi_phys_addr)(ebda_phys + i);
                return UACPI_STATUS_OK;
            }
        }
    }

    volatile uint8_t *bios_mem = (volatile uint8_t *)0x000E0000;
    for (uint32_t i = 0; i < 0x20000; i += 16) {
        if (bios_mem[i] == 'R' && bios_mem[i+1] == 'S' && bios_mem[i+2] == 'D' && bios_mem[i+3] == ' ' &&
            bios_mem[i+4] == 'P' && bios_mem[i+5] == 'T' && bios_mem[i+6] == 'R' && bios_mem[i+7] == ' ') {
            
            *out_rsdp_address = (uacpi_phys_addr)(0x000E0000 + i);
            return UACPI_STATUS_OK;
        }
    }

    return UACPI_STATUS_NOT_FOUND;
}

uacpi_status uacpi_kernel_handle_firmware_request(uacpi_firmware_request *req) {
    (void)req;
    return UACPI_STATUS_UNIMPLEMENTED;
}

int acpi_init(void) {
    uacpi_status status;

    uint32_t phys_bios = 0x000E0000; 
    uint32_t virt_bios = 0x000E0000;
    for (int i = 0; i < 32; i++) {
        paging_map_page(kernel_directory, virt_bios + (i * PAGE_SIZE), phys_bios + (i * PAGE_SIZE), 1 | 2);
    }

    serial_puts("[ACPI] Initializing...\n");
    status = uacpi_initialize(0); 
    if (uacpi_unlikely(status != UACPI_STATUS_OK)) {
        serial_puts("[ACPI] Initializing failed\n");
        return 0;
    }

    serial_puts("[uACPI] Loading namespace...\n");
    status = uacpi_namespace_load();
    if (uacpi_unlikely(status != UACPI_STATUS_OK)) {
        serial_puts("[ACPI] Loading namespace failed\n");
        return 0;
    }

    serial_puts("[uACPI] Initializing namespace objects...\n");
    status = uacpi_namespace_initialize();
    if (uacpi_unlikely(status != UACPI_STATUS_OK)) {
        serial_puts("[ACPI] Namespace initialization failed\n");
        return 0;
    }

    serial_puts("[uACPI] Finalizing GPE and entering ACPI mode...\n");
    status = uacpi_finalize_gpe_initialization();
    if (uacpi_unlikely(status != UACPI_STATUS_OK)) {
        serial_puts("[ACPI] GPE initialization failed\n");
        return 0;
    }

    serial_puts("[ACPI] ACPI ready for command\n");
    return 1;
}

void acpi_poweroff(void) {
    serial_puts("[uACPI] Entering S5 sleep state...\n");
    uacpi_prepare_for_sleep_state(UACPI_SLEEP_STATE_S5);
    asm volatile("cli");
    uacpi_enter_sleep_state(UACPI_SLEEP_STATE_S5);
    while(1) { asm volatile("hlt"); }
}

void acpi_reboot(void) {
    serial_puts("[uACPI] Executing system reboot...\n");
    uacpi_reboot();
    while(1) { asm volatile("hlt"); }
}