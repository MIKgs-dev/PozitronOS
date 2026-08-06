#include "core/gdt.h"
#include "drivers/serial.h"
#include "lib/string.h"

static struct gdt_entry gdt[GDT_ENTRIES];
static struct gdt_ptr gp;
struct tss_entry tss;

extern uint32_t stack_top;

static void gdt_set_entry(int idx, uint32_t base, uint32_t limit, uint8_t access, uint8_t flags) {
    gdt[idx].base_low = base & 0xFFFF;
    gdt[idx].base_middle = (base >> 16) & 0xFF;
    gdt[idx].base_high = (base >> 24) & 0xFF;
    
    gdt[idx].limit_low = limit & 0xFFFF;
    gdt[idx].granularity = ((limit >> 16) & 0x0F) | (flags & 0xF0);
    gdt[idx].access = access;
}

void tss_set_stack(uint32_t ss0, uint32_t esp0) {
    tss.ss0 = ss0;
    tss.esp0 = esp0;
}

void gdt_init(void) {
    serial_puts("[GDT] Initializing...\n");
    
    gp.limit = sizeof(gdt) - 1;
    gp.base = (uint32_t)gdt;
    
    memset(gdt, 0, sizeof(gdt));
    
    gdt_set_entry(0, 0, 0, 0, 0);
    
    gdt_set_entry(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);
    
    gdt_set_entry(2, 0, 0xFFFFFFFF, 0x92, 0xCF);
    
    gdt_set_entry(3, 0, 0xFFFFFFFF, 0xFA, 0xCF);
    
    gdt_set_entry(4, 0, 0xFFFFFFFF, 0xF2, 0xCF);
    
    memset(&tss, 0, sizeof(tss));
    tss.ss0 = SEL_KERNEL_DATA;
    tss.esp0 = (uint32_t)&stack_top;
    tss.iomap_base = sizeof(tss);
    
    gdt_set_entry(5, (uint32_t)&tss, sizeof(tss) - 1, 0x89, 0x00);
    
    serial_puts("[GDT] TSS at 0x");
    serial_puts_num_hex((uint32_t)&tss);
    serial_puts("\n");
    
    gdt_flush((uint32_t)&gp);
    tss_flush();
    
    serial_puts("[GDT] Initialized\n");
}