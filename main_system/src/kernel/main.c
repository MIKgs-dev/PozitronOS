#include "drivers/serial.h"
#include "drivers/vga.h"
#include "drivers/pic.h"
#include "drivers/timer.h"
#include "drivers/keyboard.h"
#include "drivers/mouse.h"
#include "drivers/cmos.h"
#include "core/gdt.h"
#include "core/idt.h"
#include "core/isr.h"
#include "core/event.h"
#include "drivers/vesa.h"
#include "kernel/memory.h"
#include "gui/gui.h"
#include "gui/setup.h"
#include "hw/scanner.h"
#include "drivers/power.h"
#include "gui/shutdown.h"
#include "kernel/multiboot_util.h"
#include "kernel/logo.h"
#include "stddef.h"
#include "lib/string.h"
#include "kernel/auth.h"
#include "gui/login.h"
#include "kernel/paging.h"
#include "kernel/device.h"
#include "kernel/notif.h"
#include "drivers/ahci.h"
#include "drivers/ata.h"
#include "drivers/disk.h"
#include "drivers/acpi.h"
#include "fs/vfs.h"
#include "kernel/drivermgr.h"
#include "kernel/syscall.h"
#include "loader/elf.h"
#include "kernel/task.h"
#include "core/cpu.h"
#include "lib/math.h"

static uint8_t system_running = 1;
uint8_t taskbar_disabled = 1;

extern void check_stack_overflow(void);

#define TARGET_FPS 45
#define FRAME_TIME_MS (1000 / TARGET_FPS)

static void handle_keyboard_events(event_t* event) {
    if (!event) return;
    
    if (event->type == EVENT_KEY_PRESS) {
        switch (event->data1) {
            case 0x3B:  // F1
                elf_load("/PozitronOS/test_prog.poz");
                break;                
            default:
                break;
        }
    }
}

void start_desktop(const char* username) {
    serial_puts("[DESKTOP] Starting desktop for user: ");
    serial_puts(username);
    serial_puts("\n");
    
    taskbar_disabled = 0;
    taskbar_init();

    //vesa_set_wallpaper("/PozitronOS/SysFiles/wallpapers/wallpaper.bmp");

    // TODO: создать окно рабочего стола, загрузить обои и т.д.
}

void kernel_main(uint32_t magic, multiboot_info_t* mb_info) {
    multiboot_dump_info(mb_info);
    multiboot_extract_acpi_layout(mb_info);
    memory_init_multiboot(mb_info);
    extern uint32_t stack_guard;
    stack_guard = 0xDEADBEEF;

    serial_init();
    vga_init();
    vga_puts("\n");

    gdt_init();
    vga_puts("[ OK ] GDT OK\n");
    idt_init();
    vga_puts("[ OK ] IDT OK\n");
    pic_init();
    vga_puts("[ OK ] PIC OK\n");
    isr_init();
    vga_puts("[ OK ] ISR OK\n");
    asm volatile("sti");

    timer_init(100);
    vga_puts("[ OK ] TIMER OK\n");

    cpu_init();
    vga_puts("[ OK ] CPU OK\n");
    
    memory_init();
    memory_dump();
    vga_puts("[ OK ] MEMORY ALLOCATION SYSTEM OK\n");
    debug_heap_layout();
    
    if(!vesa_init(mb_info)) {
        vga_puts("[ERROR] VBE/VESA INITIALISATION FAILED\n");
    } else {
        vga_puts("[ OK ] VBE/VESA OK\n");
    }
    vesa_enable_double_buffer();

    show_boot_logo();
    boot_progress = 15;
    update_boot_progress();

    scanner_init();
    vga_puts("[INFO] SCANNING HARDWARE START\n");
    scanner_scan_all();
    scanner_dump_all();
    vga_puts("[ OK ] SCANNING HARDWARE FINISH\n");

    paging_init();

    uint32_t kernel_cr3;
    asm volatile("mov %%cr3, %0" : "=r"(kernel_cr3));
    task_init(kernel_cr3);

    cmos_init();
    vga_puts("[ OK ] CMOS RTC OK\n");
    boot_progress = 25;
    update_boot_progress();

    acpi_init();

    device_init();
    notif_init();

    boot_progress = 40;
    update_boot_progress();

    disk_init();

    vfs_init();

    disk_t* boot_disk = disk_get(0);
    if (boot_disk) {
        int ext2_part = disk_find_partition_by_type(boot_disk, 0x83);
        if (ext2_part >= 0) {
            serial_puts("[VFS] Found ext2 partition, mounting...\n");
            if (vfs_mount("0", "/", "ext2") != 0) {
                serial_puts("[VFS] WARNING: Failed to mount ext2 rootfs\n");
                notif_error("VFS", "CRITICAL: Failed to mount Ext2 root filesystem on disk 0.");
            } else {
                serial_puts("[VFS] Root filesystem mounted successfully\n");
            }
        } else {
            serial_puts("[VFS] WARNING: No ext2 partition found on disk 0\n");
            notif_error("VFS", "CRITICAL: No valid Ext2 partition detected.");
        }
    } else {
        serial_puts("[VFS] WARNING: No boot disk found\n");
        notif_error("VFS", "CRITICAL: Boot disk 0 is missing or corrupted.");
    }

    drvman_init();
    drvman_set_path("/PozitronOS/Sys32/Sysdriv/");
    drvman_scan();
    drvman_autoprobe();
    drvman.dump();

    boot_progress = 50;
    update_boot_progress();

    auth_init(); 

    boot_progress = 60;
    update_boot_progress();

    uint32_t screen_width = vesa_get_width();
    uint32_t screen_height = vesa_get_height();

    vesa_cache_background();
    vesa_init_dirty();
    vesa_mark_dirty_all();
    vesa_cursor_init();
    vesa_cursor_set_visible(1);
    boot_progress = 70;
    update_boot_progress();

    boot_progress = 90;
    update_boot_progress();

    event_init();
    vga_puts("[ OK ] EVENT SYSTEM OK\n");

    mouse_init();
    keyboard_init();

    boot_progress = 100;
    update_boot_progress();
    fade_out_boot_logo();

    if (vesa_is_background_cached()) {
        vesa_restore_background();
    }

    vga_puts("[INFO] STARTUP GUI ENVIRONMENT\n");
    gui_init(screen_width, screen_height);

    taskbar_init();

    if (is_first_boot()) {
        taskbar_disabled = 1;
        show_setup_window();
    } else {
        taskbar_disabled = 1;
        char* username = show_login_screen(NULL);
        if (username) {
            start_desktop(username);
            kfree(username);
        } else {
            notif_warning("Login", "Login cancelled or authentication failed.");
        }
    }

    serial_puts("\n=== SYSTEM READY ===\n");
    vga_puts("[INFO] SYSTEMS READY\n");
    memory_dump();

    gui_render();
    vesa_cursor_update();
    if (vesa_is_double_buffer_enabled()) {
        vesa_swap_buffers();
    }

    while(system_running) {
        static uint32_t last_frame_time = 0;
        uint32_t now = timer_get_ticks_ms();
    
        asm volatile("sti");
    
        event_t event;
        while (event_poll(&event)) {
            gui_handle_event(&event);
            handle_keyboard_events(&event);
        }
    
        mouse_update();
    
        if (now - last_frame_time >= FRAME_TIME_MS) {
            vesa_hide_cursor();
        
            if (is_shutdown_mode_active()) {
                update_shutdown_animation();
            }
        
            if (vesa_is_background_cached()) {
                vesa_restore_background_dirty();
            }
        
            gui_render();
            notif_update();
            notif_render();
        
            vesa_show_cursor();
            vesa_cursor_update();
        
            if (vesa_is_double_buffer_enabled()) {
                vesa_swap_buffers();
            }
        
            last_frame_time = now;
        }
    
        if (!event_available()) {
            asm volatile("hlt");
        }
    }
}