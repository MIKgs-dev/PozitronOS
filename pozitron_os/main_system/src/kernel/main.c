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
#include "kernel/scheduler.h"
#include "kernel/paging.h"
#include "kernel/userspace.h"
#include "kernel/device.h"
#include "kernel/notif.h"
#include "drivers/ahci.h"
#include "drivers/ata.h"
#include "drivers/disk.h"
#include "fs/vfs.h"

static uint8_t system_running = 1;
uint8_t taskbar_disabled = 1;

extern void check_stack_overflow(void);

static void handle_keyboard_events(event_t* event) {
    if (!event) return;
    
    if (event->type == EVENT_KEY_PRESS) {
        switch (event->data1) {
            case 0x01:
                if (gui_state.focused_window && 
                    IS_VALID_WINDOW_PTR(gui_state.focused_window)) {
                    wm_close_window(gui_state.focused_window);
                }
                break;
                
            case 0x57:
                if (gui_state.focused_window && 
                    IS_VALID_WINDOW_PTR(gui_state.focused_window) &&
                    gui_state.focused_window->maximizable) {
                    if (gui_state.focused_window->maximized) {
                        wm_restore_window(gui_state.focused_window);
                    } else {
                        wm_maximize_window(gui_state.focused_window);
                    }
                }
                break;
                
            case 0x0F:
                if (event->data2 & 0x02) {
                    gui_focus_prev();
                } else {
                    gui_focus_next();
                }
                break;
                
            case 0x5B:
            case 0x5C:
                if (!taskbar_disabled) {
                    start_menu_toggle();
                }
                break;
                
            default:
                break;
        }
    }
}

void kernel_main(uint32_t magic, multiboot_info_t* mb_info) {
    multiboot_dump_info(mb_info);
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

    cmos_init();
    vga_puts("[ OK ] CMOS RTC OK\n");
    boot_progress = 25;
    update_boot_progress();

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
            } else {
                serial_puts("[VFS] Root filesystem mounted successfully\n");
            }
        } else {
            serial_puts("[VFS] WARNING: No ext2 partition found on disk 0\n");
        }
    } else {
        serial_puts("[VFS] WARNING: No boot disk found\n");
    }

    





    // ============ ТЕСТ EXT2 ============
    serial_puts("\n=== EXT2 TEST ===\n");

    // Тест 1: запись
    serial_puts("[TEST 1] Writing /test.txt...\n");
    struct vfs_file* test_file;
    if (vfs_open("/test.txt", FS_O_WRONLY | FS_O_CREAT, &test_file) == 0) {
        uint32_t written;
        char test_data[] = "Hello from PozitronOS! Write test passed.\n";
        if (vfs_write(test_file, test_data, strlen(test_data), &written) == 0) {
            serial_puts("[TEST 1] Write OK (");
            serial_puts_num(written);
            serial_puts(" bytes)\n");
        } else {
            serial_puts("[TEST 1] Write FAILED\n");
        }
        vfs_close(test_file);
    } else {
        serial_puts("[TEST 1] Failed to create file\n");
    }

    // Тест 2: чтение
    serial_puts("[TEST 2] Reading /test.txt...\n");
    struct vfs_file* read_file;
    if (vfs_open("/test.txt", FS_O_RDONLY, &read_file) == 0) {
        char buffer[128];
        uint32_t bytes_read;
        memset(buffer, 0, sizeof(buffer));
        if (vfs_read(read_file, buffer, sizeof(buffer) - 1, &bytes_read) == 0) {
            buffer[bytes_read] = '\0';
            serial_puts("[TEST 2] Read OK, content: \"");
            serial_puts(buffer);
            serial_puts("\"\n");
        } else {
            serial_puts("[TEST 2] Read FAILED\n");
        }
        vfs_close(read_file);
    } else {
        serial_puts("[TEST 2] Failed to open file\n");
    }

    // Тест 3: создание директории
    serial_puts("[TEST 3] Creating /testdir...\n");
    if (vfs_mkdir("/testdir", 0755) == 0) {
        serial_puts("[TEST 3] Directory created OK\n");
    } else {
        serial_puts("[TEST 3] Failed to create directory\n");
    }

    serial_puts("=== END OF EXT2 TEST ===\n\n");








    boot_progress = 50;
    update_boot_progress();

    scheduler_init();

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

    keyboard_init();
    mouse_init();

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
        serial_puts("[CONFIG] First boot detected - showing setup window\n");
        taskbar_disabled = 1;
        show_setup_window();
    } else {
        serial_puts("[CONFIG] Normal boot - system ready\n");
        taskbar_disabled = 1;
        // TODO: show_login_window();
    }

    serial_puts("\n=== SYSTEM READY ===\n");
    vga_puts("[INFO] SYSTEMS READY\n");

    gui_render();
    vesa_cursor_update();
    if (vesa_is_double_buffer_enabled()) {
        vesa_swap_buffers();
    }

    while(system_running) {
        check_stack_overflow();
        asm volatile("hlt");

        event_t event;
        while (event_poll(&event)) {
            gui_handle_event(&event);
            handle_keyboard_events(&event);
        }

        vesa_hide_cursor();

        mouse_update();

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
    }
}