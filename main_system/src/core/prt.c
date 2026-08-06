#include "core/prt.h"
#include "drivers/vesa.h"
#include "kernel/task.h"

static const char* x86_exception_messages[32] = {
    "Divide-by-Zero Error", "Debug Exception", "Non-Maskable Interrupt",
    "Breakpoint Exception", "Overflow Exception", "BOUND Range Exceeded",
    "Invalid Opcode", "Device Not Available", "Double Fault",
    "Coprocessor Segment Overrun", "Invalid TSS", "Segment Not Present",
    "Stack-Segment Fault", "General Protection Fault", "Page Fault",
    "Reserved / Unknown Exception", "x87 FPU Floating-Point Error",
    "Alignment Check Exception", "Machine Check Exception", "SIMD Floating-Point",
    "Virtualization Exception", "Control Protection Exception", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved", "Reserved",
    "Hypervisor Injection", "VMM Communication", "Security Exception", "Reserved"
};

static int prt_pixel_y = 20;

static void prt_hex_to_str(uint32_t val, char* dest) {
    for (int i = 28, d = 0; i >= 0; i -= 4, d++) {
        int digit = (val >> i) & 0xF;
        dest[d] = (digit < 10) ? ('0' + digit) : ('A' + (digit - 10));
    }
    dest[8] = '\0';
}

static void prt_byte_to_hex(uint8_t val, char* dest) {
    int high = (val >> 4) & 0xF;
    int low = val & 0xF;
    dest[0] = (high < 10) ? ('0' + high) : ('A' + (high - 10));
    dest[1] = (low < 10) ? ('0' + low) : ('A' + (low - 10));
    dest[2] = '\0';
}

static void prt_itoa(uint32_t val, char* dest) {
    if (val == 0) {
        dest[0] = '0'; dest[1] = '\0';
        return;
    }
    char buf[12];
    int i = 0;
    while (val > 0) {
        buf[i++] = '0' + (val % 10);
        val /= 10;
    }
    int j = 0;
    while (i > 0) {
        dest[j++] = buf[--i];
    }
    dest[j] = '\0';
}

void prt_run(registers_t* r) {
    uint32_t color_white = 0x00FFFFFF;
    uint32_t color_black = 0x00000000;
    uint32_t color_yellow = 0x00FFFF00;

    vesa_clear_back_buffer(color_black);
    prt_pixel_y = 20;
    prt_pixel_y += 32; 

    vesa_draw_text_rus(20, prt_pixel_y, "A critical error has occurred in PozitronOS, making it impossible to continue", color_white, color_black);
    prt_pixel_y += 16;
    vesa_draw_text_rus(20, prt_pixel_y, "operating the system safely. The kernel execution has been intercepted.", color_white, color_black);
    prt_pixel_y += 32;

    vesa_draw_text_rus(20, prt_pixel_y, "EXCEPTION TYPE: ", color_white, color_black);
    
    char exc_buf[128];
    int e_idx = 0;
    
    if (r->int_no < 32) {
        const char* msg = x86_exception_messages[r->int_no];
        while (*msg) { exc_buf[e_idx++] = *msg++; }
    } else {
        const char* msg = "Unknown Interrupt / Hardware Fault";
        while (*msg) { exc_buf[e_idx++] = *msg++; }
    }
    
    exc_buf[e_idx++] = ' ';
    exc_buf[e_idx++] = '(';
    prt_itoa(r->int_no, &exc_buf[e_idx]);

    while (exc_buf[e_idx] != '\0') { e_idx++; }
    exc_buf[e_idx++] = ')';
    exc_buf[e_idx] = '\0';
    
    vesa_draw_text_rus(140, prt_pixel_y, exc_buf, color_white, color_black);
    prt_pixel_y += 16;

    vesa_draw_text_rus(20, prt_pixel_y, "CRASHED TASK  : ", color_white, color_black);
    char pid_str[16] = "PID ";
    if (current_task) {
        prt_itoa(current_task->id, &pid_str[4]);
        if (current_task->id == 0) {
            vesa_draw_text_rus(140, prt_pixel_y, "PID 0 (Kernel Core)", color_white, color_black);
        } else {
            vesa_draw_text_rus(140, prt_pixel_y, pid_str, color_white, color_black);
        }
    } else {
        vesa_draw_text_rus(140, prt_pixel_y, "Unknown (Tasks not initialized)", color_white, color_black);
    }
    prt_pixel_y += 16;

    uint32_t cs_selector = r->cs;
    vesa_draw_text_rus(20, prt_pixel_y, "CPU PRIVILEGE : ", color_white, color_black);
    
    if (cs_selector == 0x08 || (cs_selector & 0x3) == 0) {
        vesa_draw_text_rus(140, prt_pixel_y, "Ring 0 (Kernel Space)", color_white, color_black);
    } else if (cs_selector == 0x1B || (cs_selector & 0x3) == 3) {
        vesa_draw_text_rus(140, prt_pixel_y, "Ring 3 (User Space)", color_white, color_black);
    } else {
        uint16_t current_cs;
        asm volatile("mov %%cs, %0" : "=r"(current_cs));
        if (current_cs == 0x08) {
            vesa_draw_text_rus(140, prt_pixel_y, "Ring 0 (Kernel Panic Fallback)", color_white, color_black);
        } else {
            vesa_draw_text_rus(140, prt_pixel_y, "Ring Unknown (Corrupted Context)", color_white, color_black);
        }
    }
    prt_pixel_y += 32;

    vesa_draw_text_rus(20, prt_pixel_y, "--- HARDWARE DIAGNOSTIC REGISTER DUMP ---", color_white, color_black);
    prt_pixel_y += 20;

    char hbuf[9];
    char buf[128];
    int idx;

    idx = 0;
    prt_hex_to_str(r->eax, hbuf);
    buf[idx++] = 'E'; buf[idx++] = 'A'; buf[idx++] = 'X'; buf[idx++] = ':'; buf[idx++] = '0'; buf[idx++] = 'x';
    for(int i=0; i<8; i++) buf[idx++] = hbuf[i]; while(idx < 20) buf[idx++] = ' ';
    prt_hex_to_str(r->ebx, hbuf);
    buf[idx++] = 'E'; buf[idx++] = 'B'; buf[idx++] = 'X'; buf[idx++] = ':'; buf[idx++] = '0'; buf[idx++] = 'x';
    for(int i=0; i<8; i++) buf[idx++] = hbuf[i]; while(idx < 40) buf[idx++] = ' ';
    prt_hex_to_str(r->ecx, hbuf);
    buf[idx++] = 'E'; buf[idx++] = 'C'; buf[idx++] = 'X'; buf[idx++] = ':'; buf[idx++] = '0'; buf[idx++] = 'x';
    for(int i=0; i<8; i++) buf[idx++] = hbuf[i]; while(idx < 60) buf[idx++] = ' ';
    prt_hex_to_str(r->edx, hbuf);
    buf[idx++] = 'E'; buf[idx++] = 'D'; buf[idx++] = 'X'; buf[idx++] = ':'; buf[idx++] = '0'; buf[idx++] = 'x';
    for(int i=0; i<8; i++) buf[idx++] = hbuf[i]; buf[idx] = '\0';
    vesa_draw_text_rus(20, prt_pixel_y, buf, color_white, color_black);
    prt_pixel_y += 16;

    idx = 0;
    prt_hex_to_str(r->edi, hbuf);
    buf[idx++] = 'E'; buf[idx++] = 'D'; buf[idx++] = 'I'; buf[idx++] = ':'; buf[idx++] = '0'; buf[idx++] = 'x';
    for(int i=0; i<8; i++) buf[idx++] = hbuf[i]; while(idx < 20) buf[idx++] = ' ';
    prt_hex_to_str(r->esi, hbuf);
    buf[idx++] = 'E'; buf[idx++] = 'S'; buf[idx++] = 'I'; buf[idx++] = ':'; buf[idx++] = '0'; buf[idx++] = 'x';
    for(int i=0; i<8; i++) buf[idx++] = hbuf[i]; while(idx < 40) buf[idx++] = ' ';
    prt_hex_to_str(r->esp, hbuf);
    buf[idx++] = 'E'; buf[idx++] = 'S'; buf[idx++] = 'P'; buf[idx++] = ':'; buf[idx++] = '0'; buf[idx++] = 'x';
    for(int i=0; i<8; i++) buf[idx++] = hbuf[i]; while(idx < 60) buf[idx++] = ' ';
    prt_hex_to_str(r->ebp, hbuf);
    buf[idx++] = 'E'; buf[idx++] = 'B'; buf[idx++] = 'P'; buf[idx++] = ':'; buf[idx++] = '0'; buf[idx++] = 'x';
    for(int i=0; i<8; i++) buf[idx++] = hbuf[i]; buf[idx] = '\0';
    vesa_draw_text_rus(20, prt_pixel_y, buf, color_white, color_black);
    prt_pixel_y += 16;

    idx = 0;
    prt_hex_to_str(r->eip, hbuf);
    buf[idx++] = 'E'; buf[idx++] = 'I'; buf[idx++] = 'P'; buf[idx++] = ':'; buf[idx++] = '0'; buf[idx++] = 'x';
    for(int i=0; i<8; i++) buf[idx++] = hbuf[i]; while(idx < 20) buf[idx++] = ' ';
    prt_hex_to_str(r->eflags, hbuf);
    buf[idx++] = 'F'; buf[idx++] = 'L'; buf[idx++] = 'G'; buf[idx++] = ':'; buf[idx++] = '0'; buf[idx++] = 'x';
    for(int i=0; i<8; i++) buf[idx++] = hbuf[i]; while(idx < 40) buf[idx++] = ' ';
    prt_hex_to_str(cs_selector, hbuf);
    buf[idx++] = 'C'; buf[idx++] = 'S'; buf[idx++] = ' '; buf[idx++] = ':'; buf[idx++] = '0'; buf[idx++] = 'x';
    for(int i=0; i<8; i++) buf[idx++] = hbuf[i]; buf[idx] = '\0';
    vesa_draw_text_rus(20, prt_pixel_y, buf, color_white, color_black);
    prt_pixel_y += 16;

    uint32_t cr0, cr2, cr3;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    asm volatile("mov %%cr2, %0" : "=r"(cr2));
    asm volatile("mov %%cr3, %0" : "=r"(cr3));

    idx = 0;
    prt_hex_to_str(cr0, hbuf);
    buf[idx++] = 'C'; buf[idx++] = 'R'; buf[idx++] = '0'; buf[idx++] = ':'; buf[idx++] = '0'; buf[idx++] = 'x';
    for(int i=0; i<8; i++) buf[idx++] = hbuf[i]; while(idx < 20) buf[idx++] = ' ';
    prt_hex_to_str(cr2, hbuf);
    buf[idx++] = 'C'; buf[idx++] = 'R'; buf[idx++] = '2'; buf[idx++] = ':'; buf[idx++] = '0'; buf[idx++] = 'x';
    for(int i=0; i<8; i++) buf[idx++] = hbuf[i]; while(idx < 40) buf[idx++] = ' ';
    prt_hex_to_str(cr3, hbuf);
    buf[idx++] = 'C'; buf[idx++] = 'R'; buf[idx++] = '3'; buf[idx++] = ':'; buf[idx++] = '0'; buf[idx++] = 'x';
    for(int i=0; i<8; i++) buf[idx++] = hbuf[i]; buf[idx] = '\0';
    vesa_draw_text_rus(20, prt_pixel_y, buf, color_white, color_black);
    prt_pixel_y += 32;

    vesa_draw_text_rus(20, prt_pixel_y, "--- CODE DUMP (BYTES AROUND EIP) ---", color_white, color_black);
    prt_pixel_y += 20;

    uint32_t eip_start = r->eip - 8;
    idx = 0;
    prt_hex_to_str(eip_start, hbuf);
    buf[idx++] = '0'; buf[idx++] = 'x';
    for(int i=0; i<8; i++) buf[idx++] = hbuf[i];
    buf[idx++] = ':'; buf[idx++] = ' ';

    for (int k = 0; k < 16; k++) {
        uint32_t current_addr = eip_start + k;
        uint8_t byte = 0;
        
        if (current_addr >= 0x100000 && current_addr < 0xC0000000) { 
            byte = *(uint8_t*)current_addr;
        }

        char bhex[3];
        prt_byte_to_hex(byte, bhex);

        if (current_addr == r->eip) {
            buf[idx++] = '[';
            buf[idx++] = bhex[0];
            buf[idx++] = bhex[1];
            buf[idx++] = ']';
        } else {
            buf[idx++] = bhex[0];
            buf[idx++] = bhex[1];
            if (current_addr != r->eip - 1) { buf[idx++] = ' '; }
        }
    }
    buf[idx] = '\0';
    vesa_draw_text_rus(20, prt_pixel_y, buf, color_white, color_black);
    prt_pixel_y += 32;

    if (r->int_no == 14) {
        vesa_draw_text_rus(20, prt_pixel_y, "--- MEMORY DUMP (TARGET CR2 ADDR) ---", color_white, color_black);
        prt_pixel_y += 20;

        uint32_t cr2_start = cr2 & ~0xF;
        idx = 0;
        prt_hex_to_str(cr2_start, hbuf);
        buf[idx++] = '0'; buf[idx++] = 'x';
        for(int i=0; i<8; i++) buf[idx++] = hbuf[i];
        buf[idx++] = ':'; buf[idx++] = ' ';

        for(int k=0; k<16; k++) {
            uint32_t c_addr = cr2_start + k;
            uint8_t byte = 0;
            
            if (c_addr >= 0x1000 && c_addr < 0xFFFFFFF0) {
                byte = *(uint8_t*)c_addr;
            }
            char bhex[3];
            prt_byte_to_hex(byte, bhex);
            
            buf[idx++] = bhex[0];
            buf[idx++] = bhex[1];
            buf[idx++] = ' ';
        }
        buf[idx] = '\0';
        vesa_draw_text_rus(20, prt_pixel_y, buf, color_yellow, color_black);
        prt_pixel_y += 32;
    }

    vesa_draw_text_rus(20, prt_pixel_y, "If you see this screen for the first time, restart your computer. If this", color_white, color_black);
    prt_pixel_y += 16;
    vesa_draw_text_rus(20, prt_pixel_y, "screen appears multiple times recently, contact customer support with this information.", color_white, color_black);

    asm volatile ("cli");

    vesa_draw_text_rus(20, 20, "POZITRON OS: CRITICAL KERNEL PANIC", color_white, color_black);
    
    vesa_swap_buffers();

    while (1) {
        asm volatile("hlt");
    }
}